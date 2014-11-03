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
// ReposUI.H
// Helper functions for repository UI tools

#ifndef _REPOSUI_H_
#define _REPOSUI_H_

#include <VestaSource.H>
#include <VestaConfig.H>
#include <FS.H>

namespace ReposUI
{
    class failure {
      public:
	Text msg;
	int uerrno; // -1 if none
	VestaSource::errorCode code; // VestaSource::ok if none
	failure(const Text& msg = "",
		int uerrno = -1,
		VestaSource::errorCode code = VestaSource::ok) throw()
	  : uerrno(uerrno), code(code), msg(msg) { };
    };

    //
    // Name processing methods that do not access the repository
    //

    // Return true if name has "/", ".", or ".." as its first component.
    bool qualified(const Text& name) throw ();

    // If name is not qualified, prepend the given default directory.
    Text qualify(const Text& name, const Text& dir) throw ();

    // Convert a name to canonical form, as follows:
    //
    // 1) If the name does not begin with "/", prepend the current
    // directory name (inserting a "/" separator if needed).  The
    // current directory name is obtained by calling getcwd().
    //
    // 2) Scan the name from left to right for ".", empty, and ".."
    // components.  If a "." or empty component appears, delete it.
    // If a ".." appears, delete it and the preceding (not already
    // deleted) component.  We do not expand symbolic links or check
    // whether components cancelled out by ".." actually exist in the
    // repository.
    //
    // 3) If the first component of the name is now
    // [UserInterface]AppendableRootName, replace it by "/vesta";
    // else if the first component of the name is now
    // [UserInterface]MutableRootName, replace it by "/vesta-work";
    // else if the first component of the name is now
    // "/vesta" or "/vesta-work", return the name unchanged; else
    // throw failure.
    //
    Text canonicalize(const Text& name)
      throw (VestaConfig::failure, ReposUI::failure);

    // Convenience procedure, equivalent to 
    //   canonicalize(qualify(name, root))
    Text canonicalize(const Text& name, const Text& root)
      throw (VestaConfig::failure, ReposUI::failure);

    // Split name into parent name and final arc.
    // name must not be empty or end with "/".
    // errtext is incorported in the failure exception message on errors.
    //
    void split(const Text& name, Text& par, Text& arc,
	       const Text& errtext) throw (failure);


  // strip root name "/vesta" or [UserInterface]AppendableRootName, 
  // or similarly for "/vesta-work", [UserInterface]MutableRootName,
  // or leaves the name unchanged if those were not found.
  // RootLoc indcates type of vesta root.
  enum RootLoc { VESTA, VESTAWORK, NOWHERE};
  Text stripRoot(const Text& name, /*OUT*/RootLoc& loc) 
    throw (VestaConfig::failure);
  
  // Remove the specified root from "name".  If "name" doesn't start with 
  // the specified root, throw ReposUI::failure.  
  // If " require_canonical" is true, "name" must begin with the
  // canonical root (i.e. "/vesta" for ReposUI::VESTA, 
  // "/vesta-work" for ReposUI::VESTAWORK).
  Text stripSpecificRoot(const Text& name, RootLoc which_root, 
				bool require_canonical = false) 
    throw (ReposUI::failure, VestaConfig::failure);
  
    //
    // Lookup and inverse lookup methods
    //

    // Interpret a name as the name as of a source provided by the
    // Vesta repository. Return a VestaSource object for the source
    // found.  The name is first converted to canonical form; see the
    // canonicalize() method below.  Then if it begins with "/vesta",
    // it is looked up under the appendable root, if with
    // "/vesta-work", under the mutable root; otherwise an error is
    // reported.
    //
    // The first form looks in the default (local) repository; the
    // second and third forms allow you to specify a repository.
    // In the third form, hostport is host + ":" + port, or "" to
    // signify the default repository.
    //
    VestaSource* filenameToVS(Text name)
      throw (failure, VestaConfig::failure, SRPC::failure);
    VestaSource* filenameToVS(Text name, Text host, Text port)
      throw (failure, VestaConfig::failure, SRPC::failure);
    VestaSource* filenameToVS(Text name, Text hostport)
      throw (failure, VestaConfig::failure, SRPC::failure);

    // Like filenameToVS, but try to find the master copy, possibly on
    // a remote repository.  Fail if the master copy can't be found.
    // The "hints" argument is a space-separated list of additional
    // host:port pairs to try besides those found as master-repository
    // attributes on the given name or its ancestors.
    VestaSource* filenameToMasterVS(Text name, Text hints ="")
      throw (failure, VestaConfig::failure, SRPC::failure);

    // Like filenameToVS, but try to find a copy that is not a
    // nonmaster ghost or nonmaster stub, possibly on a remote
    // repository.  Fail if such a copy can't be found.  The "hints"
    // argument is a space-separated list of additional host:port
    // pairs to try besides those found as master-repository
    // attributes on the given name or its ancestors.
    VestaSource* filenameToRealVS(Text name, Text hints ="")
      throw (failure, VestaConfig::failure, SRPC::failure);

    // Given a VestaSource, return a canonical pathname (starting with
    // "/") that will work with filenameToVS.  The hardwired prefixes
    // "/vesta" and "/vesta-work" are used, not
    // [UserInterface]AppendableRootName and MutableRootName, in order
    // to make the returned name more suitable for use in attributes
    // that may be replicated, vrepl directives, the evaluator, etc.
    //
    Text vsToFilename(VestaSource* vs)
      throw (failure, VestaConfig::failure, SRPC::failure);

