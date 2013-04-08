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
 * \file MCLS_AdjointDomain_impl.hpp
 * \author Stuart R. Slattery
 * \brief AdjointDomain implementation.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_ADJOINTDOMAIN_IMPL_HPP
#define MCLS_ADJOINTDOMAIN_IMPL_HPP

#include <algorithm>
#include <set>

#include "MCLS_MatrixTraits.hpp"
#include "MCLS_Serializer.hpp"
#include "MCLS_Preconditioner.hpp"
#include "MCLS_Estimators.hpp"

#include <Teuchos_as.hpp>
#include <Teuchos_Array.hpp>
#include <Teuchos_OrdinalTraits.hpp>

#include <Tpetra_Distributor.hpp>

namespace MCLS
{
//---------------------------------------------------------------------------//
/*!
 * \brief Matrix constructor.
 */
template<class Vector, class Matrix>
AdjointDomain<Vector,Matrix>::AdjointDomain( 
    const Teuchos::RCP<const Matrix>& A,
    const Teuchos::RCP<Vector>& x,
    const Teuchos::ParameterList& plist )
    : d_row_indexer( Teuchos::rcp(new MapType()) )
{
    MCLS_REQUIRE( !A.is_null() );
    MCLS_REQUIRE( !x.is_null() );

    // Get the estimator type. User the collision estimator as the default.
    d_estimator = COLLISION;
    if ( plist.isParameter("Estimator Type") )
    {
        d_estimator = plist.get<int>("Estimator Type");
    }

    // Get the amount of overlap.
    int num_overlap = plist.get<int>( "Overlap Size" );
    MCLS_REQUIRE( num_overlap >= 0 );

    // Build the adjoint tally from the solution vector.
    d_tally = Teuchos::rcp( new TallyType(x, d_estimator) );

    // Isolate domain generation for memory management. We want the operator
    // copies to clear if we are using the expected value estimator as we have
    // to make another copy there.
    {
        // Generate the transpose of the operator.
        Teuchos::RCP<Matrix> A_T = MT::copyTranspose( *A );

        // Apply the Neumann relaxation parameter.
        if ( plist.isParameter("Neumann Relaxation") )
        {
            Teuchos::RCP<Vector> omega = MT::cloneVectorFromMatrixRows(*A_T);
            VT::putScalar( *omega, plist.get<double>("Neumann Relaxation") );
            MT::leftScale( *A_T, *omega );
        }
    
        // Generate the overlap for the transpose operator.
        Teuchos::RCP<Matrix> A_T_overlap = 
            MT::copyNearestNeighbors( *A_T, num_overlap );

        // Generate a solution vector with the overlap decomposition.
        Teuchos::RCP<Vector> x_overlap = 
            MT::cloneVectorFromMatrixRows( *A_T_overlap );

        // Set the ovlerap with the tally.
        d_tally->setOverlapVector( x_overlap );

        // Allocate space in local row data arrays.
        int num_rows = 
            MT::getLocalNumRows( *A_T ) + MT::getLocalNumRows( *A_T_overlap );
        d_columns = Teuchos::ArrayRCP<Teuchos::Array<Ordinal> >( num_rows );
        d_cdfs = Teuchos::ArrayRCP<Teuchos::Array<double> >( num_rows );
        d_weights = Teuchos::ArrayRCP<double>( num_rows );

        // Build the local CDFs and weights.
        addMatrixToDomain( A_T );
        addMatrixToDomain( A_T_overlap );

        // Get the boundary states and their owning process ranks.
        if ( num_overlap == 0 )
        {
            buildBoundary( A_T, A );
        }
        else
        {
            buildBoundary( A_T_overlap, A );
        }
    }

    // By building the boundary data, now we know where we are sending
    // data. Find out who we are receiving from.
    Tpetra::Distributor distributor( MT::getComm(*A) );
    distributor.createFromSends( d_send_ranks() );
    d_receive_ranks = distributor.getImagesFrom();

    // If we're using the expected value estimator, build the iteration
    // matrix and provide it to the tally.
    if ( EXPECTED_VALUE == d_estimator )
    {
        // Get all of the rows in the domain.
        Teuchos::Array<Ordinal> domain_rows( d_row_indexer->size() );
        typename Teuchos::Array<Ordinal>::iterator domain_row_it;
        typename MapType::const_iterator indexer_it;
        for ( indexer_it = d_row_indexer->begin(),
          domain_row_it = domain_rows.begin();
              indexer_it != d_row_indexer->end();
              ++indexer_it, ++domain_row_it )
        {
            *domain_row_it = indexer_it->first;
        }

        // Generate the transpose of the operator.
        Teuchos::RCP<Matrix> A_T = MT::copyTranspose( *A );

        // Export the original operator into the domain row decomposition.
        Teuchos::RCP<Matrix> A_domain_decomp = 
            MT::exportFromRows( *A_T, domain_rows() );
        domain_rows.clear();

        // Apply Neumann relaxation parameter.
        if ( plist.isParameter("Neumann Relaxation") )
        {
            Teuchos::RCP<Vector> omega = 
                MT::cloneVectorFromMatrixRows(*A_domain_decomp);
            VT::putScalar( *omega, plist.get<double>("Neumann Relaxation") );
            MT::leftScale( *A_domain_decomp, *omega );
        }

        // Build the iteration matrix.
        buildIterationMatrix( A_domain_decomp );

        // Get all of the columns in the local iteration matrix and build the
        // iteration matrix tally vector.
        Teuchos::Array<Ordinal> im_cols( MT::getLocalNumCols(*A_domain_decomp) );
        MT::getMyGlobalCols( *A_domain_decomp, im_cols() );
        Teuchos::RCP<Vector> x_im = 
            VT::createFromRows( MT::getComm(*A), im_cols() );

        // Update the tally.
        d_tally->setIterationMatrix( d_h, d_im_cols, d_row_indexer );
        d_tally->setIterationMatrixVector( x_im );
    }
    
    MCLS_ENSURE( !d_tally.is_null() );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Deserializer constructor.
 * 
 * \param buffer Data buffer to construct the domain from.
 *
 * \param set_comm Set constant communicator for this domain over which to
 * reconstruct the tallies.
 */
template<class Vector, class Matrix>
AdjointDomain<Vector,Matrix>::AdjointDomain( 
    const Teuchos::ArrayView<char>& buffer,
    const Teuchos::RCP<const Comm>& set_comm )
    : d_row_indexer( Teuchos::rcp(new MapType()) )
{
    Ordinal num_rows = 0;
    int num_receives = 0;
    int num_sends = 0;
    Ordinal num_bnd = 0;
    Ordinal num_base = 0;
    Ordinal num_overlap = 0;

    Deserializer ds;
    ds.setBuffer( buffer() );

    // Unpack the estimator type.
    ds >> d_estimator;
    MCLS_CHECK( d_estimator >= 0 );

    // Unpack the local number of rows.
    ds >> num_rows;
    MCLS_CHECK( num_rows > 0 );

    // Unpack the number of receive neighbors.
    ds >> num_receives;
    MCLS_CHECK( num_receives >= 0 );

    // Unpack the number of send neighbors.
    ds >> num_sends;
    MCLS_CHECK( num_sends >= 0 );

    // Unpack the number of boundary states.
    ds >> num_bnd;
    MCLS_CHECK( num_bnd >= 0 );

    // Unpack the number of base rows in the tally.
    ds >> num_base;
    MCLS_CHECK( num_base > 0 );

    // Unpack the number of overlap rows in the tally.
    ds >> num_overlap;
    MCLS_CHECK( num_overlap >= 0 );
    MCLS_CHECK( num_base + num_overlap == num_rows );

    // Unpack the local row indexer by key-value pairs.
    Ordinal global_row = 0;
    int local_row = 0;
    for ( Ordinal n = 0; n < num_rows; ++n )
    {
	ds >> global_row >> local_row;
	(*d_row_indexer)[global_row] = local_row;
    }

    // Unpack the local columns.
    d_columns = Teuchos::ArrayRCP<Teuchos::Array<Ordinal> >( num_rows );
    Ordinal num_cols = 0;
    typename Teuchos::ArrayRCP<Teuchos::Array<Ordinal> >::iterator column_it;
    typename Teuchos::Array<Ordinal>::iterator index_it;
    for( column_it = d_columns.begin(); 
	 column_it != d_columns.end(); 
	 ++column_it )
    {
	// Unpack the number of column entries in the row.
	ds >> num_cols;
	column_it->resize( num_cols );

	// Unpack the column indices.
	for ( index_it = column_it->begin();
	      index_it != column_it->end();
	      ++index_it )
	{
	    ds >> *index_it;
	}
    }

    // Unpack the local cdfs.
    d_cdfs = Teuchos::ArrayRCP<Teuchos::Array<double> >( num_rows );
    Ordinal num_values = 0;
    Teuchos::ArrayRCP<Teuchos::Array<double> >::iterator cdf_it;
    Teuchos::Array<double>::iterator value_it;
    for( cdf_it = d_cdfs.begin(); cdf_it != d_cdfs.end(); ++cdf_it )
    {
	// Unpack the number of entries in the row cdf.
	ds >> num_values;
	cdf_it->resize( num_values );

	// Unpack the cdf values.
	for ( value_it = cdf_it->begin();
	      value_it != cdf_it->end();
	      ++value_it )
	{
	    ds >> *value_it;
	}
    }

    // If using the expected value estimator, unpack the local iteration
    // matrix and global_cols.
    std::set<Ordinal> im_unique_cols;
    if ( EXPECTED_VALUE == d_estimator )
    {
        // Unpack the iteration matrix values.
        d_h = 
            Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<double> > >(num_rows);
        num_values = 0;
        Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<double> > >::iterator h_it;
        Teuchos::Array<double>::iterator h_val_it;
        for( h_it = d_h.begin(); h_it != d_h.end(); ++h_it )
        {
            // Unpack the number of entries in the row h.
            ds >> num_values;
            *h_it = Teuchos::rcp( new Teuchos::Array<double>(num_values) );

            // Unpack the iteration matrix values.
            for ( h_val_it = (*h_it)->begin();
                  h_val_it != (*h_it)->end();
                  ++h_val_it )
            {
                ds >> *h_val_it;
            }
        }

        // Unpack the iteration matrix columns.
        d_im_cols = 
            Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<Ordinal> > >(num_rows);
        num_values = 0;
        typename Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<Ordinal> > >::iterator 
            col_it;
        typename Teuchos::Array<Ordinal>::iterator col_val_it;
        for( col_it = d_im_cols.begin(); 
             col_it != d_im_cols.end(); 
             ++col_it )
        {
            // Unpack the number of columns in the row h.
            ds >> num_values;
            *col_it = Teuchos::rcp( new Teuchos::Array<Ordinal>(num_values) );

            // Unpack the iteration matrix global columns and also insert them
            // into the set.
            for ( col_val_it = (*col_it)->begin();
                  col_val_it != (*col_it)->end();
                  ++col_val_it )
            {
                ds >> *col_val_it;
                im_unique_cols.insert( *col_val_it );
            }
        }
    }

    // Unpack the local weights.
    d_weights = Teuchos::ArrayRCP<double>( num_rows );
    Teuchos::ArrayRCP<double>::iterator weight_it;
    for ( weight_it = d_weights.begin();
	  weight_it != d_weights.end();
	  ++weight_it )
    {
	ds >> *weight_it;
    }

    // Unpack the receive ranks.
    d_receive_ranks.resize( num_receives );
    Teuchos::Array<int>::iterator receive_it;
    for ( receive_it = d_receive_ranks.begin();
	  receive_it != d_receive_ranks.end();
	  ++receive_it )
    {
	ds >> *receive_it;
    }

    // Unpack the send ranks.
    d_send_ranks.resize( num_sends );
    Teuchos::Array<int>::iterator send_it;
    for ( send_it = d_send_ranks.begin();
	  send_it != d_send_ranks.end();
	  ++send_it )
    {
	ds >> *send_it;
    }

    // Unpack the boundary-to-neighbor id table.
    Ordinal boundary_row = 0;
    int neighbor = 0;
    for ( Ordinal n = 0; n < num_bnd; ++n )
    {
	ds >> boundary_row >> neighbor;
	d_bnd_to_neighbor[boundary_row] = neighbor;
    }

    // Unpack the tally base rows.
    Teuchos::Array<Ordinal> base_rows( num_base );
    typename Teuchos::Array<Ordinal>::iterator base_it;
    for ( base_it = base_rows.begin();
	  base_it != base_rows.end();
	  ++base_it )
    {
	ds >> *base_it;
    }

    // Unpack the tally overlap rows.
    Teuchos::Array<Ordinal> overlap_rows( num_overlap );
    typename Teuchos::Array<Ordinal>::iterator overlap_it;
    for ( overlap_it = overlap_rows.begin();
	  overlap_it != overlap_rows.end();
	  ++overlap_it )
    {
	ds >> *overlap_it;
    }

    MCLS_CHECK( ds.end() == ds.getPtr() );

    // Build the tally.
    Teuchos::RCP<Vector> base_x = 
	VT::createFromRows( set_comm, base_rows() );
    Teuchos::RCP<Vector> overlap_x = 
	VT::createFromRows( set_comm, overlap_rows() );
    d_tally = Teuchos::rcp( new TallyType(base_x, d_estimator) );
    d_tally->setOverlapVector( overlap_x );

    // Set the iteration matrix data with the tally if using the expected
    // value estimator along with the boundary tally vector.
    if ( EXPECTED_VALUE == d_estimator )
    {
        // Get all of the columns in the local iteration matrix and build the
        // iteration matrix tally vector.
        Teuchos::Array<Ordinal> im_cols( im_unique_cols.size() );
        std::copy( im_unique_cols.begin(), im_unique_cols.end(), im_cols.begin() );
        im_unique_cols.clear();
        Teuchos::RCP<Vector> x_im = VT::createFromRows( set_comm, im_cols() );
        im_cols.clear();

        // Update the tally.
        d_tally->setIterationMatrix( d_h, d_im_cols, d_row_indexer );
        d_tally->setIterationMatrixVector( x_im );
    }

    MCLS_ENSURE( !d_tally.is_null() );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Pack the domain into a buffer.
 */
template<class Vector, class Matrix>
Teuchos::Array<char> AdjointDomain<Vector,Matrix>::pack() const
{
    // Get the byte size of the buffer.
    std::size_t packed_bytes = getPackedBytes();
    MCLS_CHECK( packed_bytes );

    // Build the buffer and set it with the serializer.
    Teuchos::Array<char> buffer( packed_bytes );
    Serializer s;
    s.setBuffer( buffer() );

    // Pack the estimator type.
    s << Teuchos::as<int>(d_estimator);

    // Pack the local number of rows.
    s << Teuchos::as<Ordinal>(d_row_indexer->size());

    // Pack in the number of receive neighbors.
    s << Teuchos::as<int>(d_receive_ranks.size());

    // Pack in the number of send neighbors.
    s << Teuchos::as<int>(d_send_ranks.size());

    // Pack in the number of boundary states.
    s << Teuchos::as<Ordinal>(d_bnd_to_neighbor.size());

    // Pack in the number of base rows in the tally.
    s << Teuchos::as<Ordinal>(d_tally->numBaseRows());

    // Pack in the number of overlap rows in the tally.
    s << Teuchos::as<Ordinal>(d_tally->numOverlapRows());

    // Pack up the local row indexer by key-value pairs.
    typename MapType::const_iterator row_index_it;
    for ( row_index_it = d_row_indexer->begin();
	  row_index_it != d_row_indexer->end();
	  ++row_index_it )
    {
	s << row_index_it->first << row_index_it->second;
    }

    // Pack up the local columns.
    typename Teuchos::ArrayRCP<Teuchos::Array<Ordinal> >::const_iterator 
        column_it;
    typename Teuchos::Array<Ordinal>::const_iterator index_it;
    for( column_it = d_columns.begin(); 
	 column_it != d_columns.end(); 
	 ++column_it )
    {
	// Pack the number of column entries in the row.
	s << Teuchos::as<Ordinal>( column_it->size() );

	// Pack in the column indices.
	for ( index_it = column_it->begin();
	      index_it != column_it->end();
	      ++index_it )
	{
	    s << *index_it;
	}
    }

    // Pack up the local cdfs.
    Teuchos::ArrayRCP<Teuchos::Array<double> >::const_iterator cdf_it;
    Teuchos::Array<double>::const_iterator value_it;
    for( cdf_it = d_cdfs.begin(); cdf_it != d_cdfs.end(); ++cdf_it )
    {
	// Pack the number of entries in the row cdf.
	s << Teuchos::as<Ordinal>( cdf_it->size() );

	// Pack in the column indices.
	for ( value_it = cdf_it->begin();
	      value_it != cdf_it->end();
	      ++value_it )
	{
	    s << *value_it;
	}
    }

    // If using the expected value estimator, pack the local iteration
    // matrix and global_cols.
    if ( EXPECTED_VALUE == d_estimator )
    {
        // Pack the iteration matrix values.
        Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<double> > >::const_iterator h_it;
        Teuchos::Array<double>::const_iterator h_val_it;
        for( h_it = d_h.begin(); h_it != d_h.end(); ++h_it )
        {
            // Pack the number of entries in the row h.
            s << Teuchos::as<Ordinal>( (*h_it)->size() );

            // Pack the iteration matrix values.
            for ( h_val_it = (*h_it)->begin();
                  h_val_it != (*h_it)->end();
                  ++h_val_it )
            {
                s << *h_val_it;
            }
        }

        // Pack the iteration matrix columns.
        typename Teuchos::ArrayRCP<
            Teuchos::RCP<Teuchos::Array<Ordinal> > >::const_iterator col_it;
        typename Teuchos::Array<Ordinal>::const_iterator col_val_it;
        for( col_it = d_im_cols.begin(); 
             col_it != d_im_cols.end(); 
             ++col_it )
        {
            // Pack the number of columns in the row h.
            s << Teuchos::as<Ordinal>( (*col_it)->size() );

            // Pack the iteration matrix global columns.
            for ( col_val_it = (*col_it)->begin();
                  col_val_it != (*col_it)->end();
                  ++col_val_it )
            {
                s << *col_val_it;
            }
        }
    }

    // Pack up the local weights.
    Teuchos::ArrayRCP<double>::const_iterator weight_it;
    for ( weight_it = d_weights.begin();
	  weight_it != d_weights.end();
	  ++weight_it )
    {
	s << *weight_it;
    }

    // Pack up the receive ranks.
    Teuchos::Array<int>::const_iterator receive_it;
    for ( receive_it = d_receive_ranks.begin();
	  receive_it != d_receive_ranks.end();
	  ++receive_it )
    {
	s << *receive_it;
    }

    // Pack up the send ranks.
    Teuchos::Array<int>::const_iterator send_it;
    for ( send_it = d_send_ranks.begin();
	  send_it != d_send_ranks.end();
	  ++send_it )
    {
	s << *send_it;
    }

    // Pack up the boundary-to-neighbor id table.
    typename MapType::const_iterator bnd_it;
    for ( bnd_it = d_bnd_to_neighbor.begin();
	  bnd_it != d_bnd_to_neighbor.end();
	  ++bnd_it )
    {
	s << bnd_it->first << bnd_it->second;
    }

    // Pack up the tally base rows.
    Teuchos::Array<Ordinal> base_rows = d_tally->baseRows();
    typename Teuchos::Array<Ordinal>::const_iterator base_it;
    for ( base_it = base_rows.begin();
	  base_it != base_rows.end();
	  ++base_it )
    {
	s << *base_it;
    }

    // Pack up the tally overlap rows.
    Teuchos::Array<Ordinal> overlap_rows = d_tally->overlapRows();
    typename Teuchos::Array<Ordinal>::const_iterator overlap_it;
    for ( overlap_it = overlap_rows.begin();
	  overlap_it != overlap_rows.end();
	  ++overlap_it )
    {
	s << *overlap_it;
    }

    MCLS_ENSURE( s.end() == s.getPtr() );

    return buffer;
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the size of this object in packed bytes.
 */
template<class Vector, class Matrix>
std::size_t AdjointDomain<Vector,Matrix>::getPackedBytes() const
{
    Serializer s;
    s.computeBufferSizeMode();

    // Pack the estimator type.
    s << Teuchos::as<int>(d_estimator);

    // Pack the local number of rows.
    s << Teuchos::as<Ordinal>(d_row_indexer->size());

    // Pack in the number of receive neighbors.
    s << Teuchos::as<int>(d_receive_ranks.size());

    // Pack in the number of send neighbors.
    s << Teuchos::as<int>(d_send_ranks.size());

    // Pack in the number of boundary states.
    s << Teuchos::as<Ordinal>(d_bnd_to_neighbor.size());

    // Pack in the number of base rows in the tally.
    s << Teuchos::as<Ordinal>(d_tally->numBaseRows());

    // Pack in the number of overlap rows in the tally.
    s << Teuchos::as<Ordinal>(d_tally->numOverlapRows());

    // Pack up the local row indexer by key-value pairs.
    typename MapType::const_iterator row_index_it;
    for ( row_index_it = d_row_indexer->begin();
	  row_index_it != d_row_indexer->end();
	  ++row_index_it )
    {
	s << row_index_it->first << row_index_it->second;
    }

    // Pack up the local columns.
    typename Teuchos::ArrayRCP<Teuchos::Array<Ordinal> >::const_iterator 
        column_it;
    typename Teuchos::Array<Ordinal>::const_iterator index_it;
    for( column_it = d_columns.begin(); 
	 column_it != d_columns.end(); 
	 ++column_it )
    {
	// Pack the number of column entries in the row.
	s << Teuchos::as<Ordinal>( column_it->size() );

	// Pack in the column indices.
	for ( index_it = column_it->begin();
	      index_it != column_it->end();
	      ++index_it )
	{
	    s << *index_it;
	}
    }

    // Pack up the local cdfs.
    Teuchos::ArrayRCP<Teuchos::Array<double> >::const_iterator cdf_it;
    Teuchos::Array<double>::const_iterator value_it;
    for( cdf_it = d_cdfs.begin(); cdf_it != d_cdfs.end(); ++cdf_it )
    {
	// Pack the number of entries in the row cdf.
	s << Teuchos::as<Ordinal>( cdf_it->size() );

	// Pack in the column indices.
	for ( value_it = cdf_it->begin();
	      value_it != cdf_it->end();
	      ++value_it )
	{
	    s << *value_it;
	}
    }

    // If using the expected value estimator, pack the local iteration
    // matrix and global_cols.
    if ( EXPECTED_VALUE == d_estimator )
    {
        // Pack the iteration matrix values.
        Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<double> > >::const_iterator h_it;
        Teuchos::Array<double>::const_iterator h_val_it;
        for( h_it = d_h.begin(); h_it != d_h.end(); ++h_it )
        {
            // Pack the number of entries in the row h.
            s << Teuchos::as<Ordinal>( (*h_it)->size() );

            // Pack the iteration matrix values.
            for ( h_val_it = (*h_it)->begin();
                  h_val_it != (*h_it)->end();
                  ++h_val_it )
            {
                s << *h_val_it;
            }
        }

        // Pack the iteration matrix columns.
        typename Teuchos::ArrayRCP<
            Teuchos::RCP<Teuchos::Array<Ordinal> > >::const_iterator col_it;
        typename Teuchos::Array<Ordinal>::const_iterator col_val_it;
        for( col_it = d_im_cols.begin(); 
             col_it != d_im_cols.end(); 
             ++col_it )
        {
            // Pack the number of columns in the row h.
            s << Teuchos::as<Ordinal>( (*col_it)->size() );

            // Pack the iteration matrix global columns.
            for ( col_val_it = (*col_it)->begin();
                  col_val_it != (*col_it)->end();
                  ++col_val_it )
            {
                s << *col_val_it;
            }
        }
    }

    // Pack up the local weights.
    Teuchos::ArrayRCP<double>::const_iterator weight_it;
    for ( weight_it = d_weights.begin();
	  weight_it != d_weights.end();
	  ++weight_it )
    {
	s << *weight_it;
    }

    // Pack up the receive ranks.
    Teuchos::Array<int>::const_iterator receive_it;
    for ( receive_it = d_receive_ranks.begin();
	  receive_it != d_receive_ranks.end();
	  ++receive_it )
    {
	s << *receive_it;
    }

    // Pack up the send ranks.
    Teuchos::Array<int>::const_iterator send_it;
    for ( send_it = d_send_ranks.begin();
	  send_it != d_send_ranks.end();
	  ++send_it )
    {
	s << *send_it;
    }

    // Pack up the boundary-to-neighbor id table.
    typename MapType::const_iterator bnd_it;
    for ( bnd_it = d_bnd_to_neighbor.begin();
	  bnd_it != d_bnd_to_neighbor.end();
	  ++bnd_it )
    {
	s << bnd_it->first << bnd_it->second;
    }

    // Pack up the tally base rows.
    Teuchos::Array<Ordinal> base_rows = d_tally->baseRows();
    typename Teuchos::Array<Ordinal>::const_iterator base_it;
    for ( base_it = base_rows.begin();
	  base_it != base_rows.end();
	  ++base_it )
    {
	s << *base_it;
    }

    // Pack up the tally overlap rows.
    Teuchos::Array<Ordinal> overlap_rows = d_tally->overlapRows();
    typename Teuchos::Array<Ordinal>::const_iterator overlap_it;
    for ( overlap_it = overlap_rows.begin();
	  overlap_it != overlap_rows.end();
	  ++overlap_it )
    {
	s << *overlap_it;
    }

    return s.size();
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the neighbor domain process rank from which we will receive.
 */
template<class Vector, class Matrix>
int AdjointDomain<Vector,Matrix>::receiveNeighborRank( int n ) const
{
    MCLS_REQUIRE( n >= 0 && n < d_receive_ranks.size() );
    return d_receive_ranks[n];
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the neighbor domain process rank to which we will send.
 */
template<class Vector, class Matrix>
int AdjointDomain<Vector,Matrix>::sendNeighborRank( int n ) const
{
    MCLS_REQUIRE( n >= 0 && n < d_send_ranks.size() );
    return d_send_ranks[n];
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the neighbor domain that owns a boundary state (local neighbor
 * id).
 */
template<class Vector, class Matrix>
int AdjointDomain<Vector,Matrix>::owningNeighbor( const Ordinal& state ) const
{
    typename MapType::const_iterator neighbor = d_bnd_to_neighbor.find( state );
    MCLS_REQUIRE( neighbor != d_bnd_to_neighbor.end() );
    return neighbor->second;
}

//---------------------------------------------------------------------------//
/*
 * \brief Add matrix data to the local domain.
 */
template<class Vector, class Matrix>
void AdjointDomain<Vector,Matrix>::addMatrixToDomain( 
    const Teuchos::RCP<const Matrix>& A )
{
    MCLS_REQUIRE( !A.is_null() );

    Ordinal local_num_rows = MT::getLocalNumRows( *A );
    Ordinal global_row = 0;
    int offset = d_row_indexer->size();
    int max_entries = MT::getGlobalMaxNumRowEntries( *A );
    std::size_t num_entries = 0;
    typename Teuchos::Array<Ordinal>::const_iterator diagonal_iterator;
    Teuchos::Array<double>::iterator cdf_iterator;

    for ( Ordinal i = 0; i < local_num_rows; ++i )
    {
	// Add the global row id and local row id to the indexer.
	global_row = MT::getGlobalRow(*A, i);
	(*d_row_indexer)[global_row] = i+offset;

	// Allocate column and CDF memory for this row.
	d_columns[i+offset].resize( max_entries );
	d_cdfs[i+offset].resize( max_entries );

	// Add the columns and base PDF values for this row.
	MT::getGlobalRowCopy( *A, 
			      global_row,
			      d_columns[i+offset](), 
			      d_cdfs[i+offset](), 
			      num_entries );

	// Check for degeneracy.
	MCLS_CHECK( num_entries > 0 );

	// Resize local column and CDF arrays for this row.
	d_columns[i+offset].resize( num_entries );
	d_cdfs[i+offset].resize( num_entries );

	// If this row contains an entry on the diagonal, subtract 1 for the
	// identity matrix (H^T = I-A^T).
	diagonal_iterator = std::find( d_columns[i+offset].begin(),
				       d_columns[i+offset].end(),
				       global_row );
	if ( diagonal_iterator != d_columns[i+offset].end() )
	{
	    d_cdfs[i+offset][ std::distance(
		    Teuchos::as<typename Teuchos::Array<Ordinal>::const_iterator>(
			d_columns[i+offset].begin()), diagonal_iterator) ] -= 1.0;
	}

	// Accumulate the absolute value of the PDF values to get a
	// non-normalized CDF for the row.
	d_cdfs[i+offset].front() = std::abs( d_cdfs[i+offset].front() );
	for ( cdf_iterator = d_cdfs[i+offset].begin()+1;
	      cdf_iterator != d_cdfs[i+offset].end();
	      ++cdf_iterator )
	{
	    *cdf_iterator = std::abs( (*cdf_iterator) ) + *(cdf_iterator-1);
	}

	// The final value in the non-normalized CDF is the weight for this
	// row. This is the absolute value row sum of the iteration matrix.
	d_weights[i+offset] = d_cdfs[i+offset].back();
	MCLS_CHECK( d_weights[i+offset] >= 0 );

	// Normalize the CDF for the row.
	for ( cdf_iterator = d_cdfs[i+offset].begin();
	      cdf_iterator != d_cdfs[i+offset].end();
	      ++cdf_iterator )
	{
	    *cdf_iterator /= d_weights[i+offset];
	    MCLS_CHECK( *cdf_iterator >= 0.0 );
	}

	MCLS_CHECK( std::abs(1.0 - d_cdfs[i+offset].back()) < 1.0e-6 );
    }
}

//---------------------------------------------------------------------------//
/*
 * \brief Build boundary data.
 */
template<class Vector, class Matrix>
void AdjointDomain<Vector,Matrix>::buildBoundary( 
    const Teuchos::RCP<const Matrix>& A,
    const Teuchos::RCP<const Matrix>& base_A )
{
    MCLS_REQUIRE( !A.is_null() );

    // Get the next set of off-process rows. This is the boundary. If we
    // transition to these then we have left the local domain.
    Teuchos::RCP<Matrix> A_boundary = MT::copyNearestNeighbors( *A, 1 );

    // Get the boundary rows.
    Ordinal global_row = 0;
    Teuchos::Array<Ordinal> boundary_rows;
    for ( Ordinal i = 0; i < MT::getLocalNumRows( *A_boundary ); ++i )
    {
	global_row = MT::getGlobalRow( *A_boundary, i );
	if ( !isLocalState(global_row) )
	{
	    boundary_rows.push_back( global_row );
	}
    }

    // Get the owning ranks for the boundary rows.
    Teuchos::Array<int> boundary_ranks( boundary_rows.size() );
    MT::getGlobalRowRanks( *base_A, boundary_rows(), boundary_ranks() );

    // Process the boundary data.
    Teuchos::Array<int>::const_iterator send_rank_it;
    Teuchos::Array<int>::const_iterator bnd_rank_it;
    typename Teuchos::Array<Ordinal>::const_iterator bnd_row_it;
    for ( bnd_row_it = boundary_rows.begin(), 
	 bnd_rank_it = boundary_ranks.begin();
	  bnd_row_it != boundary_rows.end();
	  ++bnd_row_it, ++bnd_rank_it )
    {
	MCLS_CHECK( *bnd_rank_it != -1 );

	// Look for the owning process in the send rank array.
	send_rank_it = std::find( d_send_ranks.begin(), 
				  d_send_ranks.end(),
				  *bnd_rank_it );

	// If it is new, add it to the send rank array.
	if ( send_rank_it == d_send_ranks.end() )
	{
	    d_send_ranks.push_back( *bnd_rank_it );
	    d_bnd_to_neighbor[*bnd_row_it] = d_send_ranks.size()-1;
	}

	// Otherwise, just add it to the boundary state to local id table.
	else
	{
	    d_bnd_to_neighbor[*bnd_row_it] =
		std::distance( 
		    Teuchos::as<Teuchos::Array<int>::const_iterator>(
			d_send_ranks.begin()), send_rank_it);
	}
    }

    MCLS_ENSURE( d_bnd_to_neighbor.size() == 
		 Teuchos::as<std::size_t>(boundary_rows.size()) );
}

//---------------------------------------------------------------------------//
/*
 * \brief Build the iteration matrix.
 */
template<class Vector, class Matrix>
void AdjointDomain<Vector,Matrix>::buildIterationMatrix(
    const Teuchos::RCP<const Matrix>& A )
{
    MCLS_REQUIRE( !A.is_null() );

    // Setup.
    Ordinal local_num_rows = MT::getLocalNumRows( *A );
    Ordinal global_row = 0;
    int max_entries = MT::getGlobalMaxNumRowEntries( *A );
    std::size_t num_entries = 0;
    typename Teuchos::Array<Ordinal>::const_iterator diagonal_iterator;
    Teuchos::Array<double>::iterator cdf_iterator;

    // Allocate for the rows and columns.
    d_h = 
        Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<double> > >(local_num_rows);
    d_im_cols = 
        Teuchos::ArrayRCP<Teuchos::RCP<Teuchos::Array<Ordinal> > >(local_num_rows);

    // Extract the matrix data.
    for ( Ordinal i = 0; i < local_num_rows; ++i )
    {
	// Get the global row.
	global_row = MT::getGlobalRow(*A, i);

	// Allocate column and value memory for this row.
	d_im_cols[i] = Teuchos::rcp( 
            new Teuchos::Array<Ordinal>(max_entries) );
	d_h[i] = Teuchos::rcp( 
            new Teuchos::Array<double>(max_entries) );

	// Add the columns and base PDF values for this row.
	MT::getGlobalRowCopy( *A, 
			      global_row,
			      (*d_im_cols[i])(), 
			      (*d_h[i])(), 
			      num_entries );
    
	// Check for degeneracy.
	MCLS_CHECK( num_entries > 0 );

	// Resize local column and CDF arrays for this row.
	d_im_cols[i]->resize( num_entries );
	d_h[i]->resize( num_entries );

	// If this row contains an entry on the diagonal, subtract 1 for the
	// identity matrix (H^T = I-A^T).
	diagonal_iterator = std::find( d_im_cols[i]->begin(),
				       d_im_cols[i]->end(),
				       global_row );
	if ( diagonal_iterator != d_im_cols[i]->end() )
        {
	    (*d_h[i])[ std::distance(
		    Teuchos::as<typename Teuchos::Array<Ordinal>::const_iterator>(
			d_im_cols[i]->begin()), diagonal_iterator) ] -= 1.0;
        }
    }
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

#endif // end MCLS_ADJOINTDOMAIN_IMPL_HPP

//---------------------------------------------------------------------------//
// end MCLS_AdjointDomain_impl.hpp
// ---------------------------------------------------------------------------//

