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
// VestaSourceAtomic.H
// Last modified on Wed Feb 23 20:06:06 EST 2005 by ken@xorian.net  
//      modified on Fri Nov 10 12:11:54 PST 2000 by mann  
//

// Facility for grouping several VestaSource actions into an atomic
// unit, from outside the repository address space.  The interface
// essentially lets you write a short, straight-line program in a very
// restricted language and ship it to the repository, where it is
// executed atomically.

// A program is a sequence of steps.  A newly created
// VestaSourceAtomic object represents an empty program; invoking
// methods on this object appends steps to the program.  After the
// program is completely entered, it can either be run by invoking the
// run() method, or be cancelled by using the cancel() method or
// deleting the object.  A program can be run at most once.  Programs
// that have not yet been run or cancelled consume resources on the
// repository server.

// A program's state consists of a list of VestaSource objects, two
// current target error codes, and an ok-replacement error code.  The
// list of objects is initially empty, and the three error codes are
// all initially VestaSource::ok.  A step can either append a given
// VestaSource to the list, change the target and ok-replacement error
// codes, or select a VestaSource from the list by index number and
// invoke one of its methods.  If such a method invocation returns a
// new VestaSource object, the new object is appended to the list.
// Every step is executed with the privileges of the same user, as
// specified in the VestaSourceAtomic constructor.

// Unless otherwise noted below, the execution of each step generates
// a VestaSource::errorCode that is tested against the targets.  If it
// matches either target, the program continues; if not, it halts.
// When a program halts due to missing the error code target, no
// further steps are executed, but any preceding steps that had side
// effects ARE STILL COMMITTED; there is no voluntary abort and
// roll-back feature.  When the program halts or ends normally, the
// number of steps successfully completed is available in the ndone
// field, the VestaSource::errorCode returned by the last step is
// available in the lasterr field, and the ok-replacement error code
// after the last step is available in the okreplace field.

// A common use for the target and ok-replacement error codes is to
// check that a given name does *not* exist, by doing a lookup() with
// both targets set to VestaSource::notFound and the ok-replacement set
// to VestaSource::nameInUse.

// Defaulting the timestamp argument in a step (i.e., setting it to 0)
// is handled specially.  For each atomic program, the repository
// server reads the system clock once (after the write lock is
// acquired) and uses the resulting time for all steps where the
// timestamp argument is defaulted.

// Program execution is atomic in the following sense: First, the
// repository write lock is held during execution, so no client can
// see intermediate states between steps.  Second, the repository's
// atomic logging mechanism ensures that neither a repository crash
// nor a client crash can cause the program to halt and commit; the
// program either runs as if there had not been a crash, or aborts and
// makes no change to the repository.

// Each step method accepts a stepname argument.  These names are
// accumulated into a sequence and can be retrieved using the name()
// method.  This feature is intended for use in error message
// printing; for example:
//
//  if (!vsa.run()) {
//    VestaSource::errorCode err =
//      (vsa.lasterr == VestaSource::ok) ? vsa.okeplace : vsa.lasterr;
//    cerr << program_name << ": " << vsa.name(vsa.ndone) << ": "
//         << ReposUI::errorCodeText(err) << endl;
//    exit(1);
//  }
//
// For uniformity, all step types take a stepname argument, even those
// that cannot cause an error.  The repos_ui tools generally supply
// the full pathname of the VestaSource object being operated on as
// the stepname, but other ways of naming steps could make sense too.
// Printing vsa.ndone itself in the error message can be useful when
// debugging.

// The methods of this interface are not monitored, and should not be
// called concurrently on the same VestaSourceAtomic object.  The
// client is responsible for any locking needed to ensure this.

#ifndef _VSA_H
#define _VSA_H 1

#include "Basics.H"
#include "FP.H"
#include "SRPC.H"
#include "MultiSRPC.H"
#include "AccessControl.H"
#include "VestaSource.H"
#include "Sequence.H"

class VestaSourceAtomic {
 public:
  //
  // Types
  //
  typedef int VSIndex;

  //
  // Data fields.  These are all read-only for the client!
  //

  // Counts the number of steps entered so far.
  int nsteps;
  // Counts the number of VestaSource objects on the list.
  VSIndex nobjects;
  // Remembers the current target error codes.
  VestaSource::errorCode target1, target2;

