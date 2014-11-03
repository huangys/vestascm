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
// Mastership.C
//
// Implements AcquireMastership and cedeMastership, etc.
//

#include "UniqueId.H"
#include "FP.H"
#include "VestaSource.H"
#include "VDirSurrogate.H"
#include "logging.H"
#include "VRConcurrency.H"
#include "Mastership.H"
#include "VLogHelp.H"

#include "lock_timing.H"

using std::fstream;
using std::endl;
using std::cerr;
using std::cout;

Text myHostPort, myMasterHint;

// Forward
static VestaSource::errorCode
AcceptMastership(VestaSource* svs, const char* pathname, char pathnameSep,
		 const char* requestid, const char* grantid) throw ();
static VestaSource::errorCode
AcknowledgeMastership(VestaSource* svs,
		      const char* grantid,
		      AccessControl::Identity swho = NULL) throw ();
static VestaSource::errorCode
FinishMastership(const char* pathname, char pathnameSep,
		 const char* grantid) throw ();
static void* RecoverMastershipThread(void *arg) throw ();


//
// Check whether the local repository is permitted to acquire
// mastership from (direction="#mastership-from") or cede mastership
// to (direction="#mastership-to") the specified repository
// (hostport).  The vs parameter is the local copy of the object.
// Implementation: search upward for a #mastership-from or
// #mastership-to attribute; if found, return true if it lists the
// specified repository or "*".  If not, or none found, return false.
//
bool
MastershipAccessCheck(VestaSource* vs, const char* direction,
		      const char* hostport) throw ()
{
  bool free_vs = false;
  for (;;) {
    if (vs == NULL) return false;
    if (vs->getAttribConst(direction)) break;
    VestaSource *vs_parent = vs->longid.getParent().lookup();
    if(free_vs) 
      delete vs;
    vs = vs_parent;
    // We allocated vs, we need to free it.
    free_vs = true;
  }
  bool result = (vs->inAttribs(direction, hostport) ||
		 vs->inAttribs(direction, "*"));
  if(free_vs)
    delete vs;
  return result;
}

// Check whether mastership transfer from a master of type fromType to
// a nonmaster of type toType is allowed.
static bool
TypeCheck(VestaSource::typeTag fromType, VestaSource::typeTag toType)
{
  switch (toType) {
  case VestaSource::appendableDirectory:
    // Transferring to an appendable directory is safe only from another
    // appendable directory.
  case VestaSource::stub:
    // Transferring to a stub is safe only from another stub.
  case VestaSource::ghost:
    // Transferring to a ghost from something of a different type is
    // safe but probably a mistake, so we disallow it too.
    if (fromType != toType) {
      return false;
    }
    break;

  default:
    // Otherwise, any transfer is safe, assuming the replication
    // invariant holds.  If svs is not either a ghost or of the same
    // type as dvs, however, the replication invariant is already
    // violated, so we disallow the transfer.
    if (fromType != toType && fromType != VestaSource::ghost) {
      return false;
    }
    break;
  }
  return true;
}

//
// Main driver routine to acquire mastership for pathname.  Major
// steps, each separately atomic:
//
// A1: Check that srcHost:srcPort can be contacted and is master.
// A2: Choose a requestid and record it locally.
// A3: Ask srcHost:srcPort to cede mastership, recording and returning grantid.
//  A3x: If srcHost:srcPort is up but says no, erase requestid and quit.
// A4: Fill in missing children, set master flag, and record grantid.
// A5: Ask srcHost:srcPort to erase grantid.
// A6: Erase grantid.
//
// Brief motivation: We want to always have some state recorded at the
// destination repository until the transfer is entirely finished, so
// that we can always drive recovery of transfers that have failed
// partway through from the destination.  If we didn't need this, we
// could merge step A6 into A4.
// 

