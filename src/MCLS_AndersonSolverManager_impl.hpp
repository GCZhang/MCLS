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
 * \file MCLS_AndersonSolverManager_impl.hpp
 * \author Stuart R. Slattery
 * \brief Anderson Acceleration solver manager implementation.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_ANDERSONSOLVERMANAGER_IMPL_HPP
#define MCLS_ANDERSONSOLVERMANAGER_IMPL_HPP

#include <string>
#include <iostream>
#include <iomanip>

#include "MCLS_DBC.hpp"
#include "MCLS_ThyraVectorExtraction.hpp"
#include "MCLS_MCSAStatusTest.hpp"

#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_Ptr.hpp>
#include <Teuchos_TimeMonitor.hpp>

#include <Thyra_NonlinearSolver_NOX.hpp>

namespace MCLS
{
//---------------------------------------------------------------------------//
/*!
 * \brief Parameter constructor. setProblem() must be called before solve().
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::AndersonSolverManager( 
    const Teuchos::RCP<Teuchos::ParameterList>& plist )
    : d_plist( plist )
#if HAVE_MCLS_TIMERS
    , d_solve_timer( Teuchos::TimeMonitor::getNewCounter("MCLS: Anderson Solve") )
#endif
{
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );
    d_plist->set( "Nonlinear Solver", "Anderson Accelerated Fixed-Point" );
    d_model_evaluator = Teuchos::rcp( 
	new MCSAModelEvaluator<Vector,Matrix,MonteCarloTag,RNG>(d_plist) );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Constructor.
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::AndersonSolverManager( 
    const Teuchos::RCP<MultiSetLinearProblem<Vector,Matrix> >& multiset_problem,
    const Teuchos::RCP<Teuchos::ParameterList>& plist )
    : d_multiset_problem( multiset_problem )
    , d_problem( multiset_problem->getProblem() )
    , d_plist( plist )
#if HAVE_MCLS_TIMERS
    , d_solve_timer( Teuchos::TimeMonitor::getNewCounter("MCLS: Anderson Solve") )
#endif
{
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );
    d_plist->set( "Nonlinear Solver", "Anderson Accelerated Fixed-Point" );

    // Create the model evaluator.
    d_model_evaluator = Teuchos::rcp( 
	new MCSAModelEvaluator<Vector,Matrix,MonteCarloTag,RNG>(
	    d_plist,
	    d_multiset_problem,
	    d_problem->getOperator(),
	    d_problem->getRHS(),
	    d_problem->getLeftPrec()) );

    // Create the nonlinear solver.
    createNonlinearSolver();
    MCLS_ENSURE( Teuchos::nonnull(d_nox_solver) );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the valid parameters for this manager.
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
Teuchos::RCP<const Teuchos::ParameterList> 
AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::getValidParameters() const
{
    // Create a parameter list with the Monte Carlo solver parameters as a
    // starting point.
    Teuchos::RCP<Teuchos::ParameterList> plist = Teuchos::parameterList();
    return plist;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the tolerance achieved on the last linear solve. This may be
 * less or more than the set convergence tolerance. 
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
typename Teuchos::ScalarTraits<
    typename AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::Scalar>::magnitudeType 
AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::achievedTol() const
{
    return 0.0;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Set the multiset linear problem with the manager.
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
void AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::setMultiSetProblem( 
    const Teuchos::RCP<MultiSetLinearProblem<Vector,Matrix> >& multiset_problem )
{
    d_multiset_problem = multiset_problem;
    setProblem( d_multiset_problem->getProblem() );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Set the linear problem with the manager.
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
void AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::setProblem( 
    const Teuchos::RCP<LinearProblemType>& problem )
{
    MCLS_REQUIRE( Teuchos::nonnull(d_plist) );
    MCLS_REQUIRE( Teuchos::nonnull(d_model_evaluator) );

    d_problem = problem;
    d_model_evaluator->setProblem( d_multiset_problem,
				   d_problem->getOperator(),
				   d_problem->getRHS(),
				   d_problem->getLeftPrec() );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Set the parameters for the manager. The manager will modify this
 * list with default parameters that are not defined.
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
void AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::setParameters( 
    const Teuchos::RCP<Teuchos::ParameterList>& params )
{
    MCLS_REQUIRE( Teuchos::nonnull(params) );

    // Set the parameters.
    d_plist = params;
    d_model_evaluator->setParameters( params );
    d_plist->set( "Nonlinear Solver", "Anderson Accelerated Fixed-Point" );

    // Create the nonlinear solver.
    createNonlinearSolver();
    MCLS_ENSURE( Teuchos::nonnull(d_nox_solver) );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Solve the linear problem. Return true if the solution
 * converged. False if it did not.
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
bool AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::solve()
{
#if HAVE_MCLS_TIMERS
    // Start the solve timer.
    Teuchos::TimeMonitor solve_monitor( *d_solve_timer );
#endif

    // Create a Thyra vector from our initial guess.
    Teuchos::RCP< ::Thyra::VectorBase<double> > x0 = 
	ThyraVectorExtraction<Vector,Matrix>::createThyraVectorFromDomain( 
	    d_problem->getLHS(), *d_problem->getOperator() );
    NOX::Thyra::Vector nox_x0( x0 );
    d_nox_solver->reset( nox_x0 );

    // Solve the problem.
    NOX::StatusTest::StatusType solve_status = d_nox_solver->solve();

    // Extract the solution.
    Teuchos::RCP<const NOX::Abstract::Vector> x = 
	d_nox_solver->getSolutionGroup().getXPtr();
    Teuchos::RCP<const NOX::Thyra::Vector> nox_thyra_x =
	Teuchos::rcp_dynamic_cast<const NOX::Thyra::Vector>(x,true);
    Teuchos::RCP< ::Thyra::VectorBase<double> > thyra_x =
	Teuchos::rcp_const_cast< ::Thyra::VectorBase<double> >(
	    nox_thyra_x->getThyraRCPVector());
    Teuchos::RCP<Vector> x_vector = 
	ThyraVectorExtraction<Vector,Matrix>::getVectorNonConstFromDomain( 
	    thyra_x, *d_problem->getOperator() );
    VT::update( *d_problem->getLHS(), 0.0, *x_vector, 1.0 );

    // Return the status of the solve.
    return solve_status;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Create the nonlinear solver.
 */
template<class Vector, class Matrix, class MonteCarloTag, class RNG>
void AndersonSolverManager<Vector,Matrix,MonteCarloTag,RNG>::createNonlinearSolver()
{
    // Create the solve criteria.
    typename Teuchos::ScalarTraits<double>::magnitudeType tolerance = 1.0e-8;
    if ( d_plist->isParameter("Convergence Tolerance") )
    {
	tolerance = d_plist->get<double>("Convergence Tolerance");
    }
    Teuchos::RCP<MCSAStatusTest<Vector,Matrix> > tol_test= Teuchos::rcp(
	new MCSAStatusTest<Vector,Matrix>(tolerance) );
    int max_num_iters = 1000;
    if ( d_plist->isParameter("Maximum Iterations") )
    {
	max_num_iters = d_plist->get<int>("Maximum Iterations");
    }
    Teuchos::RCP<NOX::StatusTest::MaxIters> max_iter_test =
	Teuchos::rcp( new NOX::StatusTest::MaxIters(max_num_iters) );
    Teuchos::RCP<NOX::StatusTest::FiniteValue> finite_test =
	Teuchos::rcp(new NOX::StatusTest::FiniteValue);
    Teuchos::RCP<NOX::StatusTest::Combo> status_test =
	Teuchos::rcp( new NOX::StatusTest::Combo(NOX::StatusTest::Combo::OR) );
    status_test->addStatusTest( tol_test );
    status_test->addStatusTest( max_iter_test );
    status_test->addStatusTest( finite_test );

    // Create the NOX group.
    MCLS_CHECK( Teuchos::nonnull(d_problem) );
    MCLS_CHECK( Teuchos::nonnull(d_model_evaluator) );
    Teuchos::RCP< ::Thyra::VectorBase<double> > x0 = 
	ThyraVectorExtraction<Vector,Matrix>::createThyraVectorFromDomain( 
	    d_problem->getLHS(), *d_problem->getOperator() );
    MCLS_CHECK( Teuchos::nonnull(x0) );
    NOX::Thyra::Vector nox_x0( x0 );
    Teuchos::RCP<NOX::Abstract::Group> nox_group = Teuchos::rcp(
	new NOX::Thyra::Group(nox_x0, d_model_evaluator,
			      Teuchos::null,Teuchos::null,Teuchos::null,
			      Teuchos::null,Teuchos::null) );
    // Create the NOX solver.
    NOX::Solver::Factory nox_factory;
    d_nox_solver = nox_factory.buildSolver( nox_group, status_test, d_plist );
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

#endif // end MCLS_ANDERSONSOLVERMANAGER_IMPL_HPP

//---------------------------------------------------------------------------//
// end MCLS_AndersonSolverManager_impl.hpp
//---------------------------------------------------------------------------//