  // Returns the number of steps successfully executed.
  int ndone;
  // Returns the error code from the last step attempted.
  VestaSource::errorCode lasterr;
  // Returns the ok-replacement error code from the last step attempted.
  VestaSource::errorCode okreplace;

  //
  // Constructors and destructor
  //

  // Use the default repository
  VestaSourceAtomic(AccessControl::Identity who =NULL) throw (SRPC::failure);

  // Specify the repository by host and port
  VestaSourceAtomic(const Text& host, const Text& port,
		    AccessControl::Identity who =NULL) throw (SRPC::failure);

  // Free resources consumed by the program
  ~VestaSourceAtomic() throw ();

  //
  // Add steps to the program.  Where no comments are provided, the
  // step invokes a VestaSource method of the same name in the obvious
  // way; see VestaSource.H and the general description above.
  //

  // Set the target and ok-replacement error codes.  Generates no
  // error code of its own.
  void setTarget(const Text& stepname,
		 VestaSource::errorCode target1 =VestaSource::ok,
		 VestaSource::errorCode target2 =VestaSource::ok,
		 VestaSource::errorCode okreplace =VestaSource::ok
		 ) throw (SRPC::failure);

  // Add a VestaSource to the list and return its index number.  If
  // vs->longid proves to be invalid at runtime, NULL is added to the
  // list and the error code is VestaSource::notFound; otherwise the
  // error code is VestaSource::ok.
  VSIndex decl(const Text& stepname, VestaSource* vs) throw (SRPC::failure);

  // Call resync at the server.  Always generates VestaSource::ok.
  void resync(const Text& stepname, VSIndex vsi) throw (SRPC::failure);

  void setTimestamp(const Text& stepname, VSIndex vsi, time_t ts)
    throw (SRPC::failure);

  VSIndex lookup(const Text& stepname, VSIndex vsi, const Text& arc)
    throw (SRPC::failure);

  VSIndex lookupPathname(const Text& stepname,
			 VSIndex vsi, const Text& pathname,
			 char pathnameSep =PathnameSep) throw (SRPC::failure);

  VSIndex lookupIndex(const Text& stepname,
		      VSIndex vsi, unsigned int index) throw (SRPC::failure);

  void reallyDelete(const Text& stepname,
		    VSIndex vsi, const Text& arc, bool existCheck =true,
		    time_t timestamp =0) throw (SRPC::failure);

  VSIndex insertFile(const Text& stepname,
		     VSIndex vsi, const Text& arc, ShortId sid, bool master,
		     VestaSource::dupeCheck chk =VestaSource::dontReplace,
		     time_t timestamp =0, FP::Tag* fptag =NULL
		     ) throw (SRPC::failure);

  VSIndex insertMutableFile(const Text& stepname,
			    VSIndex vsi, const Text& arc, ShortId sid, bool master,
			    VestaSource::dupeCheck chk
			    =VestaSource::dontReplace,
			    time_t timestamp =0, FP::Tag* fptag =NULL
			    ) throw (SRPC::failure);

  // If dir is to be NULL in the method, use -1 as diri.
  VSIndex insertImmutableDirectory(const Text& stepname, VSIndex parenti,
				   const Text& arc, VSIndex diri, bool master,
				   VestaSource::dupeCheck chk
				   =VestaSource::dontReplace,
				   time_t timestamp =0,
				   FP::Tag* fptag =NULL) throw (SRPC::failure);

  VSIndex insertAppendableDirectory(const Text& stepname,
				    VSIndex vsi, const Text& arc, bool master,
				   VestaSource::dupeCheck chk
				   =VestaSource::dontReplace,
				   time_t timestamp =0) throw (SRPC::failure);

  // If dir is to be NULL in the method, use -1 as diri.
  VSIndex insertMutableDirectory(const Text& stepname, VSIndex parenti,
				 const Text& arc, VSIndex diri, bool master,
				 VestaSource::dupeCheck chk
				 =VestaSource::dontReplace,
				 time_t timestamp =0) throw (SRPC::failure);

  VSIndex insertGhost(const Text& stepname, VSIndex vsi,
		      const Text& arc, bool master,
		      VestaSource::dupeCheck chk =VestaSource::dontReplace,
		      time_t timestamp =0) throw (SRPC::failure);

  VSIndex insertStub(const Text& stepname, VSIndex vsi,
		     const Text& arc, bool master,
		     VestaSource::dupeCheck chk =VestaSource::dontReplace,
		     time_t timestamp =0) throw (SRPC::failure);

