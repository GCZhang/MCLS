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
 * \file MCLS_MCSASolverManager.hpp
 * \author Stuart R. Slattery
 * \brief Monte Carlo Synthetic Acceleration solver manager declaration.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_MCSASOLVERMANAGER_HPP
#define MCLS_MCSASOLVERMANAGER_HPP

#include <random>

#include "MCLS_SolverManager.hpp"
#include "MCLS_FixedPointIteration.hpp"
#include "MCLS_LinearProblem.hpp"
#include "MCLS_VectorTraits.hpp"

#include <Teuchos_RCP.hpp>
#include <Teuchos_ParameterList.hpp>
#include <Teuchos_ScalarTraits.hpp>
#include <Teuchos_as.hpp>
#include <Teuchos_Comm.hpp>

namespace MCLS
{

//---------------------------------------------------------------------------//
/*!
 * \class MCSASolverManager
 * \brief Solver manager for Monte Carlo synthetic acceleration.
 */
template<class Vector, class Matrix, class RNG = std::mt19937_64>
class MCSASolverManager : public SolverManager<Vector,Matrix>
{
  public:

    //@{
    //! Typedefs.
    typedef SolverManager<Vector,Matrix>            Base;
    typedef Vector                                  vector_type;
    typedef VectorTraits<Vector>                    VT;
    typedef typename VT::scalar_type                Scalar;
    typedef Matrix                                  matrix_type;
    typedef LinearProblem<Vector,Matrix>            LinearProblemType;
    typedef FixedPointIteration<Vector,Matrix>      FixedPointType;
    typedef Teuchos::Comm<int>                      Comm;
    //@}

    // Comm constructor. setProblem() must be called before solve().
    MCSASolverManager( const Teuchos::RCP<const Comm>& global_comm,
		       const Teuchos::RCP<Teuchos::ParameterList>& plist );

    // Constructor.
    MCSASolverManager( const Teuchos::RCP<LinearProblemType>& problem,
		       const Teuchos::RCP<const Comm>& global_comm,
		       const Teuchos::RCP<Teuchos::ParameterList>& plist );

    //! Destructor.
    ~MCSASolverManager() { /* ... */ }

    //! Get the linear problem being solved by the manager.
    const LinearProblem<Vector,Matrix>& getProblem() const
    { return *d_problem; }

    // Get the valid parameters for this manager.
    Teuchos::RCP<const Teuchos::ParameterList> getValidParameters() const;

    //! Get the current parameters being used for this manager.
    Teuchos::RCP<const Teuchos::ParameterList> getCurrentParameters() const
    { return d_plist; }

    // Get the tolerance achieved on the last linear solve. This may be less
    // or more than the set convergence tolerance.
    typename Teuchos::ScalarTraits<Scalar>::magnitudeType achievedTol() const;

    // Get the number of iterations from the last linear solve.
    int getNumIters() const { return d_num_iters; };

    // Set the linear problem with the manager.
    void setProblem( 
	const Teuchos::RCP<LinearProblem<Vector,Matrix> >& problem );

    // Set the parameters for the manager. The manager will modify this list
    // with default parameters that are not defined.
    void setParameters( const Teuchos::RCP<Teuchos::ParameterList>& params );

    // Solve the linear problem. Return true if the solution converged. False
    // if it did not.
    bool solve();

    //! Return if the last linear solve converged. 
    bool getConvergedStatus() const 
    { return Teuchos::as<bool>(d_converged_status); }

  private:

    // Build the residual Monte Carlo problem from the input problem.
    void buildResidualMonteCarloProblem();

    // Print top banner for the iteration.
    void printTopBanner();

    // Print bottom banner for the iteration.
    void printBottomBanner();

  private:

    // Linear problem
    Teuchos::RCP<LinearProblemType> d_problem;

    // Residual linear problem
    Teuchos::RCP<LinearProblemType> d_residual_problem;

    // Global communicator.
    Teuchos::RCP<const Comm> d_global_comm;

    // Block communicator.
    Teuchos::RCP<const Comm> d_block_comm;

    // Parameters.
    Teuchos::RCP<Teuchos::ParameterList> d_plist;

    // Primary set indicator.
    bool d_primary_set;

    // Monte Carlo solver manager.
    Teuchos::RCP<Base> d_mc_solver;

    // Fixed point iteration.
    Teuchos::RCP<FixedPointType> d_fixed_point;

    // Number of iterations from last solve.
    int d_num_iters;

    // Converged status. True if last solve converged.
    int d_converged_status;    
};

//---------------------------------------------------------------------------//

} // end namespace MCLS

//---------------------------------------------------------------------------//
// Template includes.
//---------------------------------------------------------------------------//

#include "MCLS_MCSASolverManager_impl.hpp"

//---------------------------------------------------------------------------//

#endif // end MCLS_MCSASOLVERMANAGER_HPP

//---------------------------------------------------------------------------//
// end MCLS_MCSASolverManager.hpp
//---------------------------------------------------------------------------//

