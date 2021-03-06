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
 * \file tstEpetraUniformForwardSource.cpp
 * \author Stuart R. Slattery
 * \brief Epetra ForwardDomain tests.
 */
//---------------------------------------------------------------------------//

#include <stack>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <string>
#include <cassert>
#include <random>

#include <MCLS_UniformForwardSource.hpp>
#include <MCLS_ForwardDomain.hpp>
#include <MCLS_VectorTraits.hpp>
#include <MCLS_EpetraAdapter.hpp>
#include <MCLS_ForwardHistory.hpp>
#include <MCLS_ForwardTally.hpp>
#include <MCLS_Events.hpp>
#include <MCLS_PRNG.hpp>

#include <Teuchos_UnitTestHarness.hpp>
#include <Teuchos_DefaultComm.hpp>
#include <Teuchos_CommHelpers.hpp>
#include <Teuchos_RCP.hpp>
#include <Teuchos_ArrayRCP.hpp>
#include <Teuchos_Array.hpp>
#include <Teuchos_ArrayView.hpp>
#include <Teuchos_TypeTraits.hpp>
#include <Teuchos_ParameterList.hpp>

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
TEUCHOS_UNIT_TEST( UniformForwardSource, Typedefs )
{
    typedef Epetra_Vector VectorType;
    typedef Epetra_RowMatrix MatrixType;
    typedef std::mt19937 rng_type;
    typedef MCLS::ForwardDomain<VectorType,MatrixType,rng_type> DomainType;
    typedef MCLS::ForwardHistory<int> HistoryType;

    typedef MCLS::UniformForwardSource<DomainType> SourceType;
    typedef SourceType::HistoryType history_type;
    typedef SourceType::VectorType vector_type;

    TEST_EQUALITY_CONST( 
	(Teuchos::TypeTraits::is_same<HistoryType, history_type>::value)
	== true, true );
    TEST_EQUALITY_CONST( 
	(Teuchos::TypeTraits::is_same<VectorType, vector_type>::value)
	== true, true );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( UniformForwardSource, nh_not_set )
{
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef Epetra_RowMatrix MatrixType;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::ForwardHistory<int> HistoryType;
    typedef std::mt19937 rng_type;
    typedef MCLS::ForwardDomain<VectorType,MatrixType,rng_type> DomainType;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    // Build the linear system.
    Teuchos::RCP<Epetra_CrsMatrix> A = 	
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );
    Teuchos::Array<int> global_columns( 1 );
    Teuchos::Array<double> values( 1 );
    for ( int i = 1; i < global_num_rows; ++i )
    {
	global_columns[0] = i-1;
	values[0] = -0.5;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );
    }
    global_columns[0] = global_num_rows-1;
    values[0] = -0.5;
    A->InsertGlobalValues( global_num_rows-1, global_columns().size(),
			   &values[0], &global_columns[0] );
    A->FillComplete();

    Teuchos::RCP<MatrixType> A_T = MT::copyTranspose(*A);
    Teuchos::RCP<VectorType> x = MT::cloneVectorFromMatrixRows( *A );
    Teuchos::RCP<VectorType> b = MT::cloneVectorFromMatrixRows( *A );
    VT::putScalar( *b, -1.0 );

    // Build the forward domain.
    Teuchos::ParameterList plist;
    plist.set<int>( "Overlap Size", 0 );
    Teuchos::RCP<DomainType> domain = Teuchos::rcp( new DomainType( A_T, x, plist ) );

    // History setup.
    HistoryType::setByteSize();

    // Create the forward source with default values.
    double cutoff = 1.0e-8;
    plist.set<double>("Weight Cutoff", cutoff );
    MCLS::UniformForwardSource<DomainType> 
	source( b, domain, comm, comm->getSize(), comm->getRank(), plist );
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numToTransport(), 0 );
    TEST_EQUALITY( source.numToTransportInSet(), global_num_rows );
    TEST_EQUALITY( source.numRequested(), global_num_rows );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), 0 );

    // Build the source.
    source.buildSource();
    TEST_ASSERT( !source.empty() );
    TEST_EQUALITY( source.numToTransport(), local_num_rows );
    TEST_EQUALITY( source.numToTransportInSet(), global_num_rows );
    TEST_EQUALITY( source.numRequested(), global_num_rows );
    TEST_EQUALITY( source.numLeft(), local_num_rows );
    TEST_EQUALITY( source.numEmitted(), 0 );
    TEST_EQUALITY( source.sourceWeight(), 1.0 );

    // Sample the source.
    Teuchos::RCP<MCLS::PRNG<rng_type> > rng = Teuchos::rcp(
	new MCLS::PRNG<rng_type>(comm->getRank()) );
    source.setRNG( rng );
    for ( int i = 0; i < local_num_rows; ++i )
    {
	TEST_ASSERT( !source.empty() );
	TEST_EQUALITY( source.numLeft(), local_num_rows-i );
	TEST_EQUALITY( source.numEmitted(), i );

	Teuchos::RCP<HistoryType> history = source.getHistory();

	TEST_EQUALITY( history->weight(), 1.0 );
	TEST_ASSERT( domain->isGlobalState( history->globalState() ) );
	TEST_ASSERT( history->alive() );
	TEST_ASSERT( VT::isGlobalRow( *x, history->globalState() ) );
    }
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), local_num_rows );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( UniformForwardSource, PackUnpack )
{
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef Epetra_RowMatrix MatrixType;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::ForwardHistory<int> HistoryType;
    typedef std::mt19937 rng_type;
    typedef MCLS::ForwardDomain<VectorType,MatrixType,rng_type> DomainType;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    // Build the linear system.
    Teuchos::RCP<Epetra_CrsMatrix> A = 	
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );
    Teuchos::Array<int> global_columns( 1 );
    Teuchos::Array<double> values( 1 );
    for ( int i = 1; i < global_num_rows; ++i )
    {
	global_columns[0] = i-1;
	values[0] = -0.5;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );
    }
    global_columns[0] = global_num_rows-1;
    values[0] = -0.5;
    A->InsertGlobalValues( global_num_rows-1, global_columns().size(),
			   &values[0], &global_columns[0] );
    A->FillComplete();

    Teuchos::RCP<MatrixType> A_T = MT::copyTranspose(*A);
    Teuchos::RCP<VectorType> x = MT::cloneVectorFromMatrixRows( *A );
    Teuchos::RCP<VectorType> b = MT::cloneVectorFromMatrixRows( *A );
    VT::putScalar( *b, -1.0 );

    // Build the forward domain.
    Teuchos::ParameterList plist;
    plist.set<int>( "Overlap Size", 0 );
    Teuchos::RCP<DomainType> domain = Teuchos::rcp( new DomainType( A_T, x, plist ) );

    // History setup.
    HistoryType::setByteSize();

    // Create the forward source with default values.
    double cutoff = 1.0e-8;
    plist.set<double>("Weight Cutoff", cutoff );
    MCLS::UniformForwardSource<DomainType> 
	primary_source( b, domain, comm, comm->getSize(), 
			comm->getRank(), plist );

    // Pack and unpack the source.
    Teuchos::Array<char> source_buffer = primary_source.pack();
    MCLS::UniformForwardSource<DomainType> 
	source( source_buffer, domain, 
		comm, comm->getSize(), comm->getRank() );

    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numToTransport(), 0 );
    TEST_EQUALITY( source.numToTransportInSet(), global_num_rows );
    TEST_EQUALITY( source.numRequested(), global_num_rows );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), 0 );

    // Build the source.
    source.buildSource();
    TEST_ASSERT( !source.empty() );
    TEST_EQUALITY( source.numToTransport(), local_num_rows );
    TEST_EQUALITY( source.numToTransportInSet(), global_num_rows );
    TEST_EQUALITY( source.numRequested(), global_num_rows );
    TEST_EQUALITY( source.numLeft(), local_num_rows );
    TEST_EQUALITY( source.numEmitted(), 0 );

    // Sample the source.
    Teuchos::RCP<MCLS::PRNG<rng_type> > rng = Teuchos::rcp(
	new MCLS::PRNG<rng_type>(comm->getRank()) );
    source.setRNG( rng );
    for ( int i = 0; i < local_num_rows; ++i )
    {
	TEST_ASSERT( !source.empty() );
	TEST_EQUALITY( source.numLeft(), local_num_rows-i );
	TEST_EQUALITY( source.numEmitted(), i );

	Teuchos::RCP<HistoryType> history = source.getHistory();

	TEST_EQUALITY( history->weight(), 1.0 );
	TEST_ASSERT( domain->isGlobalState( history->globalState() ) );
	TEST_ASSERT( history->alive() );
	TEST_ASSERT( VT::isGlobalRow( *x, history->globalState() ) );
    }
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), local_num_rows );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( UniformForwardSource, nh_set_pu )
{
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef Epetra_RowMatrix MatrixType;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::ForwardHistory<int> HistoryType;
    typedef std::mt19937 rng_type;
    typedef MCLS::ForwardDomain<VectorType,MatrixType,rng_type> DomainType;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    // Build the linear system.
    Teuchos::RCP<Epetra_CrsMatrix> A = 	
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );
    Teuchos::Array<int> global_columns( 1 );
    Teuchos::Array<double> values( 1 );
    for ( int i = 1; i < global_num_rows; ++i )
    {
	global_columns[0] = i-1;
	values[0] = -0.5;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );
    }
    global_columns[0] = global_num_rows-1;
    values[0] = -0.5;
    A->InsertGlobalValues( global_num_rows-1, global_columns().size(),
			   &values[0], &global_columns[0] );
    A->FillComplete();

    Teuchos::RCP<MatrixType> A_T = MT::copyTranspose(*A);
    Teuchos::RCP<VectorType> x = MT::cloneVectorFromMatrixRows( *A );
    Teuchos::RCP<VectorType> b = MT::cloneVectorFromMatrixRows( *A );
    VT::putScalar( *b, -1.0 );

    // Build the forward domain.
    Teuchos::ParameterList plist;
    plist.set<int>( "Overlap Size", 0 );
    Teuchos::RCP<DomainType> domain = Teuchos::rcp( new DomainType( A_T, x, plist ) );

    // History setup.
    HistoryType::setByteSize();

    // Create the forward source with a set number of histories.
    int mult = 10;
    double cutoff = 1.0e-8;
    plist.set<double>("Sample Ratio",mult);
    plist.set<double>("Weight Cutoff", cutoff);
    MCLS::UniformForwardSource<DomainType> 
	primary_source( b, domain, comm, comm->getSize(), 
			comm->getRank(), plist );

    // Pack and unpack the source.
    Teuchos::Array<char> source_buffer = primary_source.pack();
    MCLS::UniformForwardSource<DomainType> 
	source( source_buffer, domain, 
		comm, comm->getSize(), comm->getRank() );

    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numToTransport(), 0 );
    TEST_EQUALITY( source.numToTransportInSet(), mult*global_num_rows );
    TEST_EQUALITY( source.numRequested(), mult*global_num_rows );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), 0 );

    // Build the source.
    source.buildSource();
    TEST_ASSERT( !source.empty() );
    TEST_EQUALITY( source.numToTransport(), mult*local_num_rows );
    TEST_EQUALITY( source.numToTransportInSet(), mult*global_num_rows );
    TEST_EQUALITY( source.numRequested(), mult*global_num_rows );
    TEST_EQUALITY( source.numLeft(), mult*local_num_rows );
    TEST_EQUALITY( source.numEmitted(), 0 );

    // Sample the source.
    Teuchos::RCP<MCLS::PRNG<rng_type> > rng = Teuchos::rcp(
	new MCLS::PRNG<rng_type>(comm->getRank()) );
    source.setRNG( rng );
    for ( int i = 0; i < mult*local_num_rows; ++i )
    {
	TEST_ASSERT( !source.empty() );
	TEST_EQUALITY( source.numLeft(), mult*local_num_rows-i );
	TEST_EQUALITY( source.numEmitted(), i );

	Teuchos::RCP<HistoryType> history = source.getHistory();

	TEST_EQUALITY( history->weight(), 1.0 );
	TEST_ASSERT( domain->isGlobalState( history->globalState() ) );
	TEST_ASSERT( history->alive() );
	TEST_ASSERT( VT::isGlobalRow( *x, history->globalState() ) );
    }
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), mult*local_num_rows );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( UniformForwardSource, nh_set )
{
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef Epetra_RowMatrix MatrixType;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::ForwardHistory<int> HistoryType;
    typedef std::mt19937 rng_type;
    typedef MCLS::ForwardDomain<VectorType,MatrixType,rng_type> DomainType;

    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    Teuchos::RCP<Epetra_Comm> epetra_comm = getEpetraComm( comm );
    int comm_size = comm->getSize();

    int local_num_rows = 10;
    int global_num_rows = local_num_rows*comm_size;
    Teuchos::RCP<Epetra_Map> map = Teuchos::rcp(
	new Epetra_Map( global_num_rows, 0, *epetra_comm ) );

    // Build the linear system.
    Teuchos::RCP<Epetra_CrsMatrix> A = 	
	Teuchos::rcp( new Epetra_CrsMatrix( Copy, *map, 0 ) );
    Teuchos::Array<int> global_columns( 1 );
    Teuchos::Array<double> values( 1 );
    for ( int i = 1; i < global_num_rows; ++i )
    {
	global_columns[0] = i-1;
	values[0] = -0.5;
	A->InsertGlobalValues( i, global_columns().size(), 
			       &values[0], &global_columns[0] );
    }
    global_columns[0] = global_num_rows-1;
    values[0] = -0.5;
    A->InsertGlobalValues( global_num_rows-1, global_columns().size(),
			   &values[0], &global_columns[0] );
    A->FillComplete();

    Teuchos::RCP<MatrixType> A_T = MT::copyTranspose(*A);
    Teuchos::RCP<VectorType> x = MT::cloneVectorFromMatrixRows( *A );
    Teuchos::RCP<VectorType> b = MT::cloneVectorFromMatrixRows( *A );
    VT::putScalar( *b, -1.0 );

    // Build the forward domain.
    Teuchos::ParameterList plist;
    plist.set<int>( "Overlap Size", 0 );
    Teuchos::RCP<DomainType> domain = Teuchos::rcp( new DomainType( A_T, x, plist ) );

    // History setup.
    HistoryType::setByteSize();

    // Create the forward source with a set number of histories.
    int mult = 10;
    double cutoff = 1.0e-8;
    plist.set<double>("Sample Ratio",mult);
    plist.set<double>("Weight Cutoff", cutoff);
    MCLS::UniformForwardSource<DomainType> 
	source( b, domain, comm, comm->getSize(), comm->getRank(), plist );
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numToTransport(), 0 );
    TEST_EQUALITY( source.numToTransportInSet(), mult*global_num_rows );
    TEST_EQUALITY( source.numRequested(), mult*global_num_rows );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), 0 );

    // Build the source.
    source.buildSource();
    TEST_ASSERT( !source.empty() );
    TEST_EQUALITY( source.numToTransport(), mult*local_num_rows );
    TEST_EQUALITY( source.numToTransportInSet(), mult*global_num_rows );
    TEST_EQUALITY( source.numRequested(), mult*global_num_rows );
    TEST_EQUALITY( source.numLeft(), mult*local_num_rows );
    TEST_EQUALITY( source.numEmitted(), 0 );

    // Sample the source.
    Teuchos::RCP<MCLS::PRNG<rng_type> > rng = Teuchos::rcp(
	new MCLS::PRNG<rng_type>(comm->getRank()) );
    source.setRNG( rng );
    for ( int i = 0; i < mult*local_num_rows; ++i )
    {
	TEST_ASSERT( !source.empty() );
	TEST_EQUALITY( source.numLeft(), mult*local_num_rows-i );
	TEST_EQUALITY( source.numEmitted(), i );

	Teuchos::RCP<HistoryType> history = source.getHistory();

	TEST_EQUALITY( history->weight(), 1.0 );
	TEST_ASSERT( domain->isGlobalState( history->globalState() ) );
	TEST_ASSERT( history->alive() );
	TEST_ASSERT( VT::isGlobalRow( *x, history->globalState() ) );
    }
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), mult*local_num_rows );
}

//---------------------------------------------------------------------------//
// end tstEpetraUniformForwardSource.cpp
//---------------------------------------------------------------------------//

