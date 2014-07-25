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
 * \file MCLS_PRNG_impl.hpp
 * \author Stuart R. Slattery
 * \brief Parallel random number generator class implementation.
 */
//---------------------------------------------------------------------------//

#ifndef MCLS_PRNG_IMPL_HPP
#define MCLS_PRNG_IMPL_HPP

#include <random>

namespace MCLS
{
//---------------------------------------------------------------------------//
/*!
 * \brief Constructor.
 */
template<class RNG>
PRNG<RNG>::PRNG( const int comm_rank)
{
    // Create a random device to get an initial random number.
    std::random_device rand_device;

    // Create a master rng to produce seed values for each parallel rank.
    RNG master_rng( rand_device() );
    typename RNG::result_type seed = 0;
    for ( int i = 0; i < comm_rank; ++i )
    {
	seed = master_rng();
    }

    // Seed the random number generator on this process with the appropriate
    // seed.
    d_rng = rng_type( seed );
}

//---------------------------------------------------------------------------//

} // end namespace MCLS

#endif // end MCLS_PRNG_IMPL_HPP

//---------------------------------------------------------------------------//
// end MCLS_PRNG_impl.hpp
//---------------------------------------------------------------------------//