VestaSource::errorCode
AcquireMastership(const char* pathname,
		  const char* srcHost, const char* srcPort, char pathnameSep,
		  AccessControl::Identity dwho, AccessControl::Identity swho)
     throw ()
{
  VestaSource* sroot = NULL;
  VestaSource* droot = NULL;
  VestaSource* dvs = NULL;
  VestaSource* svs = NULL;
  ReadersWritersLock* lock = NULL;
  const char* grantid = NULL;
  VestaSource::errorCode err = VestaSource::ok;
  char arcbuf[MAX_ARC_LEN+1], hostbuf[MAX_ARC_LEN+1], portbuf[MAX_ARC_LEN+1];
  char hpbuf[2*MAX_ARC_LEN+2];
  bool needCommit = false;
  bool needRecovery = false;
  FP::Tag uid;
  time_t now = 0;
  char *xp = NULL;
  unsigned char* uidbytes = NULL;

  Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership: %s\n", pathname);

  if (*srcHost == '\000' || *srcPort == '\000') {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: srcHost:srcPort must be supplied\n");
    err = VestaSource::notMaster;
    goto done;
  }    

  Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership: trying master %s:%s\n",
		 srcHost, srcPort);

  //
  // Step A1: Check that srcHost:srcPort can be contacted and is master.
  //

  // A few sanity checks.  The srcHost must be a fully qualified
  // domain name, while the srcPort can be a decimal number or a
  // service name (!).  We check for certain illegal characters that
  // would cause trouble in the code below or in SRPC.
  //
  if (strchr(srcHost, ' ') || strchr(srcHost, '/') ||
      strchr(srcPort, ' ') || strchr(srcPort, '/') ||
      !strchr(srcHost, '.') || strchr(srcPort, ':')) { 
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: bad srcHost:srcPort %s:%s\n",
		   srcHost, srcPort);
    err = VestaSource::notMaster;
    goto done;
  }

  try {
    // Look up the remote object
    sroot = VDirSurrogate::repositoryRoot(srcHost, srcPort);
    err = sroot->lookupPathname(pathname, svs, swho, pathnameSep);

  } catch (SRPC::failure f) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: initial RPC to %s:%s failed: %s (%d)\n", 
		   srcHost, srcPort, f.msg.cchars(), f.r);
    // In this case there can be no partially-done transfer
    err = VestaSource::rpcFailure;
    goto done;
  }

  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: remote lookup failed: %s\n", 
		   VestaSource::errorCodeString(err));
    if (err == VestaSource::notFound) err = VestaSource::notMaster;
    goto done;
  }

  if (!svs->hasAttribs()) {
    Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership: not an independent object\n");
    err = VestaSource::inappropriateOp;
    goto done;
  }

  if (!svs->master) {
    Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership: %s:%s is not master\n",
		   srcHost, srcPort);
    err = VestaSource::notMaster;
    goto done;
  }      

  //
  // Step A2: Choose a requestid and record it locally.
  //

  // Look up the object locally, get write lock, start atomic action
  droot = VestaSource::repositoryRoot(LongId::writeLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "AcquireMastership (step A2)");
  VRLog.start();
  needCommit = true;

  err = droot->lookupPathname(pathname, dvs, dwho, pathnameSep);
  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: local lookup failed: %s)\n", 
		   VestaSource::errorCodeString(err));
    goto done;
  }

  // Had better be under the appendable root
  assert(RootLongId.isAncestorOf(dvs->longid));

  // Nothing to do if already master
  if (dvs->master) {
    Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership: already master\n");
    err = VestaSource::ok;
    goto done;
  }    

  // Check type compatibility.  We'll have to do this again just before
  // completing the transfer, in case something else is racing against us.
  if (!TypeCheck(svs->type, dvs->type)) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: type %s -> %s disallowed\n",
		   VestaSource::typeTagString(svs->type),
		   VestaSource::typeTagString(dvs->type));
    err = VestaSource::inappropriateOp;
    goto done;
  }

  // Check permission locally.  If it changes hereafter, we'll succeed
  // anyway, because we do all local changes with who=NULL.
  sprintf(hpbuf, "%s:%s", srcHost, srcPort);
  if (!dvs->ac.check(dwho, AccessControl::ownership) ||
      !MastershipAccessCheck(dvs, "#mastership-from", hpbuf)) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: noPermission in local repository\n");
    err = VestaSource::noPermission;
    goto done;
  }

  // Generate a requestid for the transfer, of the form
  //  "uid timestamp srcHost:srcPort dstHost:dstPort"
  uid = UniqueId();
  now = time(NULL);
  char requestid[1024];
  xp = requestid;
  int i;
  uidbytes = (unsigned char*) uid.Words();
  for (i=0; i<FP::ByteCnt; i++) {
    sprintf(xp, "%02x", uidbytes[i]);
    xp += 2;
  }
  *xp = '\000';
  sprintf(xp, " %u %s:%s %s", now, srcHost, srcPort, 
	  (myMasterHint.Empty()
	   ? myHostPort.cchars()
	   : myMasterHint.cchars()));
  Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership: requestid \"%s\"\n", requestid);

  // Record requestid locally; marks transfer as being in progress
  err = dvs->addAttrib("#master-request", requestid, dwho, now);
  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: record requestid failed: %s\n", 
		   VestaSource::errorCodeString(err));
    goto done;
  }

  // Recovery must run if we get an SRPC::failure henceforth
  needRecovery = true;
  // Log to ensure recovery runs if we crash
  // ('acqm' pathname pathnameSep requestid)
  VRLog.put("(acqm ");
  LogPutQuotedString(VRLog, pathname);
  VRLog.put(" ");
  char sep[2];
  sep[0] = pathnameSep;
  sep[1] = '\000';
  LogPutQuotedString(VRLog, sep);
  VRLog.put(" ");
  LogPutQuotedString(VRLog, requestid);
  VRLog.put(")\n");

  // Must end atomic action and release lock to do RPC, invalidating dvs
  delete dvs;
  dvs = droot = NULL;
  VRLog.commit();
  needCommit = false;
  lock->releaseWrite();
  lock = NULL;

  //
  // Step A3: Ask the remote repository to cede mastership, recording
  // and returning grantid.  See cedeMastership() below for details.
  //
  try {
    err = svs->cedeMastership(requestid, /*OUT*/&grantid, swho);

  } catch (SRPC::failure f) {
    // This is a bad case: we don't know for sure whether the remote
    // repository ceded mastership or not.  See recovery description below.
    err = VestaSource::rpcFailure;
    goto done;
  }

  //
  // Step A3x: Clean up if cedeMastership returned an error
  //
  if (err != VestaSource::ok) {
    // This is an annoying case.  We recorded the request, then it was
    // denied.  We have to delete the requestid locally before returning
    // the error.
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: cedeMastership refused: %s\n", 
		   VestaSource::errorCodeString(err));
    (void) FinishMastership(pathname, pathnameSep, requestid);
    needRecovery = false;
    goto done;
  }

  Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership: grantid \"%s\"\n", grantid);

  //
  // Step A4: Fill in missing children, set master flag, and record grantid.
  //
  err = AcceptMastership(svs, pathname, pathnameSep, requestid, grantid);
  if (err != VestaSource::ok) {
     Repos::dprintf(DBG_MASTERSHIP, 
		    "AcquireMastership: AcceptMastership (A4) failed: %s\n",  
		    VestaSource::errorCodeString(err)); 
    // Give up on recovery
    (void) FinishMastership(pathname, pathnameSep, requestid);
    needRecovery = false;
    goto done;
  }

  //
  // Step A5: Ask srcHost:srcPort to erase grantid.
  //
  err = AcknowledgeMastership(svs, grantid, swho);
  if (err != VestaSource::ok && err != VestaSource::rpcFailure) {
    Repos::dprintf(DBG_MASTERSHIP,  
		   "AcquireMastership: AcknowledgeMastership (A5) failed: %s\n",   
		   VestaSource::errorCodeString(err));
    // Give up on recovery
    (void) FinishMastership(pathname, pathnameSep, requestid);
    needRecovery = false;
    goto done;
  }

  //
  // Step A6: Erase grantid locally.
  //
  err = FinishMastership(pathname, pathnameSep, grantid);

 done:
  if (err == VestaSource::rpcFailure && needRecovery) {
    // Schedule recovery
    QueueRecoverMastership(pathname, pathnameSep, requestid);
  }
  if (grantid) delete[] grantid;
  if (sroot) delete sroot;
  if (svs) delete svs;
  if (dvs) delete dvs;
  if (needCommit) VRLog.commit();
  if (lock) lock->release();
  Repos::dprintf(DBG_MASTERSHIP, "AcquireMastership returns %s\n",
		 VestaSource::errorCodeString(err));
  return err;
}