  void renameTo(const Text& stepname,
		VSIndex vsi, const Text& arc,
		VSIndex fromDirI, const Text& fromArc,
		VestaSource::dupeCheck chk =VestaSource::dontReplace,
		time_t timestamp =0) throw (SRPC::failure);

  void makeFilesImmutable(const Text& stepname, VSIndex vsi,
			  unsigned int threshold) throw (SRPC::failure);

  // For error code checking, a true return (vs->master == master) 
  // generates VestaSource::ok; a false return generates the err argument.
  void testMaster(const Text& stepname, VSIndex vsi, bool master,
		  VestaSource::errorCode err) throw (SRPC::failure);

  void setMaster(const Text& stepname, VSIndex vsi, bool state)
    throw (SRPC::failure);

  // For error code checking, inAttribs == expected generates
  // VestaSource::ok; inAttribs != expected generates the err argument.
  void inAttribs(const Text& stepname,
		 VSIndex vsi, const Text& name, const Text& value,
		 bool expected, VestaSource::errorCode err)
    throw (SRPC::failure);

  inline void setAttrib(const Text& stepname,
			VSIndex vsi, const Text& name, const Text& value,
			time_t timestamp =0) throw (SRPC::failure)
    { writeAttrib(stepname, vsi, VestaSource::opSet, name, value,
		  timestamp); };

  inline void clearAttrib(const Text& stepname, VSIndex vsi, const Text& name,
			  time_t timestamp =0) throw (SRPC::failure)
    { writeAttrib(stepname, vsi, VestaSource::opClear, name, "", timestamp); };

  inline void addAttrib(const Text& stepname,
			VSIndex vsi, const Text& name, const Text& value,
		 time_t timestamp =0) throw (SRPC::failure)
    { writeAttrib(stepname, vsi, VestaSource::opAdd, name, value, timestamp); }

  inline void removeAttrib(const Text& stepname,
			   VSIndex vsi, const Text& name, const Text& value,
			   time_t timestamp =0) throw (SRPC::failure)
    { writeAttrib(stepname, vsi, VestaSource::opRemove, name, value,
		  timestamp); };

  void writeAttrib(const Text& stepname, VSIndex vsi, VestaSource::attribOp op,
		   const Text& name, const Text& value,
		   time_t timestamp =0) throw (SRPC::failure);

  // Merge the value(s) of attribute "name" from fromvsi into tovsi.
  // If name is NULL or "", all attributes of fromvsi are merged in.
  void mergeAttrib(const Text& stepname, VSIndex fromvsi, VSIndex tovsi,
		   const Text& name, time_t timestamp =0)
    throw (SRPC::failure);

  // Invoke AccessControl::check on vsi's ac field.  For error code
  // checking, check == expected generates VestaSource::ok;
  // check != expected generates the err argument.
  void accessCheck(const Text& stepname,
		   VSIndex vsi, AccessControl::Class cls, bool expected,
		   VestaSource::errorCode err) throw (SRPC::failure);

  // Compare vsi's type field to a bitmap of allowed value(s).  Let
  // tbit = 1 << (int) type(vsi).  Then if (tbit & allowed) == 0, this
  // step generates err; otherwise it generates VestaSource::ok.
  static inline unsigned int typebit(VestaSource::typeTag type) {
    return 1 << ((int) type);
  };
  void typeCheck(const Text& stepname,
		 VSIndex vsi, unsigned int allowed,
		 VestaSource::errorCode err) throw (SRPC::failure);

  //
  // Run the program.  Returns true if all steps were successfully
  // executed.  The number of steps successfully executed and the
  // error code value from the last step attempted are placed in the
  // ndone and lasterr fields of the object, respectively.  The
  // resources associated with the program are freed and it cannot be
  // executed again.
  //
  bool run() throw (SRPC::failure);

  // 
  // Choose not to run the program.  The resources associated with the
  // program are freed and it cannot be executed again.  Called
  // automatically by the destructor if run() has not yet been called.
  //
  void cancel() throw (SRPC::failure);

  //
  // Obtain the name of the nth step (0-origin).
  //
  Text name(int step) throw ();

  //
  // Private stuff
  //
 private:
  SRPC* srpc;
  MultiSRPC::ConnId id;
  typedef Sequence<Text> NameSeq;
  NameSeq* names;
};

#endif // _VSA_H
