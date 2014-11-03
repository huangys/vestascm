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
// VestaLog.H
//
// Log changes to the repository state
//

#ifndef _VLOG
#define _VLOG

#include "Basics.H"
#include "Text.H"
#include <fstream>

// INTRODUCTION

// A VestaLog is a persistent bytestream with atomic append.
// Checkpointing and recovery features are also provided.  

// The log does not maintain record boundaries; it is up to the
// client to write its data in a format that can be read back later.

// VestaLog provides no locking of any kind, not even to protect its
// own data structures; clients are responsible for this.  Only one
// client thread should use the log at a time.

// ATOMIC APPEND

// After calling start, a client can tentatively append bytes, then
// either commit (atomically make the append permanent), or abort
// (return the log to its state at the time of the start).  A crash
// cleanly aborts any ongoing append.  A client can call start
// multiple times before calling commit or abort: start increments a
// counter that is decremented by commit, and the bytes are committed
// only when the counter reaches zero.  One abort satisfies all
// pending starts, however.

// CHECKPOINTING

// A checkpoint is a file that is effectively created (including its
// complete contents) atomically.  The checkpointBegin operation opens
// a new checkpoint for writing, while checkpointEnd marks the
// checkpoint as committed.  If there is a crash before checkpointEnd
// is called, the uncommitted checkpoint is not seen at recovery time.
// The checkpointAbort operation also marks the current checkpoint as
// uncommitted; it allows you to change your mind about writing a
// checkpoint.

// At checkpoint time, logging switches to a new, empty log file.
// This supports recovering from the most recent checkpoint plus the
// log records written since the checkpoint.  To support fuzzy
// checkpointing (that is, checkpointing in parallel with logging new
// updates), the switch to a new log file is made at checkpointBegin
// time, and it is permitted to write log records between
// checkpointBegin and checkpointEnd.  This creates an extra
// complication if there is a crash during checkpointing: the log
// records written since the last committed checkpoint may be spread
// across multiple log files.  Handling this situation during recovery
// is discussed below.

// By default, if there is a crash during checkpointing, the
// checkpoint that was in progress is aborted.  Alternatively, the
// checkpointResume method lets you resume writing the same checkpoint
// you were working on before the crash.

// Committing a new checkpoint does not delete old logs or
// checkpoints.  If desired, this deletion can be done by calling the
// prune method.  Alternatively, the old data can be retained as a
// backup against damaged or corrupt checkpoints; you can choose to
// recover from an older committed checkpoint plus the log records
// written since, or even entirely from log records.

// RECOVERY

// When a log is first opened, it is in "recovering" state, ready for
// reading.  By default, open arranges to begin reading at the start
// of the log file associated with the most recent committed
// checkpoint, or from the 0th log file if there have been no
// checkpoints as yet.  The client can begin recovery at an earlier
// checkpoint by passing the checkpoint's version number to the open
// method (see below).

// The openCheckpoint method gets the checkpoint file (if any)
// associated with the current log file.  Normally you will call this
// method once, immediately after opening the log, in order to process
// the checkpoint itself before processing the subsequent log records.

// The reading methods read bytes from the log.  They throw Eof when
// there are no more records in the current log file.  At this point
// the client must call nextLog in case there are more log files.  If
// nextLog returns true, the client must continue calling reading
// methods to read the rest of the log; if false, it may call
// loggingBegin to enter normal operation and begin appending new
// records to the log.  

// Pseudocode for recovery:
//
// VestaLog log;
// log.open(".");
// fstream* ckp = log.openCheckpoint();
// // Restore state from ckp
// ...
// ckp.close();
// // Apply log records beyond ckp
// for (;;) {
//   try {
//     char c;
//     log.get(c); // or other reading method
//     ...
//   } catch (VestaLog::Eof) {
//     if (!log.nextLog()) break;
//   }
// }
// log.loggingBegin();

// READING THE LOG DURING CHECKPOINTING

// If you need to read the old log in order to construct a checkpoint,
// but you want to avoid reading new records written after you started
// the checkpoint, use the logVersion method to determine where to
// stop.  This method returns the version number of the log file that
// is currently open.  The version number increases after a successful
// call to nextLog or a call to checkpointBegin.  Call logVersion on
// the VestaLog object you are writing to, immediately after calling
// checkpointBegin, and let lv be the value returned.  Open the same
// log again and read from it in the normal way, but stop as soon as
// logVersion returns lv.