//
// Do step A4 of mastership acquisition: Fill in missing children, set
// master flag, and record grantid in place of requestid.
//
static VestaSource::errorCode
AcceptMastership(VestaSource* svs,
		  const char* pathname, char pathnameSep,
		  const char* requestid, const char* grantid) throw ()
{
  VestaSource* droot = NULL;
  ReadersWritersLock* lock = NULL;
  VestaSource::errorCode err = VestaSource::ok;
  VestaSource* dvs = NULL;

  // Start second atomic action
  droot = VestaSource::repositoryRoot(LongId::writeLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "AcceptMastership");
  VRLog.start();

  // Sanity check on grantid
  int reqlen = strlen(requestid);
  bool ok = (strncmp(requestid, grantid, reqlen) == 0);
  if (!ok || grantid[reqlen] != ' ') {
    Repos::dprintf(DBG_MASTERSHIP, "AcceptMastership: bad grantid \"%s\"\n", grantid);
    err = VestaSource::invalidArgs;
    goto done;
  }

  // Re-lookup dvs
  err = droot->lookupPathname(pathname, dvs, NULL, pathnameSep);
  if (err != VestaSource::ok) {
    // An error can occur here due to the local repository being
    // changed out from under us while we had the lock released.
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcceptMastership: error on 2nd local lookup: %s\n", 
		   VestaSource::errorCodeString(err));
    goto done;
  }

  // A type check error can occur here due to the local repository
  // being changed out from under us while we had the lock released.
  // (Note: if svs's type changes after the initial check,
  // cedeMastership will fail with invalidArgs; we won't even get to
  // here.  We need this check only to cover changes to dvs->type.)
  if (!TypeCheck(svs->type, dvs->type)) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcceptMastership: type %s -> %s disallowed\n",
		   VestaSource::typeTagString(svs->type),
		   VestaSource::typeTagString(dvs->type));
    err = VestaSource::inappropriateOp;
    goto done;
  }

  // If the local repository no longer has requestid recorded, then
  // don't proceed.  Could happen if someone changed it out from under
  // us while we had the lock released, but no one should do that.
  if (!dvs->inAttribs("#master-request", requestid)) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcceptMastership: grantid \"%s\" has no requestid\n", grantid);
    err = VestaSource::ok; //!! should this be an error?
    goto done;
  }

  // Update the master-repository hint on dvs
  if(!myMasterHint.Empty())
    {
      err = dvs->setAttrib("master-repository", myMasterHint.cchars(), NULL);
      assert(err == VestaSource::ok);
    }

  // Update master-repository hints on children and install stubs for
  // any missing children.
  if (dvs->type == VestaSource::appendableDirectory) {
    const char* p;
    char arcbuf[MAX_ARC_LEN+1];
    char hintbuf[MAX_ARC_LEN+1]; // seems a safe size
    time_t hinttime;
    p = grantid + strlen(requestid) + 1;
    while (*p) {
      // Extract the child arc
      const char* q = strchr(p, '/');
      if (q == NULL || q-p > MAX_ARC_LEN) {
	Repos::dprintf(DBG_MASTERSHIP,
		       "AcceptMastership: bad grantid (arc) \"%s\"\n", grantid);
	err = VestaSource::invalidArgs;
	goto done;
      }
      memcpy(arcbuf, p, q-p);
      arcbuf[q-p] = '\000';
      p = q+1;

      // Extract the child master hint
      q = strchr(p, '/');
      if (q == NULL || q-p > MAX_ARC_LEN) {
	Repos::dprintf(DBG_MASTERSHIP,
		       "AcceptMastership: bad grantid (hint) \"%s\"\n", grantid);
	err = VestaSource::invalidArgs;
	goto done;
      }
      memcpy(hintbuf, p, q-p);
      hintbuf[q-p] = '\000';
      p = q+1;

      // Extract the hint's timestamp
      q = strchr(p, '/');
      if (q == NULL || !isdigit(*p)) {
	Repos::dprintf(DBG_MASTERSHIP,
		       "AcceptMastership: bad grantid (htime) \"%s\"\n", grantid);
	err = VestaSource::invalidArgs;
	goto done;
      }
      hinttime = (time_t) atoi(p);
      p = q + 1;

      // Look up the child
      VestaSource* child;
      err = dvs->lookup(arcbuf, child, NULL);
      if (err == VestaSource::notFound) {
	// Need to insert it as a nonmaster stub
	err = dvs->insertStub(arcbuf, false, NULL,
			      VestaSource::dontReplace, &child);
	Repos::dprintf(DBG_MASTERSHIP,
		       "AcceptMastership: inserted stub %s\n", arcbuf);
      }
      assert(err == VestaSource::ok);

      if (child->master) {
	// Any master child can have its hint deleted, since the hint on
	// the parent now implicitly covers it.  We ignore what the
	// remote repository had as a hint here; we are sure the local
	// repository is master.
	err = child->clearAttrib("master-repository");
      } else {
	// Child not mastered here; need to make sure its hint is
	// explicit. We copy the hint from the remote repository, if
	// any, using the supplied timestamp; if we have a more recent
	// hint already, that one will dominate.  If neither we nor
	// the remote repository has a hint, it will remain unset.
	if (hinttime > 0) {
	  err = child->setAttrib("master-repository", hintbuf, NULL, hinttime);
	}
      }
      assert(err == VestaSource::ok);
      delete child;
    }
  }

  // Change #master-request to record the grantid, signifying that we
  // have finished accepting mastership and now have only to
  // acknowledge to the old master.  Note: The tiebreak rules
  // correctly order the add/remove/add/remove sequence on
  // #master-request even if all four get the same timestamp.
  err = dvs->removeAttrib("#master-request", requestid, NULL);
  assert(err == VestaSource::ok);
  err = dvs->addAttrib("#master-request", grantid, NULL);
  assert(err == VestaSource::ok);

  // At last we get to turn on the master flag!
  err = dvs->setMaster(true, NULL);
  assert(err == VestaSource::ok);

 done:
  if (dvs) delete dvs;
  if (lock) {
    VRLog.commit();
    lock->releaseWrite();
  }
  return err;
}

