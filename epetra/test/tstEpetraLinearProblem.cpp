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
 * \file tstEpetraLinearProblem.cpp
 * \author Stuart R. Slattery
 * \brief MCLS::LinearProbelm tests.
 */
//---------------------------------------------------------------------------//

#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <string>
#include <cassert>

#include <MCLS_LinearProblem.hpp>
#include <MCLS_MatrixTraits.hpp>
#include <MCLS_VectorTraits.hpp>
#include <MCLS_EpetraAdapter.hpp>

#include <Teuchos_UnitTestHarness.hpp>
#include <Teuchos_DefaultComm.hpp>
#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_RCP.hpp>
#include <Teuchos_ArrayRCP.hpp>
#include <Teuchos_Array.hpp>
#include <Teuchos_ArrayView.hpp>
#include <Teuchos_TypeTraits.hpp>

#include <Epetra_Map.h>
#include <Epetra_Vector.h>
#include <Epetra_RowMatrix.h>
#include <Epetra_CrsMatrix.h>
#include <Epetra_Comm.h>
#include <Epetra_SerialComm.h>

#ifdef HAVE_MPI
#include <Epetra_MpiComm.h>
#endif

//---------------------------------------------------------------------------//
// Helper functions.
//---------------------------------------------------------------------------//

Teuchos::RCP<Epetra_Comm> getEpetraComm( 
    const Teuchos::RCP<const Teuchos::Comm<int> >& comm )
{
#ifdef HAVE_MPI
    Teuchos::RCP< const Teuchos::MpiComm<int> > mpi_comm = 
	Teuchos::rcp_dynamic_cast< const Teuchos::MpiComm<int> >( comm );
    Teuchos::RCP< const Teuchos::OpaqueWrapper<MPI_Comm> > opaque_comm = 
	mpi_comm->getRawMpiComm();
    return Teuchos::rcp( new Epetra_MpiComm( (*opaque_comm)() ) );
#else
    return Teuchos::rcp( new Epetra_SerialComm() );
#endif
}

