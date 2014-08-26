//---------------------------------------------------------------------------//
/*
  Copyright (c) 2012, Stuart R. Slattery
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

  *: Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  *: Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  *: Neither the name of the University of Wisconsin - Madison nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
//---------------------------------------------------------------------------//
/*!
 * \file MCLS_MCSAModelEvaluator_impl.hpp
 * \author Stuart R. Slattery
 * \brief Monte Carlo Synthetic Acceleration solver manager implementation.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_MCSAMODELEVALUATOR_IMPL_HPP
#define MCLS_MCSAMODELEVALUATOR_IMPL_HPP

#include <string>
#include <iostream>
#include <iomanip>

#include "MCLS_DBC.hpp"
#include "MCLS_AdjointSolverManager.hpp"
#include "MCLS_ForwardSolverManager.hpp"
#include "MCLS_ThyraVectorExtraction.hpp"

#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_Ptr.hpp>
#include <Teuchos_TimeMonitor.hpp>
#include <Teuchos_Time.hpp>

namespace MCLS
{
//---------------------------------------------------------------------------//
/*!
 * \brief Comm constructor. setProblem() must be called before solve().
 */
template<class Vector, class Matrix, class RNG>
MCSAModelEvaluator<Vector,Matrix,RNG>::MCSAModelEvaluator( 
    const Teuchos::RCP<const Comm>& global_comm,
    const Teuchos::RCP<Teuchos::ParameterList>& plist )
    : d_global_comm( global_comm )
    , d_plist( plist )
    , d_num_smooth( 1 )
{
    MCLS_REQUIRE( Teuchos::nonnull(d_global_comm) );
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );

    // Set the number of smoothing steps.
    d_num_smooth = 1;
    if ( d_plist->isParameter("Smoother Steps") )
    {
	d_num_smooth = d_plist->get<int>("Smoother Steps");
    }
}

//---------------------------------------------------------------------------//
/*!
 * \brief Constructor.
 */
template<class Vector, class Matrix, class RNG>
MCSAModelEvaluator<Vector,Matrix,RNG>::MCSAModelEvaluator( 
    const Teuchos::RCP<const Comm>& global_comm,
    const Teuchos::RCP<Teuchos::ParameterList>& plist,
    const Teuchos::RCP<const Matrix>& A,
    const Teuchos::RCP<const Vector>& b,
    const Teuchos::RCP<const Matrix>& M )
    : d_global_comm( global_comm )
    , d_plist( plist )
    , d_A( A )
    , d_b( b )
    , d_M( M )
    , d_num_smooth( 1 )
{
    MCLS_REQUIRE( Teuchos::nonnull(d_global_comm) );
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );

    // Create the resiudal.
    d_r = MT::cloneVectorFromMatrixDomain( *d_A );
    d_work_vec = VT::clone( *d_r );

    // Create the vector space for the LHS.
    d_x_space = 
	ThyraVectorExtraction<Vector,Matrix>::createVectorSpaceFromDomain( *d_A );
    d_f_space = d_x_space;

    // Set the number of smoothing steps.
    d_num_smooth = 1;
    if ( d_plist->isParameter("Smoother Steps") )
    {
	d_num_smooth = d_plist->get<int>("Smoother Steps");
    }

    // Build the residual Monte Carlo problem.
    buildResidualMonteCarloProblem();
}

//---------------------------------------------------------------------------//
/*!
 * \brief Set the linear problem with the manager.
 */
template<class Vector, class Matrix, class RNG>
void MCSAModelEvaluator<Vector,Matrix,RNG>::setProblem( 
    const Teuchos::RCP<const Matrix>& A,
    const Teuchos::RCP<const Vector>& b,
    const Teuchos::RCP<const Matrix>& M )
{
    MCLS_REQUIRE( Teuchos::nonnull(d_global_comm) );
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );

    // Determine if the linear operator has changed. It is presumed the
    // preconditioners are bound to the linear operator and will therefore
    // change when the operator changes. The mechanism here for determining if
    // the operator has changed is checking if the memory address is the
    // same. This may not be the best way to check.
    bool update_operator = true;
    if ( d_A.getRawPtr() == A.getRawPtr() )
    {
	update_operator = false;
    }

    // Set the problem.
    d_A = A;
    d_b = b;
    d_M = M;

    // Create the resiudal.
    d_r = MT::cloneVectorFromMatrixDomain( *d_A );
    d_work_vec = VT::clone( *d_r );

    // Create the vector space for the LHS.
    d_x_space = 
	ThyraVectorExtraction<Vector,Matrix>::createVectorSpaceFromDomain( *d_A );
    d_f_space = d_x_space;

    // Update the residual problem if it already exists.
    if ( Teuchos::nonnull(d_mc_solver) )
    {
	if ( update_operator )
	{
	    d_mc_problem->setOperator( d_A );
	    d_mc_problem->setLeftPrec( d_M );
	}

	// Set the updated residual problem with the Monte Carlo solver.
	d_mc_solver->setProblem( d_mc_problem );
    }
    // Otherwise this is initialization.
    else
    {
	buildResidualMonteCarloProblem();
    }
}