//
// Do step A5 of mastership acquisition: Ask srcHost:srcPort to erase grantid.
//
static VestaSource::errorCode
AcknowledgeMastership(VestaSource* svs, const char* grantid,
		      AccessControl::Identity swho) throw ()
{
  try {
    return svs->removeAttrib("#master-grant", grantid, swho);
  } catch (SRPC::failure f) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcquireMastership: RPC to erase grantid failed\n");
    return VestaSource::rpcFailure;
  }
}

//
// Do step A6 of mastership acquisition: Erase grantid.
// This code also serves for step A3x, with requestid in place of grantid.
//
// !! Maybe this should generally run if we fail with an error other
// than rpcFailure, since there is no point in running recovery then.
//
static VestaSource::errorCode
FinishMastership(const char* pathname, char pathnameSep,
		 const char* id) throw ()
{
  VestaSource* droot = NULL;
  ReadersWritersLock* lock = NULL;
  VestaSource::errorCode err = VestaSource::ok;
  VestaSource* dvs = NULL;

  // Start third atomic action
  droot = VestaSource::repositoryRoot(LongId::writeLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "FinishMastership");
  VRLog.start();

  // Log to cancel out the acqm and prevent recovery running after
  // next restart.  
  // ('finm' pathname pathnameSep id)
  char sep[2];
  sep[0] = pathnameSep;
  sep[1] = '\000';
  VRLog.put("(finm ");
  LogPutQuotedString(VRLog, pathname);
  VRLog.put(" ");
  LogPutQuotedString(VRLog, sep);
  VRLog.put(" ");
  LogPutQuotedString(VRLog, id);
  VRLog.put(")\n");

  // Re-lookup dvs
  err = droot->lookupPathname(pathname, dvs, NULL, pathnameSep);
  if (err != VestaSource::ok) {
    // An error can occur here due to the local repository being
    // changed out from under us while we had the lock released.
    // At this point we are essentially done, so forget the error
    Repos::dprintf(DBG_MASTERSHIP,
		   "AcceptMastership: error on 3rd local lookup: %s\n", 
		   VestaSource::errorCodeString(err));
    err = VestaSource::ok;
    goto done;
  }

  // Delete the #master-request attribute, signifying that we're done.
  err = dvs->removeAttrib("#master-request", id, NULL);
  assert(err == VestaSource::ok);

 done:
  if (dvs) delete dvs;
  if (lock) {
    VRLog.commit();
    lock->releaseWrite();
  }
  return err;
}

//
// Support for failure recovery
//
struct RecoverMastershipInfo {
  RecoverMastershipInfo *next;
  RecoverMastershipInfo *prev;
  const char* pathname;
  char pathnameSep;
  const char* requestid;
};
static Basics::mutex recoverMastershipMu;
static Basics::cond recoverMastershipCond;
static Basics::thread recoverMastershipTh;
static RecoverMastershipInfo* recoverMastershipList;
#define RECOVER_MASTERSHIP_SLEEP 3600  // one hour

// Recovery for failure cases.  This must not be invoked twice on the
// same object+requestid, or concurrently with AcquireMastership on
// it.  It causes a background thread to persistently retry the
// recovery until either it succeeds or someone manually removes the
// matching #master-request attribute.
//
// R1: #master-request is a requestid, but has no corresponding
// #master-grant.  AcquireMastership failed before step A3; delete the
// #master-request using FinishMastership.
//
// R2: #master-request is a requestid and has a corresponding
// #master-grant.  AcquireMastership failed before step A4; resume there.
//
// R3: #master-request is a grantid and has a corresponding #master-grant.
// AcquireMastership failed before step A5; resume there.
//
// R4: #master-request is a grantid and has no corresponding #master-grant.
// AcquireMastership failed before step A6; call FinishMastership to do it.
//

struct CheckMatchClosure {
  const char* requestid;  //in
  int requestidlen;       //in
  const char* matchedid;  //out
};

static bool
CheckMatchCallback(void* closure, const char* value)
{
  CheckMatchClosure* cl = (CheckMatchClosure*) closure;
  if (strncmp(cl->requestid, value, cl->requestidlen) == 0) {
    // Found a matching grantid
    cl->matchedid = strdup(value);
    return false;
  }
  return true;
}

