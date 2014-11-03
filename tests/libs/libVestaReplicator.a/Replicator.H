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
// Replicator.H
//
// Utility routine for copying data from one Vesta repository to
// another.
//

#ifndef _REPLICATOR_H
#define _REPLICATOR_H

#include <Basics.H>
#include <Text.H>
#include <VestaSource.H>
#include <AccessControl.H>
#include <Sequence.H>
#include <ReposUI.H>

// A Replicator object is used to copy from a source repository to a
// destination repository.  The source and destination are specified
// in the constructor and are constant thereafter.
//
class Replicator {
  public:
    // The replicator uses "pathname patterns" that have a superset
    // of the syntax and metacharacters as the "model patterns" used
    // by the weeder.  See the VestaWeed(1) man page.  In pathname
    // patterns, the additional metacharacters '#' and '%' are
    // defined.  '#' matches zero or more digits.  '%' at the start of
    // a pattern arc causes the remainder of that pattern arc to match
    // zero or more consecutive arcs of a pathname.  Also, all
    // metacharacters (except '%') are allowed within curly braces 
    // ('{ }').  In this interface, the leading "/vesta/" of each
    // pattern must be omitted.

    // As with the weeder, the set of objects to replicate is built up
    // by a sequence of directives.  A '+' directive adds all names matching
    // the corresponding pattern to the set; a '-' directive deletes all
    // matching names.  The directives are cumulative, so the same name
    // could be added and deleted several times before finally ending
    // up in or out of the set.

    // While the weeder's patterns always name model files, the
    // replicator's patterns may name directories.  When a pattern
    // matches a directory, it implicitly names the entire subtree
    // rooted at that directory.

    typedef Text Pattern;
    struct Directive {
	char sign; // '+' to add to the set; '-' to subtract from it
	Pattern pattern;
	inline Directive(char s, Pattern p) : pattern(p) {
	    sign = s;
	};
	inline Directive() { };
    };
    typedef Sequence<Directive> DirectiveSeq;

    // Failure exception.  The message does *not* include the error
    // text for the code; use ReposUI::errorCodeText() to get it when
    // printing messages.
    struct Failure {
	VestaSource::errorCode code;  // -1 if not a VestaSource error
	Text msg;
	inline Failure(VestaSource::errorCode code, const Text& msg) {
	    this->code = code; this->msg = msg; };
	inline Failure(const Text& msg) {
	    this->code = (VestaSource::errorCode) -1; this->msg = msg;
	}
	inline Failure& operator=(const Failure &f) {
	    this->code = f.code; this->msg = f.msg; return *this; };
    };

    // Constructor
    Replicator(Text source_host, Text source_port, 
	       Text dest_host, Text dest_port,
	       AccessControl::Identity source_who =NULL,
	       AccessControl::Identity dest_who =NULL);
	
    // Flags, or'd together
    enum Flags {
	noFlags     = 0,
        attrNew     = 0x0001, // Copy attributes on all newly created
	                      // objects or type changes (e.g. stub to
                              // non-stub).
	attrOld     = 0x0002, // Copy attributes on objects that are
	                      // included in the set to be copied but
			      // already exist.
        attrInner   = 0x0004, // Copy attributes on existing
                              // directories that appear inside the
                              // pathnames of objects that are included
                              // in the set to be copied.  For example,
			      // if /vesta/foo/bar is included but
			      // /vesta/foo is neither included nor
			      // excluded, and /vesta/foo already
			      // exists at the destination, the
			      // attributes of /vesta/foo will be
			      // copied only if this flag is given.
        attrAccess  = 0x0008, // When copying attributes, those whose
	                      // names begin with '#' (that is, access
	                      // control attributes) are omitted unless
			      // this flag is given.
	revive      = 0x0010, // Replace ghosts in the destination with
                              // non-ghost objects where source has them
                              // (except for master ghosts of appendable
                              // dirs).  The destination object master
                              // status is preserved.
        reviveMA    = 0x0020, // Replace master ghosts of appendable
                              // directories when the source has a copy.
                              // The new copy is (necessarily) set to
                              // *nonmaster* status, so the directory no
                              // longer has a master.
        inclStubs   = 0x0040, // Include stubs in the set to be copied.
	                      // Without this flag, all stubs are excluded.
        inclGhosts  = 0x0080, // Include ghosts in the set to be copied.
                              // Without this flag, all ghosts are
                              // excluded.  Even with this flag, a
			      // ghost will not be copied if it would
			      // replace an object already present at
			      // the destination.
	verbose     = 0x0100, // Log activity to standard output.
	test        = 0x0200, // Do not modify the destination
			      // repository.  Useful (with verbose) to
			      // see what would be copied.  Copying to
			      // the destination is not even simulated,
			      // so objects that would be copied only
			      // once without this flag may be shown as
			      // being copied repeatedly.
        latest      = 0x0400, // If an appendable directory A is of
			      // type package, checkout, or session,
			      // and A is or has a descendant in the 
			      // set to be copied, and A/latest is a
			      // stub or ghost, then copy A/latest and
			      // its attributes too.
        warnBadImp  = 0x0800, // If some of the imports of a model
                              // requested are missing in the source
                              // repository, issue a warning but
                              // continue to replicate what we can.  If
                              // turned off, missing imports will cause
                              // the replicator to terminate with an
                              // error message and not replicate
                              // anything.
	dontCopyNew = 0x1000, // do not copy new objects.
	
      };

