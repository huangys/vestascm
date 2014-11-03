// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// TestOS -- tests the OS interface utility routines

#include <strings.h>
#include <netdb.h>
#include <Basics.H>
#include "OS.H"
#include "FS.H"

using std::cout;
using std::endl;

int main()
{
  static struct hostent *hp;
  char hostname[MAXHOSTNAMELEN+1];
  sockaddr_in hostsock;

  if (gethostname(hostname, sizeof(hostname)-1) == SYSERROR)
    return errno;

  cout << "My hostname is " << hostname << "\n";

  if ((hp = gethostbyname(hostname)) == NULL)
    return errno;

  bzero((char *)&hostsock, sizeof(hostsock));
  memcpy((char *)&(hostsock.sin_addr.s_addr), hp->h_addr, hp->h_length);
  endhostent();

  cout << "My IP address is " << inet_ntoa_r(hostsock.sin_addr) << "\n";

  // Note: The tests of FS::Exists/IsDirectory/IsFile are a little
  // UNIX-specific

  if(FS::IsDirectory("/"))
    {
      cout << "FS::IsDirectory seems to work" << endl;
    }
  else
    {
      cout << "FS::IsDirectory(\"/\") returned false" << endl;
      return errno || 1;
    }

  if(FS::IsFile("/etc/passwd"))
    {
      cout << "FS::IsFile seems to work" << endl;
    }
  else
    {
      cout << "FS::IsFile(\"/etc/passwd\") returned false" << endl;
      return errno || 1;
    }

  if(FS::Exists("/dev/null") && !FS::IsDirectory("/dev/null") && !FS::IsFile("/dev/null"))
    {
      cout << "FS::Exists seems to work" << endl;
    }
  else
    {
      cout << "Problem with FS::Exists/IsDirectory/IsFile test on /dev/null" << endl;
      return errno || 1;
    }

  unsigned long total, resident;
  OS::GetProcessSize(total, resident);
  cout << "Total process size:    " << total << endl
       << "Resident process size: " << resident << endl;

  return 0;
}

  