static VestaSource::errorCode
RecoverMastership(const char* pathname, char pathnameSep,
		  const char* requestid) throw ()
{
  Repos::dprintf(DBG_MASTERSHIP, "RecoverMastership of %s: requestid %s\n",
		 pathname, requestid);

  VestaSource* sroot = NULL;
  VestaSource* svs = NULL;
  VestaSource::errorCode err = VestaSource::ok;
  char srcHost[MAX_ARC_LEN+1];
  char srcPort[MAX_ARC_LEN+1];
  const char* p;
  const char* q;
  const char* srcid = NULL;
  const char* dstid = NULL;
  ReadersWritersLock* lock = NULL;
  VestaSource* droot = NULL;
  VestaSource* dvs = NULL;

  // Parse stuff out of requestid
  // Skip uid
  p = strchr(requestid, ' ');
  p++;
  // Skip timestamp
  p = strchr(p, ' ');        // point to timestamp
  p++;
  // Find end of srcHost
  q = strchr(p, ':');
  memcpy(srcHost, p, q-p);
  srcHost[q-p] = '\000';
  p = q+1;
  // Find end of srcPort
  q = strchr(p, ' ');
  memcpy(srcPort, p, q-p);
  srcPort[q-p] = '\000';
  p = q+1;
  
  // Look up the local object to read #master-request
  droot = VestaSource::repositoryRoot(LongId::readLock, &lock);
  RWLOCK_LOCKED_REASON(lock, "RecoverMastership");
  err = droot->lookupPathname(pathname, dvs, NULL, pathnameSep);
  if (err != VestaSource::ok) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "RecoverMastership: local lookup failed: %s\n", 
		   VestaSource::errorCodeString(err));
    // This causes recovery of this request to be abandoned
    goto done;
  }
  // Get matching local id, or NULL if none
  CheckMatchClosure cl;
  cl.requestid = requestid;
  cl.requestidlen = strlen(requestid);
  cl.matchedid = NULL;
  dvs->getAttrib("#master-request", CheckMatchCallback, &cl);
  dstid = cl.matchedid;

  dvs = droot = NULL;
  lock->releaseRead();
  lock = NULL;

  if (dstid == NULL) {
    // Someone deleted #master-request.  Quit.  Need to call
    // FinishMastership just to log the completion.
    Repos::dprintf(DBG_MASTERSHIP,
		   "RecoverMastership: #master-request was manually deleted\n");
    (void) FinishMastership(pathname, pathnameSep, requestid);
    err = VestaSource::ok;
    goto done;
  }

  try {
    // Look up the remote object to read #master-grant
    sroot = VDirSurrogate::repositoryRoot(srcHost, srcPort);
    err = sroot->lookupPathname(pathname, svs, NULL/*!!??*/, pathnameSep);

    // Get matching grantid, or NULL if none
    CheckMatchClosure cl;
    cl.requestid = requestid;
    cl.requestidlen = strlen(requestid);
    cl.matchedid = NULL;
    svs->getAttrib("#master-grant", CheckMatchCallback, &cl);
    srcid = cl.matchedid;

  } catch (SRPC::failure f) {
    Repos::dprintf(DBG_MASTERSHIP,
		   "RecoverMastership: RPC to %s:%s failed: %s (%d)\n", 
		   srcHost, srcPort, f.msg.cchars(), f.r);
    err = VestaSource::rpcFailure;
    goto done;
  }

  // Is #master-request set to the requestid or the grantid?
  if (strlen(dstid) == strlen(requestid)) {
    // #master-request is the requestid
    if (srcid == NULL) {
      // Case R1: Do step A3x
      Repos::dprintf(DBG_MASTERSHIP,
		     "RecoverMastership: case R1 (aborting)\n");
      err = FinishMastership(pathname, pathnameSep, dstid);
    } else {
      // Case R2: Resume at step A4
      Repos::dprintf(DBG_MASTERSHIP,
		     "RecoverMastership: case R2 (accepting)\n");
      err = AcceptMastership(svs, pathname, pathnameSep, requestid, srcid);
      if (err != VestaSource::ok) {
	// Give up on recovery
	Repos::dprintf(DBG_MASTERSHIP,
		       "RecoverMastership: failed to accept transfer: %s\n",
		       VestaSource::errorCodeString(err));
	(void) FinishMastership(pathname, pathnameSep, requestid);
	goto done;
      }
      err = AcknowledgeMastership(svs, srcid);
      if (err != VestaSource::ok && err != VestaSource::rpcFailure) {
	// Give up on recovery
	Repos::dprintf(DBG_MASTERSHIP,
		       "RecoverMastership: failed to acknowledge transfer: %s\n",
		       VestaSource::errorCodeString(err));
	(void) FinishMastership(pathname, pathnameSep, requestid);
	goto done;
      }
      err = FinishMastership(pathname, pathnameSep, srcid);
    }
  } else {
    // #master-request is the grantid
    if (srcid != NULL) {
      // Case R3: Resume at step A5
      Repos::dprintf(DBG_MASTERSHIP, "RecoverMastership: case R3 (acknowledging)\n");
      err = AcknowledgeMastership(svs, srcid);
      if (err != VestaSource::ok && err != VestaSource::rpcFailure) {
	// Give up on recovery
	Repos::dprintf(DBG_MASTERSHIP,
		       "RecoverMastership: failed to acknowledge transfer: %s\n",
		       VestaSource::errorCodeString(err));
	(void) FinishMastership(pathname, pathnameSep, requestid);
	goto done;
      }
      err = FinishMastership(pathname, pathnameSep, srcid);
    } else {
      // Case R4:
      Repos::dprintf(DBG_MASTERSHIP,
		     "RecoverMastership: case R4 (finishing)\n");
      err = FinishMastership(pathname, pathnameSep, dstid);
    }
  }

 done:
  if (sroot) delete sroot;
  if (svs) delete svs;
  if (lock) lock->release();
  if (dvs) delete dvs;
  if (srcid) delete[] srcid;
  if (dstid) delete[] dstid;
  Repos::dprintf(DBG_MASTERSHIP, "RecoverMastership returns %s\n",
		 VestaSource::errorCodeString(err));	  
  return err;
}

