
/* Copyright (c) 2011, Stefan Eilemann <eile@eyescale.ch> 
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *  
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "module.h"

#include <gpusd1/gpuInfo.h>
#include <dns_sd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <arpa/inet.h>
#include <sys/time.h>

#define WAIT_TIME 500 // ms

namespace gpusd
{
namespace dns_sd
{
namespace
{

Module* instance = 0;

static bool handleEvent( DNSServiceRef service )
{
    const int fd = DNSServiceRefSockFD( service );
    const int nfds = fd + 1;

    fd_set fdSet;
    FD_ZERO( &fdSet );
    FD_SET( fd, &fdSet );

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = WAIT_TIME * 1000;

    const int result = select( nfds, &fdSet, 0, 0, &tv );
    switch( result )
    {
      case 0: // timeout
          return false;

      default:
          DNSServiceProcessResult( service );
          return true;

      case -1:
          std::cerr << "Select error: " << strerror( errno ) << " (" 
                    << errno << ")" << std::endl;
          return false;
    }
}

template< class T >
bool getTXTRecordValue( const uint16_t txtLen, const unsigned char* txt,
                        const std::string& name, T& result )
{
    uint8_t len = 0;
    const char* data = (const char*)TXTRecordGetValuePtr( txtLen, txt,
                                                          name.c_str(), &len );
    if( !data || len == 0 )
        return false;

    std::istringstream in( std::string( data, len ));
    in >> result;
    return true;
}


static void resolveCallback( DNSServiceRef service, DNSServiceFlags flags,
                             uint32_t interface, DNSServiceErrorType error,
                             const char* name, const char* host, uint16_t port,
                             uint16_t txtLen, const unsigned char* txt,
                             void* context )
{
    if( error != kDNSServiceErr_NoError)
    {
        std::cerr << "Resolve callback error: " << error << std::endl;
        return;
    }

    unsigned nGPUs = 0;
    getTXTRecordValue( txtLen, txt, "GPU Count", nGPUs );
    if( !nGPUs )
        return;

    GPUInfos* result = reinterpret_cast< GPUInfos* >( context );
    for( unsigned i = 0; i < nGPUs; ++i )
    {
        std::ostringstream out;
        out << "GPU" << i << " ";
        const std::string& gpu = out.str();

        std::string type;
        if( !getTXTRecordValue( txtLen, txt, gpu + "Type", type ))
            continue;

        GPUInfo info( type );
        info.hostname = host;
        getTXTRecordValue( txtLen, txt, "Session", info.session );
        getTXTRecordValue( txtLen, txt, gpu + "Port", info.port );
        getTXTRecordValue( txtLen, txt, gpu + "Device", info.device );
        getTXTRecordValue( txtLen, txt, gpu + "X", info.pvp[0] );
        getTXTRecordValue( txtLen, txt, gpu + "Y", info.pvp[1] );
        getTXTRecordValue( txtLen, txt, gpu + "Width", info.pvp[2] );
        getTXTRecordValue( txtLen, txt, gpu + "Height", info.pvp[3] );
        result->push_back( info );
    }
}


static void browseCallback( DNSServiceRef service, DNSServiceFlags flags,
                            uint32_t interface, DNSServiceErrorType error,
                            const char* name, const char* type,
                            const char* domain, void* context )
{
    if( error != kDNSServiceErr_NoError)
    {
        std::cerr << "Browse callback error: " << error << std::endl;
        return;
    }

    if( !( flags & kDNSServiceFlagsAdd ))
        return;

    error = DNSServiceResolve( &service, 0, interface, name, type, domain,
                               resolveCallback, context );
    if( error != kDNSServiceErr_NoError)
    {
        std::cerr << "DNSServiceResolve error: " << error << std::endl;
        return;
    }

    GPUInfos* result = reinterpret_cast< GPUInfos* >( context );
    const size_t old = result->size();

    while( old == result->size() && handleEvent( service ))
        /* nop */;
}

}

void Module::use()
{
    if( !instance )
        instance = new Module;
}

GPUInfos Module::discoverGPUs_() const
{
    DNSServiceRef service;
    GPUInfos candidates;
    const DNSServiceErrorType error = DNSServiceBrowse( &service, 0, 0,
                                                        "_gpu-sd._tcp", "",
                                                        browseCallback,
                                                        &candidates );
    if( error == kDNSServiceErr_NoError )
    {
        while( handleEvent( service ))
            /* nop */;
        DNSServiceRefDeallocate( service );
    }
    else
        std::cerr << "DNSServiceDiscovery error: " << error << std::endl;

    // eliminate duplicates
    GPUInfos result;
    for( GPUInfosCIter i = candidates.begin(); i != candidates.end(); ++i )
    {
        const GPUInfo& info = *i;
        if( std::find( result.begin(), result.end(), info ) == result.end( ))
            result.push_back( info );
    }
    return result;
}

}
}
