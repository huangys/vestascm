// Copyright (C) 2001, Compaq Computer Corporation
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

//
// VestaLogPrivate.H
//
// Log changes to the repository state
// Portion of VestaLog that is private to the implementation but is
//  needed by the compiler to build clients.
//

#ifndef _VLOGP
#define _VLOGP 1

#include "Basics.H"
#include <netinet/in.h>

static const unsigned int DiskBlockSize = 512;

namespace VLog
{
  // Physical block number.  We use a 64-bit integer to make sure we
  // support large logs
  typedef Basics::uint64 phy_t;

  // Block sequence number.
  typedef Basics::uint32 seq_t;

  // Block length.
  typedef Basics::uint16 len_t;
}

struct VLogBlock {
    VLogBlock *next;
    VLog::phy_t phy;      // "physical" block number (address in file)
    VLog::phy_t pocketPhy;// reading: phy # of block left in pocket after
                           //  this one was sequenced.
                           // writing: phy # of unused block in pocket
    bool tailCommitted;  // during reading, true if the bytes beyond
                            //  len are known to be committed
    struct data_block {
	// Both seq and len are always in big-endian order.
	VLog::seq_t seq_;  // hashed sequence number of this logical block
	VLog::len_t len_;  // (offset to end of last record in this block, or
	             //  0 if no record ends in this block) +
                     // (2 bit block version number in high-order bits)
	char bytes[DiskBlockSize - 6];

	inline VLog::seq_t getSeq() throw()
          { return Basics::ntoh32(seq_); };
	inline VLog::len_t getLen() throw()
          { return Basics::ntoh16(len_) & 0x3FFF; };
	inline VLog::len_t getVer() throw()
          { return (Basics::ntoh16(len_) >> 14) & 0x0003; };
	inline void setSeq(VLog::seq_t seq) throw()
          { seq_ = Basics::hton32(seq); };
	inline void setLen(VLog::len_t len) throw()
          { len_ = Basics::hton16(len | (getVer() << 14)); };
	inline void setVer(VLog::len_t ver) throw()
          { len_ = Basics::hton16(getLen() | ((ver & 0x0003) << 14)); };
    } *data;

  VLogBlock()
  {
    data = NEW_PTRFREE(data_block);
#if defined(VALGRIND_SUPPORT)
    memset((void *) data, 0, sizeof(data_block));
#endif
  }
  ~VLogBlock()
  {
    delete data;
    data = NULL;  // Help out the garbage collecotr
  }
};


struct VestaLogPrivate {
    enum State { initial=0, recovering, recovered, ready, logging, bad };
    bool checkpointing;
    bool readonly;
    bool bakckp;     // true if backing up checkpoints
    bool hitEOF;     // reading: true if we got an end-of-file
    bool usePocket;  // writing: true if we should write to pocket next
    bool commUsePocket;  // writing: usePocket after last commit
    int nesting;     // if state==logging, how deeply starts are nested.
    State state;
    char* directory;  // directory containing log and checkpoint files
    char* directory2; // dir containing backup of log (and ckp), or NULL
    int version;     // log version currently open
    int ccVersion;   // highest committed checkpoint
    int ccVersion2;  // highest committed checkpoint in backup (if any)
    int fd;      // log file descriptor
    int fd2;     // log file descriptor for backup, -1 if none
    int lockfd;  // lock file descriptor
    int lockfd2; // lock file descriptor for backup
    VLog::seq_t curSeq;  // seq number of first block in cur chain
    VLog::len_t curLen;  // offset of next byte in buf->bytes to read or write
    VLog::seq_t nextSeq; // reading: seq number of next block not yet sequenced
    VLog::phy_t nextPhy; // reading: phy address of next block not yet read
    VLog::seq_t commSeq;        // writing: curSeq of last block written to commit
    VLog::phy_t commPhy;        // writing: cur->phy after last commit
    VLog::phy_t commPocketPhy;  // writing: cur->pocketPhy after last commit
    VLogBlock* cur;  // reading: chain of blocks already read and sequenced
                    // writing: buffer for block being assembled
    VLogBlock* last; // reading: last block in cur chain, NULL if none
    VLogBlock* pocket;   // reading: holds saved out-of-order block
    VLogBlock* free;     // free list

    inline VLogBlock* balloc() throw() {
	if (free) {
	    VLogBlock* b = free;
	    free = free->next;
	    b->next = NULL;  // just to be cautious
	    return b;
	} else {
	  return NEW(VLogBlock);
	}
    };
    inline void bfree(VLogBlock *b) throw() {
	b->next = free;
	free = b;
    };
    void extendCur()         // extend the cur chain by one block
      throw(VestaLog::Eof, VestaLog::Error);
    void makeBytesAvail()    // ensure bytes to read in cur
      throw(VestaLog::Eof, VestaLog::Error);
    void writeCur()          // write out cur block
      throw(VestaLog::Error);
    void makeSpaceAvail()    // write full cur block and empty cur
      throw(VestaLog::Error);
    void eraseUncommitted(int fd)  // write invalid data to unused blocks
      throw(VestaLog::Error);
    VLogBlock* readBlock()   // read next block, return NULL if EOF
      throw(VestaLog::Error);
};

#endif //_VLOGP