void
QueueRecoverMastership(const char* pathname, char pathnameSep,
		       const char* requestid) throw ()
{
  recoverMastershipMu.lock();
  RecoverMastershipInfo* info = NEW(RecoverMastershipInfo);
  info->pathname = strdup(pathname);
  info->pathnameSep = pathnameSep;
  info->requestid = strdup(requestid);
  // Add to front of list
  info->next = recoverMastershipList;
  info->prev = NULL;
  if (recoverMastershipList) recoverMastershipList->prev = info;
  recoverMastershipList = info;
  recoverMastershipMu.unlock();
  recoverMastershipCond.signal();
}

// Requires mutex
void
DequeueRecoverMastershipL(RecoverMastershipInfo* rminfo)
{
  if (rminfo->prev) {
    rminfo->prev->next = rminfo->next;
  } else {
    recoverMastershipList = rminfo->next;
  }
  if (rminfo->next) {
    rminfo->next->prev = rminfo->prev;
  }
  free((void*)rminfo->pathname);
  free((void*)rminfo->requestid);
  delete rminfo;
}

void
DequeueRecoverMastership(RecoverMastershipInfo* rminfo)
{
  recoverMastershipMu.lock();
  DequeueRecoverMastershipL(rminfo);
  recoverMastershipMu.unlock();
}

static void*
RecoverMastershipThread(void *arg) throw ()
{
  VestaSource::errorCode err;
  struct timespec waittime;
  waittime.tv_sec = RECOVER_MASTERSHIP_SLEEP;
  waittime.tv_nsec = 0;

  recoverMastershipMu.lock();
  for (;;) {
    while (recoverMastershipList == NULL) {
      recoverMastershipCond.wait(recoverMastershipMu);
    }
    // Make one pass down the list.  New work can be added above where
    // we started while we are busy.
    RecoverMastershipInfo* first = recoverMastershipList;
    RecoverMastershipInfo* cur = first;

    while (cur) {
      // Allow for more work to be added
      recoverMastershipMu.unlock();

      // Process cur
      err = RecoverMastership(cur->pathname, cur->pathnameSep, cur->requestid);

      // Reacquire lock
      recoverMastershipMu.lock();
      RecoverMastershipInfo* next = cur->next;
      if (err != VestaSource::rpcFailure) {
	// Recovery is either done or hopeless
	DequeueRecoverMastershipL(cur);
      }
      cur = next;
    }

    if (recoverMastershipList == first) {
      // No new work has been added
      struct timespec abstime;      
#if 0
      pthread_get_expiration_np(&waittime, &abstime);
#else
      struct timeval now;
      gettimeofday(&now, NULL);
      abstime.tv_sec = now.tv_sec + waittime.tv_sec;
      abstime.tv_nsec = now.tv_usec * 1000 + waittime.tv_nsec;
      if (abstime.tv_nsec >= 1000000000) {
	abstime.tv_nsec -= 1000000000;
	abstime.tv_sec++;
      }
#endif
      recoverMastershipCond.timedwait(recoverMastershipMu, &abstime);
    }
  }
}


// Unpack log record and requeue request on recoverMastershipList
static void
AcqmCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
  char* pathname;
  char sep[2];
  char *requestid;

  rr->getNewQuotedString(c, pathname);
  rr->getQuotedString(c, sep, sizeof(sep));
  rr->getNewQuotedString(c, requestid);
  QueueRecoverMastership(pathname, sep[0], requestid);
  delete[] pathname;
  delete[] requestid;
}

// Unpack log record and dequeue request from recoverMastershipList
static void
FinmCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
  char* pathname;
  char sep[2];
  char *finid;

  rr->getNewQuotedString(c, pathname);
  rr->getQuotedString(c, sep, sizeof(sep));
  rr->getNewQuotedString(c, finid);

  RecoverMastershipInfo* rminfo = recoverMastershipList;
  bool removed = false;
  while (rminfo && !removed) {
    RecoverMastershipInfo* next = rminfo->next;
    if (strcmp(pathname, rminfo->pathname) == 0 &&
	sep[0] == rminfo->pathnameSep &&
	strncmp(finid, rminfo->requestid, strlen(rminfo->requestid)) == 0) {
      DequeueRecoverMastership(rminfo);
      removed = true;
    }
    rminfo = next;
  }
  assert(removed);

  delete[] pathname;
  delete[] finid;
}

// Write log records for all unfinished recoveries into the checkpoint
// file.  Would be nice to have a RecoveryWriter so we could factor
// out the common code, sigh.
void
MastershipCheckpoint(fstream& ckpt) throw ()
{
  recoverMastershipMu.lock();
  RecoverMastershipInfo* rminfo = recoverMastershipList;
  while (rminfo) {
    ckpt << "(acqm ";
    PutQuotedString(ckpt, rminfo->pathname);
    ckpt << " ";
    char sep[2];
    sep[0] = rminfo->pathnameSep;
    sep[1] = '\000';
    PutQuotedString(ckpt, sep);
    ckpt << " ";
    PutQuotedString(ckpt, rminfo->requestid);
    ckpt << ")\n";

    rminfo = rminfo->next;
  }
  recoverMastershipMu.unlock();
}


//
// Helpers for cedeMastership
//

// Closure and callback for VestaSource::getAttribHistory to find the
// timestamp of a particular name, value pair.

struct AttribTimestampClosure {
  const char* name;
  const char* value;
  time_t timestamp;
};