// Pseudocode for reading the log during checkpointing:
//
// VestaLog wl;  // log object being written to
// ...
// fstream* ckp = wl.checkpointBegin();
// int lv = wl.logVersion();
// VestaLog rl;  // same log, object being read
// rl.open(".", -1, true);
// for (;;) {
//   try {
//     // read from rl, write to ckp
//   } catch (VestaLog::Eof) {
//     bool more = rl.nextLog();
//     assert(more);  // there is always >= 1 to read, plus one to ignore
//     if (rl.logVersion() >= lv) break;
//   }
// }
// rl.close();
// ckp.close();
// wl.checkpointEnd();

// FINDING OLD CHECKPOINTS AND LOGS

// Currently no methods are provided to determine which old
// checkpoint version numbers are committed (and thus useful to pass
// as the optional second argument to open).  One can get this
// information by examining the directory where the package stores its
// files.  Files are named as follows, where # is a decimal digit
// string:

// - version contains the highest checkpoint number that is committed.
// - version.new is an auxiliary file used in atomic checkpointing.
// - pruned contains the highest checkpoint number that has been pruned.
// - pruned.new is an auxiliary file used in pruning.
// - #.log is the log beginning at the #.ckp checkpoint.
// - The first log is 0.log, and there is no 0.ckp checkpoint.
// - If #.ckp is present and # <= version, then #.ckp is a committed
//    checkpoint.  Any #.ckp files with # > version must be ignored. 

// LOG AND CHECKPOINT ONLINE BACKUP

// You can have the log (and optionally also checkpoints) written to a
// second directory, as an on-line backup against damaged or lost
// files.  If log backup is on, all writes go to both logs, and
// commit() does not return until both logs are synced.  During
// recovery, both logs are read in parallel.  Any data that did not
// reach both logs is considered uncommitted and ignored.  When
// loggingBegin is called, the backup log is made identical to the
// primary log.

// (A subtle consequence of this algorithm is that you may see a
// shorter log when recovering from both primary and backup than if
// one of the logs is lost and you recover from the other alone.  This
// will happen if a commit() was in progress at the time of the last
// crash and data up to the commit point had been fully written to one
// log but not the other.)

// (!!It should be possible to speed up recovery a bit by reading from
// only one log except for the portion between the last two commit
// points, but this would complicate the recovery code and has not
// been implemented.)

// No methods are currently provided to deal with the loss of a log or
// to copy a log to make a new backup.  If the specified primary and
// backup logs are not both available at recovery time, open() will
// raise the Error exception.  To fix the problem, you will have to
// determine which log directory is intact (the message printed with
// the Error exception will help here) and manually copy it to the
// other log directory.  A log directory must be copied only when
// the log is not open for writing.  If one log appears to be lost and
// you choose to recover from the other, it is important to (1) copy
// the remaining log before starting recovery so that you are not
// running with only one, and (2) make certain that the "lost" log
// cannot be found later and inadvertently used (either alone or as
// part of a primary/backup pair), since it will now be outdated and
// incorrect.

// If you ask for checkpoints to be backed up, the checkpointEnd
// method makes a copy of the checkpoint file and stores it in
// the backup log directory, then updates the version files in both 
// directories, before returning.  The backup checkpoints are never
// read in normal operation; they are kept only in case you ever lose
// the primary and need to promote the backup log directory to
// primary. 

class VestaLogPrivate;

class VestaLog {
  public:
    class Exception {};

   // Hit end of current log file
    class Eof : public Exception {};

   // Log format error or I/O error.  When Error is thrown, the log
   // enters state "bad".  Only the close method is valid in this state.
    class Error : public Exception {
      public:
	int r; // OS errno, or 0 if not an OS error
	Text msg;
	inline Error() { };
	inline Error(int r, const Text &msg) {
	    this->r = r; this->msg = msg; };
	inline Error(const Error &f) {
	    this->r = f.r; this->msg = f.msg; };
	inline Error& operator=(const Error &f) {
	    this->r = f.r; this->msg = f.msg; return *this; };
    };

    VestaLog() throw();

    // Open a log.  State: initial -> recovering & !checkpointing.
    // dir = directory holding the log files.  ver = checkpoint
    // version to start at; -1 means the latest version with a
    // committed checkpoint, or 0 if there are no committed
    // checkpoints.  readonly = true for read-only access.
    // If lock == true, get an advisory lock on a file in the lock
    // directory (a read lock if readonly, else a write lock), 
    // throwing an error if there is a lock conflict.
    // If dir2 != NULL, write a backup of the log in the
    // given directory.  If bakckp == true, also backup checkpoint
    // files in dir2.  dir2 must != NULL if bakckp is true.
    void open(char *dir, int ver =-1, bool readonly =false,
	      bool lock =false, char* dir2 =NULL, bool bakckp =false)
      throw(Error);
         				   
