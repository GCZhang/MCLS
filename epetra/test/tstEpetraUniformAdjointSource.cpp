//---------------------------------------------------------------------------//
/*!
 * \file tstEpetraUniformAdjointSource.cpp
 * \author Stuart R. Slattery
 * \brief Epetra AdjointDomain tests.
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

#include <MCLS_UniformAdjointSource.hpp>
#include <MCLS_AdjointDomain.hpp>
#include <MCLS_VectorTraits.hpp>
#include <MCLS_EpetraAdapter.hpp>
#include <MCLS_History.hpp>
#include <MCLS_AdjointTally.hpp>
#include <MCLS_Events.hpp>
#include <MCLS_RNGControl.hpp>

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
#include <Epetra_MpiComm.h>

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
TEUCHOS_UNIT_TEST( UniformAdjointSource, Typedefs )
{
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef Epetra_RowMatrix MatrixType;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::AdjointDomain<VectorType,MatrixType> DomainType;
    typedef MCLS::History<int> HistoryType;

    typedef MCLS::UniformAdjointSource<DomainType> SourceType;
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
TEUCHOS_UNIT_TEST( UniformAdjointSource, nh_not_set )
{
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef Epetra_RowMatrix MatrixType;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::History<int> HistoryType;
    typedef MCLS::AdjointDomain<VectorType,MatrixType> DomainType;

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

    Teuchos::RCP<VectorType> x = MT::cloneVectorFromMatrixRows( *A );
    Teuchos::RCP<VectorType> b = MT::cloneVectorFromMatrixRows( *A );
    VT::putScalar( *b, -1.0 );

    // Build the adjoint domain.
    Teuchos::ParameterList plist;
    plist.set<int>( "Overlap Size", 0 );
    Teuchos::RCP<DomainType> domain = Teuchos::rcp( new DomainType( A, x, plist ) );

    // History setup.
    Teuchos::RCP<MCLS::RNGControl> control = Teuchos::rcp(
	new MCLS::RNGControl( 3939294 ) );
    HistoryType::setByteSize( control->getSize() );

    // Create the adjoint source with default values.
    double cutoff = 1.0e-8;
    plist.set<double>("Weight Cutoff", cutoff );
    MCLS::UniformAdjointSource<DomainType> 
	source( b, domain, control, comm, plist );
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numToTransport(), 0 );
    TEST_EQUALITY( source.numToTransportInSet(), global_num_rows );
    TEST_EQUALITY( source.numRequested(), global_num_rows );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), 0 );
    TEST_EQUALITY( source.numStreams(), 0 );
    TEST_ASSERT( plist.isParameter("Relative Weight Cutoff") );
    TEST_EQUALITY( plist.get<double>("Relative Weight Cutoff"),
		   global_num_rows*cutoff );

    // Build the source.
    source.buildSource();
    TEST_ASSERT( !source.empty() );
    TEST_EQUALITY( source.numToTransport(), local_num_rows );
    TEST_EQUALITY( source.numToTransportInSet(), global_num_rows );
    TEST_EQUALITY( source.numRequested(), global_num_rows );
    TEST_EQUALITY( source.numLeft(), local_num_rows );
    TEST_EQUALITY( source.numEmitted(), 0 );
    TEST_EQUALITY( source.numStreams(), comm->getSize() );

    // Sample the source.
    for ( int i = 0; i < local_num_rows; ++i )
    {
	TEST_ASSERT( !source.empty() );
	TEST_EQUALITY( source.numLeft(), local_num_rows-i );
	TEST_EQUALITY( source.numEmitted(), i );

	Teuchos::RCP<HistoryType> history = source.getHistory();

	TEST_EQUALITY( history->weight(), -global_num_rows );
	TEST_ASSERT( domain->isLocalState( history->state() ) );
	TEST_ASSERT( history->alive() );
	TEST_ASSERT( VT::isGlobalRow( *x, history->state() ) );
    }
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), local_num_rows );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( UniformAdjointSource, nh_set )
{
    typedef Epetra_Vector VectorType;
    typedef MCLS::VectorTraits<VectorType> VT;
    typedef Epetra_RowMatrix MatrixType;
    typedef MCLS::MatrixTraits<VectorType,MatrixType> MT;
    typedef MCLS::History<int> HistoryType;
    typedef MCLS::AdjointDomain<VectorType,MatrixType> DomainType;

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

    Teuchos::RCP<VectorType> x = MT::cloneVectorFromMatrixRows( *A );
    Teuchos::RCP<VectorType> b = MT::cloneVectorFromMatrixRows( *A );
    VT::putScalar( *b, -1.0 );

    // Build the adjoint domain.
    Teuchos::ParameterList plist;
    plist.set<int>( "Overlap Size", 0 );
    Teuchos::RCP<DomainType> domain = Teuchos::rcp( new DomainType( A, x, plist ) );

    // History setup.
    Teuchos::RCP<MCLS::RNGControl> control = Teuchos::rcp(
	new MCLS::RNGControl( 3939294 ) );
    HistoryType::setByteSize( control->getSize() );

    // Create the adjoint source with a set number of histories.
    int mult = 10;
    double cutoff = 1.0e-8;
    plist.set<int>("Set Number of Histories", mult*global_num_rows);
    plist.set<double>("Weight Cutoff", cutoff);
    MCLS::UniformAdjointSource<DomainType> 
	source( b, domain, control, comm, plist );
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numToTransport(), 0 );
    TEST_EQUALITY( source.numToTransportInSet(), mult*global_num_rows );
    TEST_EQUALITY( source.numRequested(), mult*global_num_rows );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), 0 );
    TEST_EQUALITY( source.numStreams(), 0 );
    TEST_ASSERT( plist.isParameter("Relative Weight Cutoff") );
    TEST_EQUALITY( plist.get<double>("Relative Weight Cutoff"),
		   global_num_rows*cutoff );

    // Build the source.
    source.buildSource();
    TEST_ASSERT( !source.empty() );
    TEST_EQUALITY( source.numToTransport(), mult*local_num_rows );
    TEST_EQUALITY( source.numToTransportInSet(), mult*global_num_rows );
    TEST_EQUALITY( source.numRequested(), mult*global_num_rows );
    TEST_EQUALITY( source.numLeft(), mult*local_num_rows );
    TEST_EQUALITY( source.numEmitted(), 0 );
    TEST_EQUALITY( source.numStreams(), comm->getSize() );

    // Sample the source.
    for ( int i = 0; i < mult*local_num_rows; ++i )
    {
	TEST_ASSERT( !source.empty() );
	TEST_EQUALITY( source.numLeft(), mult*local_num_rows-i );
	TEST_EQUALITY( source.numEmitted(), i );

	Teuchos::RCP<HistoryType> history = source.getHistory();

	TEST_EQUALITY( history->weight(), -global_num_rows );
	TEST_ASSERT( domain->isLocalState( history->state() ) );
	TEST_ASSERT( history->alive() );
	TEST_ASSERT( VT::isGlobalRow( *x, history->state() ) );
    }
    TEST_ASSERT( source.empty() );
    TEST_EQUALITY( source.numLeft(), 0 );
    TEST_EQUALITY( source.numEmitted(), mult*local_num_rows );
}

//---------------------------------------------------------------------------//
// end tstEpetraUniformAdjointSource.cpp
//---------------------------------------------------------------------------//