//---------------------------------------------------------------------------//
/*!
 * \brief Set the parameters for the manager. The manager will modify this
 * list with default parameters that are not defined.
 */
template<class Vector, class Matrix, class RNG>
void MCSAModelEvaluator<Vector,Matrix,RNG>::setParameters( 
    const Teuchos::RCP<Teuchos::ParameterList>& params )
{
    MCLS_REQUIRE( Teuchos::nonnull(params) );
    MCLS_REQUIRE( Teuchos::nonnull(d_mc_solver) );

    // Set the number of smoothing steps.
    d_num_smooth = 1;
    if ( d_plist->isParameter("Smoother Steps") )
    {
	d_num_smooth = d_plist->get<int>("Smoother Steps");
    }

    // Set the parameters.
    d_plist = params;

    // Propagate the parameters to the existing Monte Carlo solver.
    d_mc_solver->setParameters( d_plist );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the preconditioned residual given a LHS.
 */
template<class Vector, class Matrix, class RNG>
Teuchos::RCP<Vector> 
MCSAModelEvaluator<Vector,Matrix,RNG>::getPrecResidual( 
    const Teuchos::RCP<Vector>& x ) const
{
    MCLS_REQUIRE( Teuchos::nonnull(d_A) );
    MCLS_REQUIRE( Teuchos::nonnull(x) );
    MCLS_REQUIRE( Teuchos::nonnull(d_b) );

    MT::apply( *d_A, *x, *d_r );
    VT::update( *d_r, -1.0, *d_b, 1.0 );

    // Apply left preconditioning if necessary.
    if ( Teuchos::nonnull(d_M) )
    {
	VT::update( *d_work_vec, 0.0, *d_r, 1.0 );
	MT::apply( *d_M, *d_work_vec, *d_r );
    }

    return d_r;
}

//---------------------------------------------------------------------------//
// Overridden from Thyra::ModelEvaulator
template<class Vector, class Matrix, class RNG>
Teuchos::RCP<const ::Thyra::VectorSpaceBase<typename VectorTraits<Vector>::scalar_type> >
MCSAModelEvaluator<Vector,Matrix,RNG>::get_x_space() const
{
  return d_x_space;
}

//---------------------------------------------------------------------------//
// Overridden from Thyra::ModelEvaulator
template<class Vector, class Matrix, class RNG>
Teuchos::RCP<const ::Thyra::VectorSpaceBase<typename VectorTraits<Vector>::scalar_type> >
MCSAModelEvaluator<Vector,Matrix,RNG>::get_f_space() const
{
  return d_f_space;
}

//---------------------------------------------------------------------------//
// Overridden from Thyra::ModelEvaulator
template<class Vector, class Matrix, class RNG>
::Thyra::ModelEvaluatorBase::InArgs<typename VectorTraits<Vector>::scalar_type>
MCSAModelEvaluator<Vector,Matrix,RNG>::getNominalValues() const
{
    MCLS_REQUIRE( Teuchos::nonnull(d_A) );
    MCLS_REQUIRE( Teuchos::nonnull(d_b) );
    d_nominal_values = this->createInArgs();
    Teuchos::RCP<Vector> x0 = VT::clone( *d_b );
    d_nominal_values.set_x( 
	ThyraVectorExtraction<Vector,Matrix>::createThyraVectorFromDomain(
	    x0, *d_A) );
    return d_nominal_values;
}

//---------------------------------------------------------------------------//
// Overridden from Thyra::ModelEvaulator
template<class Vector, class Matrix, class RNG>
::Thyra::ModelEvaluatorBase::InArgs<typename VectorTraits<Vector>::scalar_type>
MCSAModelEvaluator<Vector,Matrix,RNG>::createInArgs() const
{
    ::Thyra::ModelEvaluatorBase::InArgsSetup<Scalar> inArgs;
    inArgs.setModelEvalDescription(this->description());
    inArgs.setSupports(::Thyra::ModelEvaluatorBase::IN_ARG_x);
    return inArgs;
}

//---------------------------------------------------------------------------//
// Overridden from Thyra::ModelEvaulator
template<class Vector, class Matrix, class RNG>
::Thyra::ModelEvaluatorBase::OutArgs<typename VectorTraits<Vector>::scalar_type>
MCSAModelEvaluator<Vector,Matrix,RNG>::createOutArgsImpl() const
{
    ::Thyra::ModelEvaluatorBase::OutArgsSetup<Scalar> outArgs;
    outArgs.setModelEvalDescription(this->description());
    outArgs.setSupports(::Thyra::ModelEvaluatorBase::OUT_ARG_f);
    return outArgs;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Solve the linear problem.
 */
template<class Vector, class Matrix, class RNG>
void MCSAModelEvaluator<Vector,Matrix,RNG>::evalModelImpl(
    const ::Thyra::ModelEvaluatorBase::InArgs<Scalar> &inArgs,
    const ::Thyra::ModelEvaluatorBase::OutArgs<Scalar> &outArgs ) const
{
    MCLS_REQUIRE( Teuchos::nonnull(d_global_comm) );
    MCLS_REQUIRE( Teuchos::nonnull(d_mc_solver) );
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );

    // Get the input argument.
    Teuchos::RCP<const Vector> x = 
	ThyraVectorExtraction<Vector,Matrix>::getVectorFromDomain( 
	    inArgs.get_x(), *d_A );

    // Get the output argument.
    Teuchos::RCP<Vector> f = 
	ThyraVectorExtraction<Vector,Matrix>::getVectorNonConstFromDomain(
	    outArgs.get_f(), *d_A );
    VT::update( *f, 0.0, *x, 1.0 );

    // Get the preconditioned residual.
    Teuchos::RCP<Vector> r = getPrecResidual( f );

    // Do the fixed point iterations.
    for ( int l = 0; l < d_num_smooth; ++l )
    {
	VT::update( *f, 1.0, *r, 1.0 );
	r = getPrecResidual( f );
    }

    // Solve the residual Monte Carlo problem.
    d_mc_problem->setRHS( r );
    VT::putScalar( *d_mc_problem->getLHS(), 0.0 );
    d_mc_solver->solve();

    // Compute the new nonlinear residual.
    VT::update( *f, 1.0, *d_mc_problem->getLHS(), 1.0, *x, -1.0 );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Build the residual Monte Carlo problem.
 */
template<class Vector, class Matrix, class RNG>
void MCSAModelEvaluator<Vector,Matrix,RNG>::buildResidualMonteCarloProblem()
{
    MCLS_REQUIRE( Teuchos::nonnull(d_global_comm) );
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );

    // Generate the residual Monte Carlo problem on the primary set. The
    // preconditioned residual is the source and the transposed composite
    // operator is the domain. We pass the preconditioners and operator
    // separately to defer composite operator construction until the last
    // possible moment.
    Teuchos::RCP<Vector> delta_x = MT::cloneVectorFromMatrixRows( *d_A );
    d_mc_problem = Teuchos::rcp( new LinearProblemType(d_A, delta_x, d_b) );
    d_mc_problem->setLeftPrec( d_M );

    // Create the Monte Carlo direct solver for the residual problem.
    bool use_adjoint = false;
    bool use_forward = false;
    if ( d_plist->get<std::string>("MC Type") == "Adjoint" )
    {
	use_adjoint = true;
    }
    else if ( d_plist->get<std::string>("MC Type") == "Forward" )
    {
	use_forward = true;
    }
    else
    {
	MCLS_INSIST( use_forward || use_adjoint, 
		     "MC Type not supported" );
    }

    if ( use_adjoint )
    {
	d_mc_solver = Teuchos::rcp( 
	    new AdjointSolverManager<Vector,Matrix,RNG>(
		d_mc_problem, d_global_comm, d_plist, true) );
    }
    else if ( use_forward )
    {
	d_mc_solver = Teuchos::rcp( 
	    new ForwardSolverManager<Vector,Matrix,RNG>(
		d_mc_problem, d_global_comm, d_plist, true) );
    }
    else
    {
        MCLS_INSIST( use_forward || use_adjoint, 
                     "MC Type not supported" );
    }

    MCLS_ENSURE( Teuchos::nonnull(d_mc_solver) );
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

#endif // end MCLS_MCSAMODELEVALUATOR_IMPL_HPP

//---------------------------------------------------------------------------//
// end MCLS_MCSAModelEvaluator_impl.hpp
//---------------------------------------------------------------------------//