static bool
AttribTimestampCallback(void* closure, VestaSource::attribOp op,
			const char* name, const char* value, time_t timestamp)
{
  AttribTimestampClosure *cl = (AttribTimestampClosure *) closure;
  if ((op == VestaSource::opSet || op == VestaSource::opAdd) &&
      strcmp(name, cl->name) == 0 &&
      strcmp(value, cl->value) == 0) {
    cl->timestamp = timestamp;
    return false;
  } else {
    return true;
  }
}

// Closure and callback for VestaSources::list to update the hints on
// children and form the arc/hint/ts/... list of a grantid.

struct HintsClosure {
  VestaSource* vs;
  Text list;
  time_t now;
  Text nowtxt;
};

static bool
HintsCallback(void* closure, VestaSource::typeTag type, Arc arc,
	      unsigned int index, Bit32 pseudoInode, ShortId sid, bool master)
{
  HintsClosure* cl = (HintsClosure*) closure;
  VestaSource* child;
  VestaSource::errorCode err = cl->vs->lookupIndex(index, child);
  assert(err == VestaSource::ok);
  if (master) {
    if(!myMasterHint.Empty())
      child->setAttrib("master-repository", myMasterHint.cchars(),
		       NULL, cl->now);
    cl->list = (cl->list + arc + "/" +
		(myMasterHint.Empty()
		 ? myHostPort.cchars()
		 : myMasterHint.cchars()) + "/" +
		cl->nowtxt + "/");
  } else {
    const char* hint = child->getAttrib("master-repository");
    time_t hintts;
    if (hint == NULL) {
      hint = "";
      hintts = 0;
    } else {
      AttribTimestampClosure atcl;
      atcl.name = "master-repository";
      atcl.value = hint;
      atcl.timestamp = 0;
      child->getAttribHistory(AttribTimestampCallback, &atcl);
      hintts = atcl.timestamp;
    }
    char hinttsChars[32];
    sprintf(hinttsChars, "%d", hintts);
    cl->list = cl->list + arc + "/" + hint + "/" + hinttsChars + "/";
  }
  delete child;
  return true;
}

// Do the following atomically: 
//
//  C1: Check that we have mastership for this object.  If not,
//  return notMaster.  Also check that "who" has permission to take
//  mastership.  (!!For now at least, use ownership.)
//
//  C2: Update master-repository hint and (if object is an appendable
//  directory) those of its children.
//
//  C3: If object is an appendable directory, make a list of its
//  children, their new master-repository hints, and the timestamp of
//  each hint.  Append the list to requestid (separated by a space),
//  and return the result as grantid.  If a child has no hint (i.e.,
//  its master is unknown), return an empty hint string with timestamp
//  0.  List syntax: arc1/hint1/t1/arc2/hint2/t2/.../arcn/hintn/tn/
//
//  C4: Record grantid in #master-grant attribute.
//
//  C5: Turn off master flag.
//
// The caller is responsible for freeing grantid.
// Requires StableLock.write on entry.
//
VestaSource::errorCode
VestaSource::cedeMastership(const char* requestid, const char **grantidOut,
			    AccessControl::Identity who)
     throw (SRPC::failure /*can't really happen*/)
{
  VestaSource::errorCode err;

  if (Repos::isDebugLevel(DBG_MASTERSHIP)) {
    char buf[100];
    Repos::pr_nfs_fh(buf, (nfs_fh*) longid.value.byte);
    Repos::dprintf(DBG_MASTERSHIP, "cedeMastership: longid %s, requestid \"%s\"\n",
		   buf, requestid);
  }

  // Step C1: checking
  if (!RootLongId.isAncestorOf(longid))
    return VestaSource::invalidArgs;
  if (!master) return VestaSource::notMaster;
  const char* tstmp = strchr(requestid, ' ');
  if (tstmp == NULL) {
    // Bad requestid
    Repos::dprintf(DBG_MASTERSHIP, "cedeMastership: bad requestid \"%s\"\n",
		   requestid);
    return VestaSource::invalidArgs;
  }
  tstmp++;
  const char* srcHP = strchr(tstmp, ' ');
  if (srcHP == NULL) {
    // Bad requestid
    Repos::dprintf(DBG_MASTERSHIP, "cedeMastership: bad requestid \"%s\"\n",
		   requestid);
    return VestaSource::invalidArgs;
  }
  srcHP++;
  const char* dstHP = strrchr(srcHP, ' ');
  if (dstHP == NULL) {
    // Bad requestid
    Repos::dprintf(DBG_MASTERSHIP, "cedeMastership: bad requestid \"%s\"\n",
		   requestid);
    return VestaSource::invalidArgs;
  }  
  dstHP++;
  int srclen = dstHP - srcHP - 1;
  // If srcHP matches neither myHostPort nor myMasterHint, this is an
  // invalid request.
  if (!(srclen == myHostPort.Length() &&
	strncmp(srcHP, myHostPort.cchars(), srclen) == 0) &&
      !(!myMasterHint.Empty() &&
	srclen == myMasterHint.Length() &&
	strncmp(srcHP, myMasterHint.cchars(), srclen) == 0)) {
    // Bad srcHost:srcPort in requestid
    if(myMasterHint.Empty())
      Repos::dprintf(DBG_MASTERSHIP,
		     "cedeMastership: bad requestid \"%s\"; srcHostPort != %s\n",
		     requestid, myHostPort.cchars());
    else
      Repos::dprintf(DBG_MASTERSHIP,
		     "cedeMastership: bad requestid \"%s\"; srcHostPort != %s or %s\n",
		     requestid, myHostPort.cchars(), myMasterHint.cchars());
    return VestaSource::invalidArgs;
  }
  if (!ac.check(who, AccessControl::ownership) ||
      !MastershipAccessCheck(this, "#mastership-to", dstHP)) {
    return VestaSource::noPermission;
  }

  // Atomically group log entries
  VRLog.start();

  // Steps C2 and C3: Update master-repository hints, form grantid
  HintsClosure cl;
  cl.vs = this;
  cl.list = " ";
  cl.now = time(NULL);
  char nowtxt[32];
  sprintf(nowtxt, "%d", cl.now);
  cl.nowtxt = nowtxt;

  err = setAttrib("master-repository", dstHP, NULL, cl.now);
  assert(err == VestaSource::ok);
  if (type == VestaSource::appendableDirectory) {
    err = list(0, HintsCallback, &cl);
    assert(err == VestaSource::ok);
  }

  int reqlen = strlen(requestid);
  char* grantid = NEW_PTRFREE_ARRAY(char, reqlen + cl.list.Length() + 1);
  strcpy(grantid, requestid);
  strcpy(grantid + reqlen, cl.list.cchars());

  // Step C4: record grantid
  err = addAttrib("#master-grant", grantid, NULL, cl.now);
  assert(err == VestaSource::ok);

  // Step C5: turn off master flag
  err = setMaster(false);
  assert(err == VestaSource::ok);

  // Done
  Repos::dprintf(DBG_MASTERSHIP, "cedeMastership succeeded, grantid \"%s\"\n",
		 grantid);
  *grantidOut = grantid;
  VRLog.commit();
  return VestaSource::ok;
}