    // Return the version number of the logfile that is currently
    // open, and (if checkpointing) the checkpoint that is currently
    // being written.  This number increases when nextLog returns true
    // or checkpointBegin is called.  State: !initial & !bad.
    int logVersion() throw(Error);

    // Open (for reading) the checkpoint that the current log file
    // starts from.  Returns NULL if 0.log is current.  Throws Error
    // if the checkpoint associated with the current log file was not
    // committed.  State: recovering.
    std::fstream *openCheckpoint() throw(Error);

    // Read one character. State: recovering.
    void get(char& c) throw(Eof, Error);	       

    // Read n bytes, or read up to (but not including) the next
    // "term" character, whichever is less.  State: recovering.
    void get(char* p, int n, char term='\n') throw(Eof, Error);

    // Read n bytes or read up to Eof, whichever is less.  Return
    // number of bytes read.  State: recovering.
    int read(char* p, int n) throw(Error);              
                                 
    // Read exactly n bytes.  State: recovering.
    void readAll(char* p, int n) throw(Eof, Error);	       

    // Test for end of current log file.  State: recovering.
    bool eof() throw(Error);

    // Begin reading log records from the next log file.  Before
    // calling this method, you must have read up to Eof with get or
    // read.  Returns true if there is a next log file and leaves the
    // log object in recovering state; otherwise returns false and
    // leaves the object in recovered state.  State: recovering ->
    // (recovering | recovered).
    bool nextLog() throw(Error); 

    // Ready to start logging.  (Even if you know in advance that the
    // log is empty, you must get into the recovered state before
    // calling loggingBegin(), by calling get, read, readAll, or eof
    // at least once, then calling nextLog() once.)  Illegal if you
    // called open with readonly=true.  State: recovered -> ready.
    void loggingBegin() throw(Error);

    // If the current thread has already called start(), assert state
    // == logging and increment the nesting level of starts.
    // Otherwise set nesting = 1, state = logging, and start a record.
    // State: (ready | logging) -> logging.
    void start() throw(Error);

    // Return the nesting level of starts.  State: *
    int nesting() throw();

    // Write one character.  State: logging.
    void put(char c) throw(Error);

    // Write a NUL-terminated string.  State: logging.
    void put(const char *p) throw(Error);

    // Write n bytes.  State: logging.
    void write(const char *p, int n) throw(Error);

    // Commit the current record if --nesting == 0.
    // State: logging -> (--nesting == 0) ? ready : logging.
    void commit() throw(Error);              

    // Abort the current record and set nesting = 0.  
    // State: logging -> ready.
    void abort() throw(Error);

    // Open (for writing) a file to receive a new checkpoint and
    // return it.  Switch logging into a new log file.  State: ready &
    // !checkpointing -> ready & checkpointing.
    std::fstream *checkpointBegin(std::ios::openmode mode =std::ios::out) throw(Error);              

    // Atomically commit the current checkpoint.
    // State: ready & checkpointing -> ready & !checkpointing.
    void checkpointEnd() throw(Error);		 

    // Abort the current checkpoint.  State: ready & checkpointing ->
    // ready & !checkpointing.
    void checkpointAbort() throw(Error);		 

    // Reopen (for writing) a checkpoint that was being written at
    // the time of the last crash but was not yet committed.  Will not
    // resume a checkpoint that has been aborted with checkpointAbort.
    // Returns NULL if there is no such checkpoint.  Illegal if you
    // called open with readonly=true.  State: recovered &
    // !checkpointing -> recovered & checkpointing.
    std::fstream *checkpointResume(std::ios::openmode mode =std::ios::out|std::ios::trunc)
      throw(Error);              

    // Delete old committed checkpoints and logs.  ckpkeep = number
    // of committed checkpoints to retain.  The conceptual empty 0th
    // checkpoint can be included in this count.  It is not an error
    // to request keeping more checkpoints than are in existence.
    // If logkeep = false, delete any logs whose version number precedes
    // the oldest checkpoint kept.  If logkeep = true, keep all logs;
    // uniformly keeping logs permits recovery entirely from logs (i.e.,
    // from the 0th checkpoint).  It is legal to set ckpkeep = 0, but
    // doing so will make recovery impossible unless you have kept all
    // your logs.  If prunebak = true and a backup log directory was
    // specified at open time, prune it too; otherwise don't.
    // State: !initial & !bad.
    void prune(int ckpkeep, bool logkeep =false, bool prunebak =true)
      throw(Error);

    // Stop whatever we're doing.  State: * -> initial.
    void close() throw();

  private:
    VestaLogPrivate *vlp;
};

#endif // _VLOG
