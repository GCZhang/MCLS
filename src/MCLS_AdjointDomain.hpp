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
 * \file MCLS_AdjointDomain.hpp
 * \author Stuart R. Slattery
 * \brief AdjointDomain declaration.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_ADJOINTDOMAIN_HPP
#define MCLS_ADJOINTDOMAIN_HPP

#include <stack>

#include <MCLS_DBC.hpp>
#include <MCLS_History.hpp>
#include <MCLS_AdjointTally.hpp>
#include <MCLS_SamplingTools.hpp>
#include <MCLS_Events.hpp>
#include <MCLS_VectorTraits.hpp>
#include <MCLS_MatrixTraits.hpp>

#include <Teuchos_RCP.hpp>
#include <Teuchos_Array.hpp>
#include <Teuchos_ArrayView.hpp>
#include <Teuchos_ParameterList.hpp>

#include <boost/tr1/unordered_map.hpp>

namespace MCLS
{

//---------------------------------------------------------------------------//
/*!
 * \class AdjointDomain
 * \brief Adjoint transport domain.
 *
 * Derived from the adjoint Neumann-Ulam product of a matrix.
 *
 * H^T = I - A^T 
 * H^T = (P) x (W)
 *
 * This domain contains data for all local states in the system, including the
 * overlap and neighboring domains. This object is responsible for creating
 * the tally for the solution vector over the domain as it has ownership of
 * the parallel decomposition of the domain.
 */
template<class Vector, class Matrix>
class AdjointDomain
{
  public:

    //@{
    //! Typedefs.
    typedef Vector                                      vector_type;
    typedef VectorTraits<Vector>                        VT;
    typedef Matrix                                      matrix_type;
    typedef MatrixTraits<Vector,Matrix>                 MT;
    typedef typename VT::global_ordinal_type            Ordinal;
    typedef AdjointTally<Vector>                        TallyType;
    typedef typename TallyType::HistoryType             HistoryType;
    typedef std::stack<Teuchos::RCP<HistoryType> >      BankType;
    //@}

    // Matrix constructor.
    AdjointDomain( const Teuchos::RCP<const Matrix>& A,
		   const Teuchos::RCP<Vector>& x,
		   const Teuchos::ParameterList& plist );

    // Deserializer constructor.
    explicit AdjointDomain( const Teuchos::ArrayView<char>& buffer );

    // Destructor.
    ~AdjointDomain()
    { /* ... */ }

    // Pack the domain into a buffer.
    Teuchos::Array<char> pack() const;

    // Process a history through a transition to a new state.
    inline void processTransition( HistoryType& history );

    // Get the domain tally.
    Teuchos::RCP<TallyType> domainTally() const
    { return d_tally; }

    // Determine if a given state is on-process.
    inline bool isLocalState( const Ordinal& state );

    //! Get the number of neighboring domains from which we will receive.
    int numReceiveNeighbors() const
    { return d_receive_ranks.size(); }

    // Get the neighbor domain process rank from which we will receive.
    int receiveNeighborRank( int n ) const;

    //! Get the number of neighboring domains to which we will send.
    int numSendNeighbors() const
    { return d_send_ranks.size(); }

    // Get the neighbor domain process rank to which we will send.
    int sendNeighborRank( int n ) const;

    // Get the neighbor domain that owns a boundary state (local neighbor id).
    int owningNeighbor( const Ordinal& state );

  public:

    // Set the byte size of the packed domain state.
    static void setByteSize();

    // Get the number of bytes in the packed domain state.
    static std::size_t getPackedBytes();

  private:

    // Add matrix data to the local domain.
    void addMatrixToDomain( const Teuchos::RCP<const Matrix>& A );

    // Build boundary data.
    void buildBoundary( const Teuchos::RCP<const Matrix>& A,
			const Teuchos::RCP<const Matrix>& base_A );

  private:

    // Domain tally.
    Teuchos::RCP<TallyType> d_tally;

    // Local row indexer.
    std::tr1::unordered_map<Ordinal,int> d_row_indexer;

    // Local columns.
    Teuchos::Array<Teuchos::Array<Ordinal> > d_columns;

    // Local CDFs.
    Teuchos::Array<Teuchos::Array<double> > d_cdfs;

    // Local weights.
    Teuchos::Array<double> d_weights;

    // Neighboring domain process ranks from which we will receive.
    Teuchos::Array<int> d_receive_ranks;

    // Neighboring domain process ranks to which we will send.
    Teuchos::Array<int> d_send_ranks;

    // Boundary state to owning neighbor local id table.
    std::tr1::unordered_map<Ordinal,int> d_bnd_to_neighbor;

  private:

    // Packed size of domain in bytes.
    static std::size_t d_packed_bytes;
};

//---------------------------------------------------------------------------//
// Inline functions.
//---------------------------------------------------------------------------//
/*!
 * \brief Process a history through a transition to a new state.
 */
template<class Vector, class Matrix>
inline void AdjointDomain<Vector,Matrix>::processTransition( 
    HistoryType& history )
{
    Require( history.alive() );
    Require( TRANSITION == history.event() );
    Require( isLocalState(history.state()) );

    typename std::tr1::unordered_map<Ordinal,int>::const_iterator index =
	d_row_indexer.find( history.state() );
    Check( index != d_row_indexer.end() );

    // Sample the row CDF to get a new state.
    history.setState( 
	d_columns[index->second][ 
	    SamplingTools::sampleDiscreteCDF( d_cdfs[index->second](),
					      history.rng().random() ) ] );

    // Update the history weight with the transition weight.
    history.multiplyWeight( d_weights[index->second] );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Determine if a given state is on-process.
 */
template<class Vector, class Matrix>
inline bool AdjointDomain<Vector,Matrix>::isLocalState( const Ordinal& state )
{
   typename std::tr1::unordered_map<Ordinal,int>::const_iterator index =
       d_row_indexer.find( state );
   return ( index != d_row_indexer.end() );
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

//---------------------------------------------------------------------------//
// Template includes.
//---------------------------------------------------------------------------//

#include "MCLS_AdjointDomain_impl.hpp"

//---------------------------------------------------------------------------//

#endif // end MCLS_ADJOINTDOMAIN_HPP

//---------------------------------------------------------------------------//
// end MCLS_AdjointDomain.hpp
// ---------------------------------------------------------------------------//