    // lookupCreatePath looks up or creates an appendable directory
    // specified by a relative path and a root directory.  "pathname"
    // should be a relative path.  "root_name" is the canonical name
    // of an appendable directory "pathname" should be interpreted
    // relative to.  The optional "hints" parameter specifies
    // additional repositories to consult when looking for the master
    // copy of any directories.

    // If pathname does not already exist below vs_root, the function
    // creates the new path by inserting appendable directories.
    // Returns a VestaSource object which is the master copy of the
    // appendable directory found or created for the full pathname
    // within the root. User is responsible for freeing the returned
    // object.
    VestaSource* lookupCreatePath(Text root_name,
				  Text pathname, Text hints = "")
      throw(failure, VestaConfig::failure, SRPC::failure);

    // Given a VestaSource that is master, return the canonical
    // pathname of a directory whose master-repository hint should be
    // updated in a non-master replica. It traverses from vs_master
    // upward until it finds a directory whose parent is not master.
    // (This is used by some tools to automatically update
    // master-repository hint attributes.)
    Text getMasterHintDir(VestaSource* vs_master, const Text& cname)
      throw (failure, SRPC::failure);

    //
    // Methods that search directories and return useful information
    //

    // Return the highest version number in directory vs.  That is,
    // considering the set of arcs in the directory that are composed
    // entirely of decimal digits, interpret each as a decimal number and
    // return the value of the largest one.
    // Return -1 if the set is empty.
    // In the case of non-master stubs with integer version names in
    // directory vs, highver will try to find non-stub copies in other
    // repositories using the optional "hints" and "path" parameters.
    // The "hints" argument is the same format as that used with
    // filenameToRealVS above.  The "path" parameter should be the
    // canonical name of vs if it is known by the caller.
    long highver(VestaSource* vs, Text hints = "", Text path = "") 
      throw (failure, SRPC::failure);

    // Return a unique name in directory vs of the form basename.n,
    // where n is an integer.  n is chosen so that no basename.m
    // exists with m > n.
    Text uniquify(VestaSource* vs, const Text& prefix)
      throw (failure, SRPC::failure);

    // Return true if the tree rooted at the given mutable working
    // directory has changed since the last vcheckout or vadvance.  The
    // time of the vadvance must be provided as an argument, but the
    // checking is not based entirely on time.  The tree is deemed to have
    // changed if (1) the modified time of any directory in the tree is
    // greater than the given time, or (2) any directory in the tree
    // contains a mutable file (i.e., a file for which no copy-on-write
    // has been performed since the last vcheckout or vadvance).
    bool changed(VestaSource* vs, time_t since)
      throw (failure, SRPC::failure);

    // Walk the tree rooted at the given mutable working directory.
    // Delete every file whose last arc matches the given pattern, and
    // then throw failure if any file added since the last advance
    // is larger than the given size.  The pattern consists of
    // multiple fnmatch() patterns separated by spaces.  prefix is the
    // name used for vs in the messages attached to failure
    // exceptions.
    void cleanup(VestaSource* vs, const Text& pattern,
			unsigned long maxsize, const Text& prefix)
      throw (failure, SRPC::failure);

    //
    // Miscellaneous
    //

    // Give a user-friendly error code string instead of just printing
    // the name of the enum element.  Should perhaps be in VestaSource.H.
    const Text errorCodeText(VestaSource::errorCode) throw (failure);
  
    // Ask the user to enter a text comment.  It invokes the command
    // specified in the EDITOR environment variable to allow the user
    // to enter a message.  If EDITOR is not set, it reads the comment
    // from standard input.  A prompt is printed if the standard input
    // is a terminal.

    // "description" should be a short string that explains what the
    // user is supposed to enter, such as "change history message".
    // "in_message" if provided will be an initial value for the
    // message given to the user to edit.  "in_description" should be
    // description of the initial value, such as "Checkout comment".
    Text getMessage(const Text &descripton,
			   const Text &in_description = "",
			   const char* in_message = 0) 
      throw (failure, FS::Failure);

    // Run a named set of trigger commands.  This is used by some
    // repository tools to allow users to take configure actions to be
    // taken before/after a repository change.

    // The trigger commands are stored in the vesta.cfg section
    // specified with "name".  All settings in that section are sorted
    // by name with embedded strings of digits compared as decimal
    // integers.  (For example, "foo_10_bar" would sort after
    // "foo_2_bar".)  The commands are then executed with system(3) in
    // this sorted order.

    // If any trigger command exits with non-zero status or is killed
    // by a signal, failure will be thrown without executing any more
    // commands.  Also, failure will be thrown if there is a problem
    // starting the command (e.g. fork(2) fails).

    // If "verbose" is true, the name and value of each trigger
    // setting is printed before the trigger command is run.  If
    // "execute" is false, the the trigger commands are not actually
    // executed.
    void runTriggers(const Text &name, bool verbose, bool execute)
      throw(failure, VestaConfig::failure);

    // Helper function for the above to set a collection of
    // environment variables, optionally printing them out while doing
    // so.
    void setTriggerVars(const TextTextTbl &vars, bool verbose)
      throw();

    // find the version that an immutable directory is based on.
    Text prevVersion(VestaSource* vs, Text hints = "")
      throw (failure, SRPC::failure);
}

#endif
