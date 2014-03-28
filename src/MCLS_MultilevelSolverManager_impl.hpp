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
 * \file MCLS_MultilevelSolverManager_impl.hpp
 * \author Stuart R. Slattery
 * \brief Multilevel Monte Carlo solver manager implementation.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_MULTILEVELSOLVERMANAGER_IMPL_HPP
#define MCLS_MULTILEVELSOLVERMANAGER_IMPL_HPP

#include "MCLS_DBC.hpp"

#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_Ptr.hpp>

#include <MLAPI_Space.h>
#include <MLAPI_Operator.h>

namespace MCLS
{

//---------------------------------------------------------------------------//
/*!
 * \brief Comm constructor. setProblem() and setParameters() must be called
 * before solve(). 
 */
template<class Vector, class Matrix>
MultilevelSolverManager<Vector,Matrix>::MultilevelSolverManager( 
    const Teuchos::RCP<const Comm>& global_comm,
    const Teuchos::RCP<Teuchos::ParameterList>& plist,
    bool internal_solver )
    : d_global_comm( global_comm )
    , d_plist( plist )
    , d_internal_solver( internal_solver )
{
    MCLS_REQUIRE( !d_global_comm.is_null() );
    MCLS_REQUIRE( !d_plist.is_null() );
    d_mc_solver = Teuchos::rcp(
	new AdjointSolverManager<Vector,Matrix>( 
	    d_global_comm, d_plist, true) );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Constructor.
 */
template<class Vector, class Matrix>
MultilevelSolverManager<Vector,Matrix>::MultilevelSolverManager( 
    const Teuchos::RCP<LinearProblemType>& problem,
    const Teuchos::RCP<const Comm>& global_comm,
    const Teuchos::RCP<Teuchos::ParameterList>& plist,
    bool internal_solver )
    : d_problem( problem )
    , d_global_comm( global_comm )
    , d_plist( plist )
    , d_internal_solver( internal_solver )
    , d_primary_set( !d_problem.is_null() )
{
    MCLS_REQUIRE( !d_global_comm.is_null() );
    MCLS_REQUIRE( !d_plist.is_null() );
    d_mc_solver = Teuchos::rcp(
	new AdjointSolverManager<Vector,Matrix>( 
	    d_global_comm, d_plist, true) );

    buildOperatorHierarchy();
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the valid parameters for this manager.
 */
template<class Vector, class Matrix>
Teuchos::RCP<const Teuchos::ParameterList> 
MultilevelSolverManager<Vector,Matrix>::getValidParameters() const
{
    Teuchos::RCP<Teuchos::ParameterList> plist = Teuchos::parameterList();
    return plist;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the tolerance achieved on the last linear solve. This may be
 * less or more than the set convergence tolerance.
 */
template<class Vector, class Matrix>
typename Teuchos::ScalarTraits<
    typename MultilevelSolverManager<Vector,Matrix>::Scalar>::magnitudeType 
MultilevelSolverManager<Vector,Matrix>::achievedTol() const
{
    // Here we'll simply return the source weighted norm of the residual after
    // solution. This will give us a measure of the stochastic error generated
    // by the Monte Carlo solve. 
    typename Teuchos::ScalarTraits<Scalar>::magnitudeType residual_norm = 
	Teuchos::ScalarTraits<Scalar>::zero();

    // We only do this on the primary set where the linear problem exists.
    if ( d_primary_set )
    {
	d_problem->updateResidual();
	residual_norm = VT::normInf( *d_problem->getResidual() );
	residual_norm /= VT::normInf( *d_problem->getRHS() );
    }
    d_global_comm->barrier();

    return residual_norm;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the number of iterations from the last linear solve. This is a
 * direct solver and therefore does not do any iterations.
 */
template<class Vector, class Matrix>
int MultilevelSolverManager<Vector,Matrix>::getNumIters() const
{
    return 0;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Set the linear problem with the manager.
 */
template<class Vector, class Matrix>
void MultilevelSolverManager<Vector,Matrix>::setProblem( 
    const Teuchos::RCP<LinearProblem<Vector,Matrix> >& problem )
{
    MCLS_REQUIRE( !d_global_comm.is_null() );
    MCLS_REQUIRE( !d_plist.is_null() );

    d_primary_set = Teuchos::nonnull( problem );

    // Determine if the linear operator has changed. It is presumed the
    // preconditioners are bound to the linear operator and will therefore
    // change when the operator change. The mechanism here for determining if
    // the operator has changed is checking if the memory address is the
    // same. This may not be the best way to check.
    int update_operator = 1;
    if ( d_primary_set )
    {
        if ( Teuchos::nonnull(d_problem) )
        {
            if ( d_problem->getOperator().getRawPtr() == 
                 problem->getOperator().getRawPtr() )
            {
                update_operator = 0;
            }
        }
    }

    // Set the problem.
    d_problem = problem;

    // Update the parallel domain with this operator if it has changed.
    if ( update_operator )
    {
        buildOperatorHierarchy();
    }
}

//---------------------------------------------------------------------------//
/*! 
 * \brief Set the parameters for the manager. The manager will modify this
    list with default parameters that are not defined. 
*/
template<class Vector, class Matrix>
void MultilevelSolverManager<Vector,Matrix>::setParameters( 
    const Teuchos::RCP<Teuchos::ParameterList>& params )
{
    MCLS_REQUIRE( !params.is_null() );
    d_plist = params;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Solve the linear problem. Return true if the solution
 * converged. False if it did not.
 */
template<class Vector, class Matrix>
bool MultilevelSolverManager<Vector,Matrix>::solve()
{    
    MCLS_REQUIRE( !d_global_comm.is_null() );
    MCLS_REQUIRE( !d_plist.is_null() );

    // Build the RHS hierarchy. We assume the RHS of the linear system changes
    // with each solve.
    buildRHSHierarchy();

    // Get the number of histories to run at the coarsest level.
    int nh = d_plist->get<int>("Set Number of Histories");
    int nh_l = 0;

    // Solve the linear problem at each of the levels.
    Teuchos::RCP<Teuchos::ParameterList> mc_plist =
	Teuchos::rcp( new Teuchos::ParameterList(*d_plist) );
    Teuchos::RCP<LinearProblem<Vector,Matrix> > level_problem;
    for ( int l = 0; l < d_num_levels; ++l )
    {
	if ( d_primary_set )
	{
	    // Create the level problem.
	    level_problem = Teuchos::rcp( 
		new LinearProblem<Vector,Matrix>(
		    d_mlapi->A(l).GetRCPRowMatrix(), d_x[l], d_b[l]) );
	}
	// Compute the number of histories at this level.
	nh_l = nh * std::pow( 2.0, -3.0*(d_num_levels-l-1)/2.0 );
	mc_plist->set<int>("Set Number of Histories", nh_l);

	// Solve the Monte Carlo problem on this level.
	d_mc_solver->setParameters( mc_plist );
	d_mc_solver->setProblem( level_problem );
	d_mc_solver->solve();
    }

    // Collapse the tallies to the fine grid.
    Teuchos::RCP<Matrix> P_l;
    Teuchos::RCP<Vector> work;
    for ( int l = d_num_levels - 1; l > 0; --l )
    {
	if ( d_primary_set )
	{
	    // Create a work vector for this level.
	    work = VT::clone( *d_x[l-1] );

	    // Get the prolongation operator for this level.
	    P_l = d_mlapi->P(l).GetRCPRowMatrix();

	    // Apply the prolongation operator to the level tally.
	    MT::apply( *P_l, *d_x[l], *work );

	    // Add the coarse level to the fine level.
	    VT::update( *d_x[l-1], 1.0, *work, 1.0 );
	}
    }

    // This is a direct solve and therefore always converged in the iterative
    // sense. 
    return true;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Build the multigrid hierarchy.
 */
template<class Vector, class Matrix>
void MultilevelSolverManager<Vector,Matrix>::buildOperatorHierarchy()
{
    MCLS_REQUIRE( !d_global_comm.is_null() );
    MCLS_REQUIRE( !d_plist.is_null() );

    if ( d_primary_set )
    {
	// Build ML data structures.
	MLAPI::Space domain_space( 
	    d_problem->getOperator()->OperatorDomainMap() );
	MLAPI::Space range_space( 
	    d_problem->getOperator()->OperatorRangeMap() );
	Teuchos::RCP<Matrix> A = 
	    Teuchos::rcp_const_cast<const Matrix>(d_problem->getOperator());
	Matrix* A_ptr = A->getRawPtr();
	MLAPI::Operator ml_operator( domain_space, range_space, A_ptr, false );

	// Create the smoothed aggregation operator hierarchy.
	d_mlapi = 
	    Teuchos::rcp( new MLAPI::MultiLevelSA(ml_operator, *d_plist) );
	d_num_levels = d_mlapi->GetMaxLevels();
	MCLS_CHECK( d_mlapi->IsComputed() );

	// Allocate the LHS and RHS vector hierarchy.
	d_x.resize( d_num_levels );
	d_x[0] = d_problem->getLHS();
	d_b.resize( d_num_levels );
	d_b[0] = d_problem->getRHS();
	
	// Build the hierarchy.
	Teuchos::RCP<Matrix> R_l;
	for ( int l = 1; l < d_num_levels; ++l )
	{
	    // Get the restriction operator for this level.
	    R_l = d_mlapi->R(l-1).GetRCPRowMatrix();

	    // Create the LHS for this level.
	    d_x[l] = Teuchos::rcp( new Vector(R_l->OperatorRangeMap()) );
	    
	    // Create the RHS for this level.
	    d_b[l] = Teuchos::rcp( new Vector(R_l->OperatorRangeMap()) );
	}
    }
}

//---------------------------------------------------------------------------//
/*!
 * \brief Build the multigrid RHS hierarchy.
 */
template<class Vector, class Matrix>
void MultilevelSolverManager<Vector,Matrix>::buildRHSHierarchy()
{
    MCLS_REQUIRE( !d_global_comm.is_null() );
    MCLS_REQUIRE( !d_plist.is_null() );

    // Apply the restriction operator to successive levels.
    Teuchos::RCP<Matrix> R_l;
    for ( int l = 1; l < d_num_levels; ++l )
    {
	R_l = d_mlapi->R(l-1).GetRCPRowMatrix();
	MT::apply( *R_l, *d_b[l-1], *d_b[l] );
    }
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

//---------------------------------------------------------------------------//

#endif // end MCLS_MULTILEVELSOLVERMANAGER_IMPL_HPP

//---------------------------------------------------------------------------//
// end MCLS_MultilevelSolverManager_impl.hpp
//---------------------------------------------------------------------------//
