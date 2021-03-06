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
 * \file MCLS_ForwardHistory_impl.hpp
 * \author Stuart R. Slattery
 * \brief ForwardHistory class declaration.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_FORWARDHISTORY_IMPL_HPP
#define MCLS_FORWARDHISTORY_IMPL_HPP

#include <algorithm>

#include "MCLS_DBC.hpp"
#include "MCLS_Serializer.hpp"

#include <Teuchos_as.hpp>

namespace MCLS
{
//---------------------------------------------------------------------------//
/*!
 * \brief Deserializer constructor.
 */
template<class Ordinal>
ForwardHistory<Ordinal>::ForwardHistory( 
    const Teuchos::ArrayView<char>& buffer )
{
    MCLS_REQUIRE( Teuchos::as<std::size_t>(buffer.size()) == d_packed_bytes );
    Deserializer ds;
    ds.setBuffer( buffer );
    this->unpackHistory( ds );
    ds >> d_starting_state >> d_history_tally;
    MCLS_ENSURE( ds.getPtr() == ds.end() );
}

//---------------------------------------------------------------------------//
/*!
 * \brief Pack the history into a buffer.
 */
template<class Ordinal>
Teuchos::Array<char> ForwardHistory<Ordinal>::pack() const
{
    MCLS_REQUIRE( d_packed_bytes );
    MCLS_REQUIRE( d_packed_bytes > 0 );
    Teuchos::Array<char> buffer( d_packed_bytes );
    Serializer s;
    s.setBuffer( buffer() );
    this->packHistory( s );
    s << d_starting_state << d_history_tally;
    MCLS_ENSURE( s.getPtr() == s.end() );
    return buffer;
}

//---------------------------------------------------------------------------//
// Static members.
//---------------------------------------------------------------------------//
template<class Ordinal>
std::size_t ForwardHistory<Ordinal>::d_packed_bytes = 0;

//---------------------------------------------------------------------------//
/*!
 * \brief Set the byte size of the packed history state.
 */
template<class Ordinal>
void ForwardHistory<Ordinal>::setByteSize()
{
    Base::setStaticSize();
    d_packed_bytes = Base::getStaticSize() + sizeof(Ordinal) + sizeof(double);
}

//---------------------------------------------------------------------------//
/*!
 * \brief Get the number of bytes in the packed history state.
 */
template<class Ordinal>
std::size_t ForwardHistory<Ordinal>::getPackedBytes()
{
    MCLS_REQUIRE( d_packed_bytes );
    return d_packed_bytes;
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

//---------------------------------------------------------------------------//

#endif // end MCLS_FORWARDHISTORY_IMPL_HPP

//---------------------------------------------------------------------------//
// end MCLS_ForwardHistory_impl.hpp
//---------------------------------------------------------------------------//