//---------------------------------------------------------------------------//
// Test templates
//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( LinearProblem, Typedefs )
{
    typedef Epetra_RowMatrix MatrixType;
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::LinearProblem<VectorType,MatrixType> LinearProblemType;

    TEST_EQUALITY_CONST( 
	(Teuchos::TypeTraits::is_same<LinearProblemType::vector_type,
				      VectorType>::value) == true, true );

    TEST_EQUALITY_CONST( 
	(Teuchos::TypeTraits::is_same<LinearProblemType::matrix_type,
				      MatrixType>::value) == true, true );

    TEST_EQUALITY_CONST( 
	(Teuchos::TypeTraits::is_same<LinearProblemType::VT,VT>::value) 
	== true, true );

    TEST_EQUALITY_CONST( 
	(Teuchos::TypeTraits::is_same<LinearProblemType::MT,MT>::value) 
	== true, true );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( LinearProblem, Constructor )
{
    typedef Epetra_RowMatrix MatrixType;
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    Teuchos::RCP<Epetra_CrsMatrix> A = 
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );

    Teuchos::Array<int> global_columns( 1 );
    Teuchos::Array<double> values( 1, 1 );
    for ( int i = 0; i < global_num_rows; ++i )
    {
	global_columns[0] = i;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );

    }
    A->FillComplete();

    Teuchos::RCP<VectorType> X = MT::cloneVectorFromMatrixRows( *A );
    Teuchos::RCP<VectorType> B = VT::clone( *X );

    MCLS::LinearProblem<VectorType,MatrixType> linear_problem( A, X, B );
    TEST_EQUALITY( linear_problem.getOperator(), A );

    Teuchos::RCP<VectorType> Y = VT::clone( *X );
    linear_problem.setLHS( Y );

    Teuchos::RCP<VectorType> C = VT::clone( *B );
    linear_problem.setRHS( C );

    TEST_ASSERT( !linear_problem.isLeftPrec() );
    linear_problem.setLeftPrec( A );
    TEST_ASSERT( linear_problem.isLeftPrec() );
    TEST_EQUALITY( linear_problem.getLeftPrec(), A );

    TEST_ASSERT( !linear_problem.isRightPrec() );
    linear_problem.setRightPrec( A );
    TEST_ASSERT( linear_problem.isRightPrec() );
    TEST_EQUALITY( linear_problem.getRightPrec(), A );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( LinearProblem, Apply )
{
    typedef Epetra_RowMatrix MatrixType;
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    Teuchos::RCP<Epetra_CrsMatrix> A = 
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );

    Teuchos::Array<int> global_columns( 1 );
    Teuchos::Array<double> values( 1, 1.0 );
    for ( int i = 0; i < global_num_rows; ++i )
    {
	global_columns[0] = i;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );

    }
    A->FillComplete();

    Teuchos::RCP<VectorType> X = MT::cloneVectorFromMatrixRows( *A );
    double x_val = 2;
    VT::putScalar( *X, x_val );

    Teuchos::RCP<VectorType> B = VT::clone( *X );
    double b_val = 5;
    VT::putScalar( *B, b_val );

    MCLS::LinearProblem<VectorType,MatrixType> linear_problem( A, X, B );

    Teuchos::RCP<VectorType> Y = VT::clone( *X );
    linear_problem.applyOp( *X, *Y );

    Teuchos::ArrayRCP<const double> Y_view = VT::view( *Y );
    Teuchos::ArrayRCP<const double>::const_iterator view_iterator;
    for ( view_iterator = Y_view.begin();
	  view_iterator != Y_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, x_val );
    }

    linear_problem.updateSolution( X );
    linear_problem.apply( *X, *Y );
    for ( view_iterator = Y_view.begin();
	  view_iterator != Y_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, 2*x_val );
    }

    linear_problem.setRightPrec( A );
    linear_problem.applyRightPrec( *X, *Y );
    for ( view_iterator = Y_view.begin();
	  view_iterator != Y_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, 2*x_val );
    }

    linear_problem.apply( *X, *Y );
    for ( view_iterator = Y_view.begin();
	  view_iterator != Y_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, 2*x_val );
    }

    linear_problem.setLeftPrec( A );
    linear_problem.applyLeftPrec( *X, *Y );
    for ( view_iterator = Y_view.begin();
	  view_iterator != Y_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, 2*x_val );
    }

    linear_problem.apply( *X, *Y );
    for ( view_iterator = Y_view.begin();
	  view_iterator != Y_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, 2*x_val );
    }

    linear_problem.updateSolution( X );
    Teuchos::RCP<const MatrixType> composite = 
	linear_problem.getCompositeOperator();
    MT::apply( *composite, *X, *Y );
    for ( view_iterator = Y_view.begin();
	  view_iterator != Y_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, 4*x_val );
    }
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( LinearProblem, ResidualUpdate )
{
    typedef Epetra_RowMatrix MatrixType;
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    Teuchos::RCP<Epetra_CrsMatrix> A = 
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );

    Teuchos::Array<int> global_columns( 1 );
    Teuchos::Array<double> values( 1, 1 );
    for ( int i = 0; i < global_num_rows; ++i )
    {
	global_columns[0] = i;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );

    }
    A->FillComplete();

    Teuchos::RCP<VectorType> X = MT::cloneVectorFromMatrixRows( *A );
    double x_val = 2;
    VT::putScalar( *X, x_val );

    Teuchos::RCP<VectorType> B = VT::clone( *X );
    double b_val = 5;
    VT::putScalar( *B, b_val );

    MCLS::LinearProblem<VectorType,MatrixType> linear_problem( A, X, B );
    linear_problem.updateResidual();

    Teuchos::RCP<const VectorType> R = linear_problem.getResidual();
    Teuchos::ArrayRCP<const double> R_view = VT::view( *R );
    Teuchos::ArrayRCP<const double>::const_iterator view_iterator;
    for ( view_iterator = R_view.begin();
	  view_iterator != R_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, b_val - x_val );
    }

    Teuchos::RCP<const VectorType> RP = linear_problem.getPrecResidual();
    Teuchos::ArrayRCP<const double> RP_view = VT::view( *RP );
    Teuchos::ArrayRCP<const double>::const_iterator pview_iterator;
    for ( pview_iterator = RP_view.begin();
	  pview_iterator != RP_view.end();
	  ++pview_iterator )
    {
	TEST_EQUALITY( *pview_iterator, 0.0 );
    }

    linear_problem.setLeftPrec( A );
    linear_problem.updateResidual();
    for ( view_iterator = R_view.begin();
	  view_iterator != R_view.end();
	  ++view_iterator )
    {
	TEST_EQUALITY( *view_iterator, b_val - x_val );
    }

    linear_problem.updatePrecResidual();
    for ( pview_iterator = RP_view.begin();
	  pview_iterator != RP_view.end();
	  ++pview_iterator )
    {
	TEST_EQUALITY( *pview_iterator, b_val - x_val );
    }
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( LinearProblem, composite_operator )
{
    typedef Epetra_RowMatrix MatrixType;
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    Teuchos::RCP<Epetra_CrsMatrix> A = 
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );

    Teuchos::Array<int> global_columns( 2 );
    Teuchos::Array<double> values( 2 );
    for ( int i = 0; i < global_num_rows-1; ++i )
    {
	global_columns[0] = i;
	global_columns[1] = i+1;
	values[0] = 1;
	values[1] = 2;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );

    }
    A->FillComplete();

    Teuchos::RCP<MatrixType> B = A;
    Teuchos::RCP<MatrixType> C = MT::copyTranspose( *B );

    Teuchos::RCP<VectorType> X = MT::cloneVectorFromMatrixRows( *A );
    double x_val = 2;
    VT::putScalar( *X, x_val );

    MCLS::LinearProblem<VectorType,MatrixType> linear_problem( B, X, X );
    linear_problem.setLeftPrec( C );
    Teuchos::RCP<const MatrixType> D = linear_problem.getCompositeOperator();

    std::size_t num_entries;
    Teuchos::Array<int> view_columns(3);
    Teuchos::Array<double> view_values(3);
    for ( int i = 1; i < local_num_rows-1; ++i )
    {
	MT::getLocalRowCopy( *D, i, view_columns, view_values, num_entries );
	TEST_EQUALITY( num_entries, 3 );
	TEST_EQUALITY( view_columns[0], i-1 );
	TEST_EQUALITY( view_columns[1], i );
	TEST_EQUALITY( view_columns[2], i+1 );
	TEST_EQUALITY( view_values[0], 2 );
	TEST_EQUALITY( view_values[1], 5 );
	TEST_EQUALITY( view_values[2], 2 );
    }
}

//---------------------------------------------------------------------------//
// end tstEpetraLinearProblem.cpp
//---------------------------------------------------------------------------//

