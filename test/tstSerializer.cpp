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
 * \file   tstSerializer.cpp
 * \author Stuart Slattery
 * \brief  Serializer class unit tests.
 */
//---------------------------------------------------------------------------//

#include <iostream>
#include <vector>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include <MCLS_config.hpp>
#include <MCLS_Serializer.hpp>

#include "Teuchos_UnitTestHarness.hpp"
#include "Teuchos_RCP.hpp"
#include "Teuchos_Array.hpp"
#include "Teuchos_DefaultComm.hpp"
#include "Teuchos_CommHelpers.hpp"
#include "Teuchos_as.hpp"

//---------------------------------------------------------------------------//
// HELPER FUNCTIONS
//---------------------------------------------------------------------------//

// Get the default communicator.
template<class Ordinal>
Teuchos::RCP<const Teuchos::Comm<Ordinal> > getDefaultComm()
{
#ifdef HAVE_MPI
    return Teuchos::DefaultComm<Ordinal>::getComm();
#else
    return Teuchos::rcp(new Teuchos::SerialComm<Ordinal>() );
#endif
}

//---------------------------------------------------------------------------//
// Basic Struct.
//---------------------------------------------------------------------------//
struct DataHolder
{
    double data;
};

//---------------------------------------------------------------------------//
// Tests.
//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( Serializer, size_test )
{
    bool data_bool = true;
    unsigned int data_uint = 1;
    int data_int = -4;
    float data_flt = -0.4332;
    double data_dbl = 3.2;
    DataHolder data_holder;

    std::size_t buffer_size = sizeof(bool) + sizeof(unsigned int) +
			      sizeof(int) + sizeof(float) +
			      sizeof(double) + sizeof(DataHolder);

    MCLS::Serializer serializer;
    serializer.computeBufferSizeMode();
    serializer.pack( data_bool );
    serializer.pack( data_uint );
    serializer.pack( data_int );
    serializer.pack( data_flt );
    serializer.pack( data_dbl );
    serializer.pack( data_holder );

    TEST_EQUALITY( serializer.size(), buffer_size );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( Serializer, stream_size_test )
{
    bool data_bool = true;
    unsigned int data_uint = 1;
    int data_int = -4;
    float data_flt = -0.4332;
    double data_dbl = 3.2;
    DataHolder data_holder;

    std::size_t buffer_size = sizeof(bool) + sizeof(unsigned int) +
			      sizeof(int) + sizeof(float) +
			      sizeof(double) + sizeof(DataHolder);

    MCLS::Serializer serializer;
    serializer.computeBufferSizeMode();
    serializer << data_bool << data_uint << data_int << data_flt 
	       << data_dbl << data_holder;

    TEST_EQUALITY( serializer.size(), buffer_size );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( Serializer, pack_unpack_test )
{
    bool data_bool = true;
    unsigned int data_uint = 1;
    int data_int = -4;
    float data_flt = -0.4332;
    double data_dbl = 3.2;
    DataHolder data_holder;
    double data_val = 2;
    data_holder.data = data_val;

    MCLS::Serializer serializer;
    serializer.computeBufferSizeMode();
    serializer << data_bool << data_uint << data_int << data_flt 
	       << data_dbl << data_holder;

    Teuchos::Array<char> buffer( serializer.size() );
    serializer.setBuffer( buffer.size(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.getPtr(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.begin(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.end(), buffer.getRawPtr()+serializer.size() );

    serializer << data_bool << data_uint << data_int << data_flt 
	       << data_dbl << data_holder;

    MCLS::Deserializer deserializer;
    deserializer.setBuffer( buffer.size(), buffer.getRawPtr() );

    bool ds_bool = false;
    unsigned int ds_uint = 0;
    int ds_int = 0;
    float ds_flt = 0.0;
    double ds_dbl = 0.0;
    DataHolder ds_holder;

    deserializer.unpack( ds_bool );
    TEST_EQUALITY( ds_bool, data_bool );

    deserializer.unpack( ds_uint );
    TEST_EQUALITY( ds_uint, data_uint );

    deserializer.unpack( ds_int );
    TEST_EQUALITY( ds_int, data_int );

    deserializer.unpack( ds_flt );
    TEST_EQUALITY( ds_flt, data_flt );

    deserializer.unpack( ds_dbl );
    TEST_EQUALITY( ds_dbl, data_dbl );

    deserializer.unpack( ds_holder );
    TEST_EQUALITY( ds_holder.data, data_val );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( Serializer, stream_pack_unpack_test )
{
    bool data_bool = true;
    unsigned int data_uint = 1;
    int data_int = -4;
    float data_flt = -0.4332;
    double data_dbl = 3.2;
    DataHolder data_holder;
    double data_val = 2;
    data_holder.data = data_val;

    MCLS::Serializer serializer;
    serializer.computeBufferSizeMode();
    serializer << data_bool << data_uint << data_int << data_flt 
	       << data_dbl << data_holder;

    Teuchos::Array<char> buffer( serializer.size() );
    serializer.setBuffer( buffer.size(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.getPtr(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.begin(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.end(), buffer.getRawPtr()+serializer.size() );

    serializer << data_bool << data_uint << data_int << data_flt 
	       << data_dbl << data_holder;

    MCLS::Deserializer deserializer;
    deserializer.setBuffer( buffer.size(), buffer.getRawPtr() );

    bool ds_bool = false;
    unsigned int ds_uint = 0;
    int ds_int = 0;
    float ds_flt = 0.0;
    double ds_dbl = 0.0;
    DataHolder ds_holder;

    deserializer >> ds_bool;
    TEST_EQUALITY( ds_bool, data_bool );

    deserializer >> ds_uint;
    TEST_EQUALITY( ds_uint, data_uint );

    deserializer >> ds_int;
    TEST_EQUALITY( ds_int, data_int );

    deserializer >> ds_flt;
    TEST_EQUALITY( ds_flt, data_flt );

    deserializer >> ds_dbl;
    TEST_EQUALITY( ds_dbl, data_dbl );

    deserializer >> ds_holder;
    TEST_EQUALITY( ds_holder.data, data_val );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( Serializer, view_pack_unpack_test )
{
    bool data_bool = true;
    unsigned int data_uint = 1;
    int data_int = -4;
    float data_flt = -0.4332;
    double data_dbl = 3.2;
    DataHolder data_holder;
    double data_val = 2;
    data_holder.data = data_val;

    MCLS::Serializer serializer;
    serializer.computeBufferSizeMode();
    serializer << data_bool << data_uint << data_int << data_flt 
	       << data_dbl << data_holder;

    Teuchos::Array<char> buffer( serializer.size() );
    serializer.setBuffer( buffer() );
    TEST_EQUALITY( serializer.getPtr(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.begin(), buffer.getRawPtr() );
    TEST_EQUALITY( serializer.end(), buffer.getRawPtr()+serializer.size() );

    serializer << data_bool << data_uint << data_int << data_flt 
	       << data_dbl << data_holder;

    MCLS::Deserializer deserializer;
    deserializer.setBuffer( buffer() );

    bool ds_bool = false;
    unsigned int ds_uint = 0;
    int ds_int = 0;
    float ds_flt = 0.0;
    double ds_dbl = 0.0;
    DataHolder ds_holder;

    deserializer >> ds_bool;
    TEST_EQUALITY( ds_bool, data_bool );

    deserializer >> ds_uint;
    TEST_EQUALITY( ds_uint, data_uint );

    deserializer >> ds_int;
    TEST_EQUALITY( ds_int, data_int );

    deserializer >> ds_flt;
    TEST_EQUALITY( ds_flt, data_flt );

    deserializer >> ds_dbl;
    TEST_EQUALITY( ds_dbl, data_dbl );

    deserializer >> ds_holder;
    TEST_EQUALITY( ds_holder.data, data_val );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( Serializer, broadcast_test )
{
    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    int comm_rank = comm->getRank();

    bool data_bool = true;
    unsigned int data_uint = 1;
    int data_int = -4;
    float data_flt = -0.4332;
    double data_dbl = 3.2;
    DataHolder data_holder;
    double data_val = 2;
    data_holder.data = data_val;

    std::size_t buffer_size = sizeof(bool) + sizeof(unsigned int) +
			      sizeof(int) + sizeof(float) +
			      sizeof(double) + sizeof(DataHolder);

    Teuchos::Array<char> buffer( buffer_size );

    if ( comm_rank == 0 )
    {
	MCLS::Serializer serializer;
	serializer.setBuffer( buffer.size(), buffer.getRawPtr() );
	serializer << data_bool << data_uint << data_int << data_flt 
		   << data_dbl << data_holder;
    }
    comm->barrier();

    Teuchos::broadcast( *comm, 0, buffer() );

    MCLS::Deserializer deserializer;
    deserializer.setBuffer( buffer.size(), buffer.getRawPtr() );

    bool ds_bool = false;
    unsigned int ds_uint = 0;
    int ds_int = 0;
    float ds_flt = 0.0;
    double ds_dbl = 0.0;
    DataHolder ds_holder;

    deserializer >> ds_bool;
    TEST_EQUALITY( ds_bool, data_bool );

    deserializer >> ds_uint;
    TEST_EQUALITY( ds_uint, data_uint );

    deserializer >> ds_int;
    TEST_EQUALITY( ds_int, data_int );

    deserializer >> ds_flt;
    TEST_EQUALITY( ds_flt, data_flt );

    deserializer >> ds_dbl;
    TEST_EQUALITY( ds_dbl, data_dbl );

    deserializer >> ds_holder;
    TEST_EQUALITY( ds_holder.data, data_val );
}

//---------------------------------------------------------------------------//
TEUCHOS_UNIT_TEST( Serializer, view_broadcast_test )
{
    Teuchos::RCP<const Teuchos::Comm<int> > comm = 
	Teuchos::DefaultComm<int>::getComm();
    int comm_rank = comm->getRank();

    bool data_bool = true;
    unsigned int data_uint = 1;
    int data_int = -4;
    float data_flt = -0.4332;
    double data_dbl = 3.2;
    DataHolder data_holder;
    double data_val = 2;
    data_holder.data = data_val;

    std::size_t buffer_size = sizeof(bool) + sizeof(unsigned int) +
			      sizeof(int) + sizeof(float) +
			      sizeof(double) + sizeof(DataHolder);

    Teuchos::Array<char> buffer( buffer_size );

    if ( comm_rank == 0 )
    {
	MCLS::Serializer serializer;
	serializer.setBuffer( buffer() );
	serializer << data_bool << data_uint << data_int << data_flt 
		   << data_dbl << data_holder;
    }
    comm->barrier();

    Teuchos::broadcast( *comm, 0, buffer() );

    MCLS::Deserializer deserializer;
    deserializer.setBuffer( buffer() );

    bool ds_bool = false;
    unsigned int ds_uint = 0;
    int ds_int = 0;
    float ds_flt = 0.0;
    double ds_dbl = 0.0;
    DataHolder ds_holder;

    deserializer >> ds_bool;
    TEST_EQUALITY( ds_bool, data_bool );

    deserializer >> ds_uint;
    TEST_EQUALITY( ds_uint, data_uint );

    deserializer >> ds_int;
    TEST_EQUALITY( ds_int, data_int );

    deserializer >> ds_flt;
    TEST_EQUALITY( ds_flt, data_flt );

    deserializer >> ds_dbl;
    TEST_EQUALITY( ds_dbl, data_dbl );

    deserializer >> ds_holder;
    TEST_EQUALITY( ds_holder.data, data_val );
}

//---------------------------------------------------------------------------//
// end tstSerializer.cpp
//---------------------------------------------------------------------------//
