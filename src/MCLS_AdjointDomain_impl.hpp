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

#include <MCLS_MatrixTraits.hpp>

#include <Teuchos_as.hpp>

namespace MCLS
{
//---------------------------------------------------------------------------//
/*!
 * \brief Constructor.
 */
template<class Vector, class Matrix>
AdjointDomain<Vector,Matrix>::AdjointDomain( 
    const Teuchos::RCP<const Matrix>& A,
    const Teuchos::RCP<Vector>& x,
    const Teuchos::ParameterList& plist )
{
    Require( !A.is_null() );
    Require( !x.is_null() );

    // Generate the transpose of the operator.
    Teuchos::RCP<Matrix> A_T = MT::copyTranspose( *A );

    // Generate the overlap for the transpose operator.
    int num_overlap = plist.get<int>( "Overlap Size" );
    Require( num_overlap >= 0 );
    Teuchos::RCP<Matrix> A_T_overlap = 
	MT::copyNearestNeighbors( *A_T, num_overlap );

    // Generate a solution vector with the overlap decomposition.
    Teuchos::RCP<Vector> x_overlap = 
	MT::cloneVectorFromMatrixRows( *A_T_overlap );

    // Build the adjoint tally from the solution vector and the overlap.
    d_tally = Teuchos::rcp( new TallyType( x, x_overlap ) );

    // Allocate space in local row data arrays.
    int num_rows = 
	MT::getLocalNumRows( *A_T ) + MT::getLocalNumRows( *A_T_overlap );
    d_columns.resize( num_rows );
    d_cdfs.resize( num_rows );
    d_weights.resize( num_rows );

    // Build the local CDFs and weights.
    addMatrixToDomain( A_T );
    addMatrixToDomain( A_T_overlap );

    // Get the boundary states and their owning process ranks.
    if ( num_overlap == 0 )
    {
	buildBoundary( A_T );
    }
    else
    {
	buildBoundary( A_T_overlap );
    }

    Ensure( !d_tally.is_null() );
}

//---------------------------------------------------------------------------//
/*
 * \brief Add matrix data to the local domain.
 */
template<class Vector, class Matrix>
void AdjointDomain<Vector,Matrix>::addMatrixToDomain( 
    const Teuchos::RCP<const Matrix>& A )
{
    Require( !A.is_null() );

    Ordinal local_num_rows = MT::getLocalNumRows( *A );
    Ordinal global_row = 0;
    int offset = d_row_indexer.size();
    int max_entries = MT::getGlobalMaxNumRowEntries( *A );
    std::size_t num_entries = 0;
    typename Teuchos::Array<Ordinal>::const_iterator diagonal_iterator;
    Teuchos::Array<double>::iterator cdf_iterator;

    for ( Ordinal i = 0; i < local_num_rows; ++i )
    {
	// Add the global row id and local row id to the indexer.
	global_row = MT::getGlobalRow(*A, i);
	d_row_indexer.put( global_row, i+offset );

	// Allocate column and CDF memory for this row.
	d_columns[i+offset].resize( max_entries );
	d_cdfs[i+offset].resize( max_entries );

	// Add the columns and base PDF values for this row.
	MT::getGlobalRowCopy( *A, 
			      global_row,
			      d_columns[i+offset](), 
			      d_cdfs[i+offset](), 
			      num_entries );

	Check( num_entries > 0 );

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
			d_columns[i+offset].begin()), diagonal_iterator) ] -= 1;
	}

	// Accumulate the absolute value of the PDF values to get a
	// non-normalized CDF for the row.
	d_cdfs[i+offset].front() = std::abs( d_cdfs[i+offset].front() );
	for ( cdf_iterator = d_cdfs[i+offset].begin()+1;
	      cdf_iterator != d_cdfs[i+offset].end();
	      ++cdf_iterator )
	{
	    *cdf_iterator = std::abs( *cdf_iterator ) + *(cdf_iterator-1);
	}

	// The final value in the non-normalized CDF is the weight for this
	// row. This is the absolute value row sum of the iteration matrix.
	d_weights[i] = d_cdfs[i+offset].back();
	Check( d_weights[i] >= 0 );

	// Normalize the CDF for the row.
	for ( cdf_iterator = d_cdfs[i+offset].begin();
	      cdf_iterator != d_cdfs[i+offset].end();
	      ++cdf_iterator )
	{
	    *cdf_iterator /= d_weights[i];
	    Check( *cdf_iterator >= 0.0 );
	}

	Ensure( std::abs(1.0 - d_cdfs[i+offset].back()) < 1.0e-6 );
    }
}

//---------------------------------------------------------------------------//
/*
 * \brief Build boundary data.
 */
template<class Vector, class Matrix>
void AdjointDomain<Vector,Matrix>::buildBoundary( 
    const Teuchos::RCP<const Matrix>& A )
{
    Require( !A.is_null() );

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
    MT::getGlobalRowRanks( *A_boundary, boundary_rows(), boundary_ranks() );

    // Process the boundary data.
    Teuchos::Array<int>::const_iterator neighbor_rank_it;
    Teuchos::Array<int>::const_iterator bnd_rank_it;
    typename Teuchos::Array<Ordinal>::const_iterator bnd_row_it;
    for ( bnd_row_it = boundary_rows.begin(), 
	  bnd_rank_it = boundary_ranks.begin();
	  bnd_row_it != boundary_rows.end();
	  ++bnd_row_it, ++bnd_rank_it )
    {
	Check( *bnd_rank_it != -1 );
	Check( *bnd_rank_it != MT::getComm( *A )->getRank() );

	// Look for the owning process in the neighbor rank array.
	neighbor_rank_it = std::find( d_neighbor_ranks.begin(), 
				      d_neighbor_ranks.end(),
				      *bnd_rank_it );

	// If it is new, add it to the neighbor rank array.
	if ( neighbor_rank_it == d_neighbor_ranks.end() )
	{
	    d_neighbor_ranks.push_back( *bnd_rank_it );
	    d_bnd_to_neighbor.put( *bnd_row_it, d_neighbor_ranks.size()-1 );
	}

	// Otherwise, just add it to the boundary state to local id table.
	else
	{
	    d_bnd_to_neighbor.put(
		*bnd_row_it, std::distance(
		    Teuchos::as<Teuchos::Array<int>::const_iterator>(
			d_neighbor_ranks.begin()), neighbor_rank_it) );
	}
    }

    Ensure( d_bnd_to_neighbor.size() == boundary_rows.size() );
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

#endif // end MCLS_ADJOINTDOMAIN_IMPL_HPP

//---------------------------------------------------------------------------//
// end MCLS_AdjointDomain_impl.hpp
// ---------------------------------------------------------------------------//
