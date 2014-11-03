// Copyright (C) 2006, Kenneth C. Schalk
// 
// This file is part of Vesta.
// 
// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#ifndef _NETADDRESS_H
#define _NETADDRESS_H

#include <SRPC.H>

#include <string.h>
#include <assert.h>

// A network address that can be used in an STL set, as a key in an
// STL map, or as a key in a Table

struct NetAddress
{
  struct sockaddr_in address;

  NetAddress()
  {
    memset((char *) &address, 0, sizeof(address));
    address.sin_family = AF_INET;
  }

  NetAddress(const struct sockaddr_in a)
  {
    assert(a.sin_family == AF_INET);
    memcpy((char *) &address, (char *) &a, sizeof(address));
  }

  const NetAddress &operator=(const NetAddress &other)
  {
    assert(other.address.sin_family == AF_INET);
    memcpy((char *) &address, (char *) &other.address, sizeof(address));
    return *this;
  }

  // Needed so we can use it in STL sets
  bool operator<(const NetAddress &other) const
  {
    assert(address.sin_family == AF_INET);
    // When addresses are equal, compare ports
    if(address.sin_addr.s_addr == other.address.sin_addr.s_addr)
      return address.sin_port < other.address.sin_port;
    // Otherwise, compare addresses
    return address.sin_addr.s_addr < other.address.sin_addr.s_addr;
  }

  bool operator==(const NetAddress &other) const
  {
    return ((address.sin_addr.s_addr == other.address.sin_addr.s_addr) &&
	    (address.sin_port == other.address.sin_port));
  }

  bool operator!=(const NetAddress &other) const
  {
    return !(*this == other);
  }

  void send(SRPC &s) const throw(SRPC::failure)
  {
    struct sockaddr_in a;
    memcpy((char *) &a, (char *) &address, sizeof(address));
    s.send_socket(a);
  }

  void recv(SRPC &s) throw(SRPC::failure)
  {
    this->address = s.recv_socket();
  }

  // Connect to this address as an SRPC server
  SRPC *connect_to() const throw(SRPC::failure)
  {
    struct sockaddr_in a;
    memcpy((char *) &a, (char *) &address, sizeof(address));
    TCP_sock *sock = 0;
    try
      {
	sock = NEW(TCP_sock);
	sock->connect_to(a);
      }
    catch (TCP_sock::failure f)
      {
	if(sock != 0) delete sock;
	throw SRPC::convert_TCP_failure(f);
      }
    return NEW_CONSTR(SRPC, (SRPC::caller, sock));
  }

  Text host() const throw()
  {
    struct sockaddr_in a;
    memcpy((char *) &a, (char *) &address, sizeof(address));
    return inet_ntoa_r(a.sin_addr);
  }

  Text port() const throw()
  {
    Basics::uint16 port = ntohs(address.sin_port);
    return Text::printf("%hu", port);
  }

  void print(std::ostream &out) const throw()
  {
    out << host() << ":" << port();
  }

  Word Hash() const throw ()
  {
    // We only handle IPv4 today
    assert(address.sin_family == AF_INET);
    assert((sizeof(address.sin_port) +
	    sizeof(address.sin_addr.s_addr)) < sizeof(Word));
    Word result = Basics::ntoh32(address.sin_addr.s_addr);
    result <<= (sizeof(address.sin_port) * 8);
    result |= Basics::ntoh16(address.sin_port);
    return result;
  }
};

inline std::ostream &operator<<(std::ostream &out, const NetAddress &addr)
{
  addr.print(out);
  return out;
}

#endif