void 
MastershipInit1() throw ()
{
  myHostPort =
    VDirSurrogate::defaultHost() + ":" + VDirSurrogate::defaultPort();
  // Sanity check on the default host:port.  (Note: some sanity checks
  // have already been performed in VDirSurrogate_init.)
  if(myHostPort.FindChar('/') != -1)
    {
      Repos::dprintf(DBG_ALWAYS,
		     "Fatal: Default repository host:port contains a slash\n");
      abort();
    }
  if(myHostPort.FindChar(' ') != -1)
    {
      Repos::dprintf(DBG_ALWAYS,
		     "Fatal: Default repository host:port contains a space\n");
      abort();
    }
  // Default myMasterHint to myHostPort, but allow a config variable
  // to override it.
  myMasterHint = myHostPort;
  bool defaultMasterHint = true;
  try
    {
      defaultMasterHint =
	!VestaConfig::get("Repository", "master_hint", myMasterHint);
    }
   catch (VestaConfig::failure f)
     {
       // Probably can't actually happen, as config file parsing
       // errors would have ben caught in VDirSurrogate_init.
       cerr << "VestaConfig::failure " << f.msg << endl;
       abort();
     }
  // If we have a non-default non-empty master hint, make sure it's
  // OK.
  if(!defaultMasterHint && !myMasterHint.Empty())
    {
      bool badMasterHint = false;
      // The master hint must not contain a slash
      if(myMasterHint.FindChar('/') != -1)
	{
	  Repos::dprintf(DBG_ALWAYS,
			 "Fatal: [Repository]master_hint contains a slash\n");
	  badMasterHint = true;
	}
      // The master hint must not contain a space
      if(myMasterHint.FindChar(' ') != -1)
	{
	  Repos::dprintf(DBG_ALWAYS,
			 "Fatal: [Repository]master_hint contains a space\n");
	  badMasterHint = true;
	}
      // The master hint must contain a colon
      int colonPos = myMasterHint.FindChar(':');
      if(colonPos == -1)
	{
	  Repos::dprintf(DBG_ALWAYS,
			 "Fatal: [Repository]master_hint doesn't contain a colon\n");
	  badMasterHint = true;
	}
      else
	{
	  // Check that the part after the colon is numeric and
	  // non-empty.
	  for(int i = colonPos+1; i < myMasterHint.Length(); i++)
	    {
	      if(!isdigit(myMasterHint[i]))
		{
		  Repos::dprintf(DBG_ALWAYS,
				 "Fatal: Non-digit after colon in [Repository]master_hint\n");
		  badMasterHint = true;
		  break;
		}
	    }
	}
      if(badMasterHint)
	abort();
    }
  // If we have a default master hint, check to see if it's the
  // loopback address.
  if(defaultMasterHint)
    {
      try
	{
	  in_addr my_addr =
	    TCP_sock::host_to_addr(VDirSurrogate::defaultHost());
	  cout << "Repository host resolves to " << inet_ntoa_r(my_addr)
	       << endl;
	  if((my_addr.s_addr &  htonl(0xff000000)) ==
	     htonl(127 << 24))
	    {
	      // The repository is in loopback space: turn off
	      // master-repository hint attributes.
	      Repos::dprintf(DBG_ALWAYS,
			     "Warning: Repository address is in 127.0.0.0/8; "
			     "master-repository hint attributes will not be set\n");
	      myMasterHint="";
	    }
	}
      catch(...)
	{
	  // Can't resolve the hostname of the repository!  
	  Repos::dprintf(DBG_ALWAYS,
			 "Fatal: Can't resolve the repository's host (%s)!\n",
			 VDirSurrogate::defaultHost().cchars());
	  abort();
	}
    }

  RegisterRecoveryCallback("acqm", AcqmCallback);
  RegisterRecoveryCallback("finm", FinmCallback);
}

void 
MastershipInit2() throw ()
{
  Basics::thread_attr recoverMastership_attr;
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING) && defined(PTHREAD_SCHED_RR_OK) && !defined(__linux__)
  // Linux only allows the superuser to use SCHED_RR
  recoverMastership_attr.set_schedpolicy(SCHED_RR);
  recoverMastership_attr.set_inheritsched(PTHREAD_EXPLICIT_SCHED);
  recoverMastership_attr.set_sched_priority(sched_get_priority_min(SCHED_RR));
#endif

  recoverMastershipTh.fork(RecoverMastershipThread, NULL,
			   recoverMastership_attr);
}
