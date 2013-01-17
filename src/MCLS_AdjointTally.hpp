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
 * \file MCLS_AdjointTally.hpp
 * \author Stuart R. Slattery
 * \brief AdjointTally declaration.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_ADJOINTTALLY_HPP
#define MCLS_ADJOINTTALLY_HPP

#include <MCLS_DBC.hpp>
#include <MCLS_History.hpp>
#include <MCLS_VectorExport.hpp>
#include <MCLS_VectorTraits.hpp>

#include <Teuchos_RCP.hpp>

namespace MCLS
{

//---------------------------------------------------------------------------//
/*!
 * \class AdjointTally
 * \brief Monte Carlo tally for the linear system solution vector for adjoint
 * problems. 
 */
template<class Vector>
class AdjointTally
{
  public:

    //@{
    //! Typedefs.
    typedef Vector                                              vector_type;
    typedef VectorTraits<Vector>                                VT;
    typedef typename VT::scalar_type                            Scalar;
    typedef typename VT::global_ordinal_type                    Ordinal;
    typedef History<Scalar,Ordinal>                             HistoryType;
    //@}

    // Constructor.
    AdjointTally( const Teuchos::RCP<Vector>& x, 
		  const Teuchos::RCP<Vector>& x_overlap );

    // Destructor.
    ~AdjointTally()
    { /* ... */ }

    // Add a history's contribution to the tally.
    inline void tallyHistory( const HistoryType& history );

    // Combine the overlap tally with the base decomposition tally.
    void combineTallies();

    // Normalize base decomposition tallies with the number of specified
    // histories.
    void normalize( const int& nh );

  private:

    // Solution vector in base decomposition.
    Teuchos::RCP<Vector> d_x;

    // Solution vector in overlap decomposition.
    Teuchos::RCP<Vector> d_x_overlap;

    // Overlap to base decomposition vector export.
    VectorExport<Vector> d_export;
};

//---------------------------------------------------------------------------//
// Inline functions.
//---------------------------------------------------------------------------//
/*
 * \brief Add a history's contribution to the tally.
 */
template<class Vector>
inline void AdjointTally<Vector>::tallyHistory( const HistoryType& history )
{
    Require( history.alive() );
    Require( VT::isGlobalRow( *d_x, history.state() ) ||
	     VT::isGlobalRow( *d_x_overlap, history.state() ) );

    if ( VT::isGlobalRow( *d_x, history.state() ) )
    {
	VT::sumIntoGlobalValue( *d_x, history.state(), history.weight() );
    }

    else if ( VT::isGlobalRow( *d_x_overlap, history.state() ) )
    {
	VT::sumIntoGlobalValue( 
	    *d_x_overlap, history.state(), history.weight() );
    }

    else
    {
	Insist( VT::isGlobalRow( *d_x, history.state() ) ||
		VT::isGlobalRow( *d_x_overlap, history.state() ),
		"History state is not local to tally!" );
    }
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

//---------------------------------------------------------------------------//
// Template includes.
//---------------------------------------------------------------------------//

#include "MCLS_AdjointTally_impl.hpp"

//---------------------------------------------------------------------------//

#endif // end MCLS_ADJOINTTALLY_HPP

//---------------------------------------------------------------------------//
// end MCLS_AdjointTally.hpp
// ---------------------------------------------------------------------------//