    // Copy every object in the set specified by the given DirectiveSeq.
    // Special case: If directives is NULL, everything in the source
    // repository is copied.  If directives is an empty sequence, nothing
    // is copied.
    //
    // In order to copy an object, it is sometimes necessary to copy
    // its parent directory as well (and so on upwards).  If an
    // appendable directory is copied only because some of its
    // descendants are in the specified set, it is populated with the
    // minimum necessary set of children.  If an immutable directory
    // is copied for this reason, however, it is necessarily copied in
    // full, including its entire subtree of descendants.  One useful
    // consequence of this is that you can ask for a package version
    // to be copied (in full) by asking just for one of the models in it.
    //
    void replicate(DirectiveSeq* directives, Flags flags)
        throw (Failure, SRPC::failure);

    // Return true if the argument contains any metacharacters that
    // the replicator would expand ('*', '#', '?', '[', '{', '%',
    // "FIRST", "LAST").
    static bool hasMetacharacters(const Text &string) throw();

    // Expand a pattern to a set of fully-qualified pathnames.  This
    // expands all metacharacters.
    static TextSeq expandPattern(const Text &pattern,
				 Text host = "", Text port = "",
				 AccessControl::Identity who = NULL, 
				 Text dhost = "", Text dport = "",
				 AccessControl::Identity dwho = NULL)
      throw (Failure, SRPC::failure, VestaConfig::failure, ReposUI::failure);

    // Same as the above, only handle each match as it is found with a
    // "match callback".
    typedef void (*matchCallback)(void *cl, const Text &match);
    static void expandPattern(const Text &pattern, matchCallback callback,
			      void *closure,
			      Text host = "", Text port = "",
			      AccessControl::Identity who = NULL, 
			      Text dhost = "", Text dport = "",
			      AccessControl::Identity dwho = NULL);

    // Note that this variant of expandPattern can throw any exception
    // thrown by "callback" plus the following exceptions:

    //   Replicator::Failure
    //   SRPC::Failure
    //   VestaConfig::Failure
    //   ReposUI::Failure

    // If "callback" throws no exceptions, then this function could be
    // declared with this throw specification:

    // throw (Replicator::Failure, SRPC::failure,
    //        VestaConfig::failure, ReposUI::failure);

  private:
    // Helper for the following templatized functor version of
    // expandPattern
    template<class _Match_Handler>
    static void match_handler_helper(void *cl, const Text &match)
    {
      _Match_Handler *mh = (_Match_Handler *) cl;
      (*mh)(match);
    }
  public:
    // Same as the above, only handle each match as it is found with a
    // "match handler" functor (which can be either a function that
    // takes a const Text & or a class with operator() defined to take
    // a const Text &).
    template<class _Match_Handler>
    static void expandPattern(const Text &pattern, _Match_Handler &mh,
			      Text host = "", Text port = "",
			      AccessControl::Identity who = NULL, 
			      Text dhost = "", Text dport = "",
			      AccessControl::Identity dwho = NULL)
    // Note that this variant of expandPattern can throw any exception
    // thrown by "mh" plus the other exceptions thrown by the callback
    // variant above.
    {
      matchCallback cb = match_handler_helper<_Match_Handler>;
      
      Replicator::expandPattern(pattern,
				cb, (void *) &mh,
				host, port, who,
				dhost, dport, dwho);
    }

  private:	
    void copyAttribs(VestaSource* svs, Text svsname,
		     VestaSource* dvs, Flags flags)
      throw (Failure, SRPC::failure);
    friend bool doOneEntry(void* closure, VestaSource::typeTag type, Arc arc,
			   unsigned int index, Bit32 pseudoInode,
			   ShortId filesid, bool master);
    friend void getFirstLast(void* closure);
  public:
    Text shost, sport;
    Text dhost, dport;
    // Appendable roots of source and destination repository
    VestaSource* const sroot;
    VestaSource* const droot;
    // Identities used to access source and destination repositories
    AccessControl::Identity const swho;
    AccessControl::Identity const dwho;
};

#endif //_REPLICATOR
