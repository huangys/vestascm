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
// AccessControl.C
//

#include "AccessControl.H"
#include "Recovery.H"
#include "VestaConfig.H"
#include "CharsKey.H"
#include "logging.H"
#include "ReadersWritersLock.H"
#include <pwd.h>
#include <grp.h>
#include <time.h>

#include "timing.H"
#include "lock_timing.H"

//
// Types
//
class UIntKey {
  public:
    unsigned int i;
    inline Word Hash() const throw() { return i; };
    inline UIntKey() { this->i = 0; };
    inline UIntKey(unsigned int i) { this->i = i; };
    inline friend int operator==(const UIntKey& k1, const UIntKey& k2)
      throw() { return k1.i == k2.i; };
};

typedef Table<CharsKey, unsigned int>::Default CharsToUIntTable;
typedef Table<CharsKey, unsigned int>::Iterator CharsToUIntIter;
typedef Table<UIntKey, const char*>::Default UIntToCharsTable;
typedef Table<UIntKey, const char*>::Iterator UIntToCharsIter;
typedef Table<UIntKey, CharsSeq*>::Default UIntToCharsSeqTable;
typedef Table<UIntKey, CharsSeq*>::Iterator UIntToCharsSeqIter;
typedef Table<CharsKey, CharsSeq*>::Default CharsToCharsSeqTable;
typedef Table<CharsKey, CharsSeq*>::Iterator CharsToCharsSeqIter;

class ExportKey {
private:
  Text host_name;
  bool host_name_lookup_done;
public:
  Bit32 addr;
  AccessControl::IdentityRep::Flavor flavor;
  const char* arg;
  inline Word Hash() const throw()
    { return addr ^ (0x12345678 << flavor); };
  inline ExportKey()
    : host_name_lookup_done(false)
    { addr = 0; flavor = AccessControl::IdentityRep::unix_flavor; arg = NULL; }
  inline ExportKey(Bit32 a, AccessControl::IdentityRep::Flavor f, const char*g)
    : host_name_lookup_done(false)
    { addr = a; flavor = f; arg = g; };
  inline friend int operator==(const ExportKey& k1,const ExportKey& k2) throw()
    { return k1.addr == k2.addr && k1.flavor == k2.flavor &&
      ((k1.arg == NULL && k2.arg == NULL) ||
       ((k1.arg != 0) && (k2.arg != 0) && (strcasecmp(k1.arg, k2.arg) == 0))); };

  inline const char *hostname()
  {
    // If we still need to look up the hostname of the address...
    if(!host_name_lookup_done)
      {
	// Make sure the hostname starts empty
	if(!this->host_name.Empty())
	  this->host_name = "";
	try
	  {
	    in_addr i_addr;
	    i_addr.s_addr = this->addr;
	    (void) TCP_sock::addr_to_host(i_addr, this->host_name);
	    host_name_lookup_done = true;
	  }
	catch(...)
	  {
	    // Ignore, but leave host_name_lookup_done set to false
	  }
      }
    // If we have a non-empty hostname, return it
    if(!this->host_name.Empty())
      return this->host_name.cchars();
    // No hostname: return a NULL pointer
    return 0;
  }
};

struct Export {
  static Export* first;
  Export* next;
  const char* pattern; // hostname wildcard, or IPaddress[/netmask]
  enum Level { deny, readOnly, allow };
  Level level;
  AccessControl::IdentityRep::Flavor flavor;
  const char* realm; // realm that global names must be from, NULL=any
  Export(const char* p, Level l, AccessControl::IdentityRep::Flavor f,
	 const char* s, Export *tail)
    : pattern(p), level(l), flavor(f), realm(s),
      next(tail)
  { };

  // Test whether this key matches this export.
  bool match(ExportKey &key);
};

typedef Table<ExportKey, Export*>::Default ExportCache;
typedef Table<ExportKey, Export*>::Iterator ExportCacheIter;

//
// Module globals
//

// These are constant after initialization
const char* vestaGroupFile = NULL;
const char* vestaAliasFile = NULL;
const char* vestaExportFile = NULL;

// Used to protect the following variables.
ReadersWritersLock userGroup_lock(true);

static CharsToUIntTable* globalToUnixUserTable = NULL;
static CharsToUIntTable* globalToUnixGroupTable = NULL;
static UIntToCharsTable* unixToGlobalUserTable = NULL;
static UIntToCharsSeqTable* unixToGlobalGroupsTable = NULL;
static CharsToCharsSeqTable* globalUserToGroupsTable = NULL;
static CharsToCharsSeqTable* globalAliasesTable = NULL;

// Used to protect Export::first and exportCache.
ReadersWritersLock export_lock(true);
Export* Export::first = NULL;
static ExportCache* exportCache = NULL;
unsigned int exportEpoch = 0;

// The time we last reloaded the access control information.  Used to
// avoid doing it too often.
static time_t last_refresh;

// Minimum time since the last refresh when we will perform an
// automatic refresh.
static unsigned int min_auto_refresh_delay = (60*60);

// Should we honor gids sent by the client with a UnixIdentityRep?
static bool honor_client_gids = false;

//
// Forward declarations
//
static const char* unixToGlobalUserNL(uid_t uid) throw ();
static const char* unixToGlobalGroupNL(gid_t gid) throw ();

//
// Methods and functions
//

// Like strdup, but allocate the memory with the new operator so we
// can free it with the delete [] operator.  (This simplifies memory
// management, because we don't have to keep track of which strings
// are allocated with malloc/strdup and which ones are allocated with
// the new operatotr).
static char *strdup_with_new(const char *orig)
{
  char *result = NEW_PTRFREE_ARRAY(char, strlen(orig)+1);
  strcpy(result, orig);
  return result;
}

// Construct a complete copy of a CharsSeq, including copying all its
// elements.  (This simplifies memory management, because we don't
// have to handle one pointer in multiple CharsSeqs.)
static CharsSeq *full_CharSeq_copy(const CharsSeq &orig,
				   unsigned int extra_space = 0)
{
  CharsSeq *result = NEW_CONSTR(CharsSeq, (orig.size() + extra_space));

  for(unsigned int i = 0; i < orig.size(); i++)
    {
      result->addhi(strdup_with_new(orig.get(i)));
    }

  return result;
}

// Skip indicated character; error if other character seen.
static void
require(FILE* f, int& c, char req) throw(AccessControl::ParseError)
{
  if (c == EOF) throw(AccessControl::ParseError("incomplete line"));
  if (c != req) {
    throw(AccessControl::ParseError(Text("expected '") + req + "'; got '" +
				    (char)c + "'"));
  }
  c = getc(f);
}

// Whitespace character within a line?
static bool
white(int c)
{
  return c == ' ' || c == '\t' || c == '\r';
}

// Skip whitespace within a line.
static void
skipWhite(FILE* f, int& c)
{
  while (white(c)) {
    c = getc(f);
  }
}

// Skip the rest of the line.  Error if EOF before newline seen.
static void
skipLine(FILE* f, int& c) throw(AccessControl::ParseError)
{
  while (c != '\n' && c != EOF) {
    c = getc(f);
  }
  if (c == EOF) throw(AccessControl::ParseError("incomplete line"));
  c = getc(f);
}

// Skip a comment, a blank line, or leading space on a line.
// Return true if we skipped to the next line.
static bool
skipComment(FILE* f, int& c)
{
  skipWhite(f, c);
  if (c == '/') {
    c = getc(f);
    require(f, c, '/');
    skipLine(f, c);
    return true;
  }
  if (c == ';' || c == '#' || c == '\n') {
    skipLine(f, c);
    return true;
  }
  return false;
}

// Skip whitespace, then skip a comma if present
static void
skipOptionalComma(FILE* f, int&c)
{
  skipWhite(f, c);
  if (c == ',') c = getc(f);
}

// Parse out a name.  Any characters other than whitespace, newline,
// colon, or comma are accepted.  Error if name ends with EOF.
#define MAXNAMELEN 256
static void
parseName(FILE* f, int& c, char name[MAXNAMELEN])
  throw(AccessControl::ParseError)
{
  int i = 0;
  while (i < MAXNAMELEN) {
    name[i++] = c;
    c = getc(f);
    if (white(c) || c == ':' || c == ',' || c == '\n') break;
    if (c == EOF) throw(AccessControl::ParseError("incomplete line"));
  }
  if (i >= MAXNAMELEN) throw(AccessControl::ParseError("name too long"));
  name[i] = '\0';
  if (i == 0) throw(AccessControl::ParseError("empty name"));
}

// Parse out a filename.  Any characters other than whitespace or
// newline are accepted.  Error if name ends with EOF.
#define MAXNAMELEN 256
static void
parseFilename(FILE* f, int& c, char name[MAXNAMELEN])
  throw(AccessControl::ParseError)
{
  int i = 0;
  while (i < MAXNAMELEN) {
    name[i++] = c;
    c = getc(f);
    if (white(c) || c == '\n') break;
    if (c == EOF) throw(AccessControl::ParseError("incomplete line"));
  }
  if (i >= MAXNAMELEN) throw(AccessControl::ParseError("name too long"));
  name[i] = '\0';
  if (i == 0) throw(AccessControl::ParseError("empty name"));
}

// Add the default realm to this name if it doesn't already have a
// realm specifier.
static void ensure_realm(char name[MAXNAMELEN] /*IN/OUT*/)
  throw(AccessControl::ParseError)
{
  // See if it has a realm
  if(strchr(name, '@') == 0)
    {
      // Make sure adding the realm won't overrun the buffer
      if((strlen(name) + AccessControl::realmlen + 2) > MAXNAMELEN)
	{
	  throw(AccessControl::ParseError("name too long to add default realm"));
	}
      // Add the realm
      strcat(name, "@");
      strcat(name, AccessControl::realm);
    }
}

// Parse a membership file, in the format
//
//   ; optional comments, can appear anywhere
//   # optional comments, can appear anywhere
//   // optional comments, can appear anywhere
//   . includefile
//   name : [value [,]]... [value]
//   ...
//
// If "name" is already in the table (including appearing multiple times
// in the file), the values accumulate.  [,] is an optional comma.
// Where whitespace is shown, zero or more spaces, tabs, or \r characters
// can appear.
//
// The "reject" argument is not permitted as a member.

static void
parseMembershipFile(const char* filename, CharsToCharsSeqTable* tbl,
		    const char* reject ="")
  throw(AccessControl::ParseError)
{
  FILE* f = fopen(filename, "r");
  if (f == NULL)
    {
      int saved_errno = errno;
      Text etxt = Basics::errno_Text(saved_errno);
      throw(AccessControl::ParseError(Text(filename) + ": " + etxt));
    }
  int c = getc(f);
  for (;;) {
    // Handle comments and includes
    while (skipComment(f, c)) ;
    if (c == EOF) break;
    if (c == '.') {
      c = getc(f);
      skipWhite(f, c);
      char filename2[MAXNAMELEN];
      parseFilename(f, c, filename2);
      parseMembershipFile(filename2, tbl);
    }
    while (skipComment(f, c)) ;
    if (c == EOF) break;

    // Get name
    char name[MAXNAMELEN];
    parseName(f, c, name);
    // Make sure that it has a realm
    ensure_realm(name);
    skipWhite(f, c);
    require(f, c, ':');
    CharsSeq* seq;
    if (!tbl->Get(name, seq)) {
      seq = NEW_CONSTR(CharsSeq, (1));
      tbl->Put(strdup_with_new(name), seq);
    }
    for (;;) {
      // Get members
      char value[MAXNAMELEN];
      if (skipComment(f, c)) break;
      if (c == '\n' || c == EOF) break;
      parseName(f, c, value);
      // Make sure that it has a realm
      ensure_realm(name);
      //cerr << name << ": " << value << endl;
      if (strcasecmp(value, reject) == 0) {
	throw(AccessControl::ParseError(Text(value) + " not permitted"));
      }
      seq->addhi(strdup_with_new(value));
      skipOptionalComma(f, c);
    }
  }
  fclose(f);
}

// Parse the export file.  Format:
//
//   ; optional comments, can appear anywhere
//   # optional comments, can appear anywhere
//   // optional comments, can appear anywhere
//   . includefile
//   pattern [:] [level [flavor [arg]],]... [level [flavor [arg]]]
//   ...
//
// A pattern can be a DNS hostname, a DNS hostname pattern with ? or *
// wildcards (which will not match a "."), an IP address in dotted
// notation, or an IP subnet in the form address/netmask, where
// netmask can be in dotted notation, or can be a single integer to
// indicate how many bits are set from the left.  (E.g., a netmask of
// 24 represents 255.255.255.0.)
//
// level ::= allow, rw, readwrite (synonyms); readonly, ro (synonyms); deny
// flavor ::=  unix_flavor; global; gssapi; all, any (synonyms)
//
// If no "level flavor arg" tuples appear, "allow all" is supplied by
// default; otherwise we start with "deny all" and then apply the
// pairs that appear.  If "flavor" is omitted from a tuple, it defaults
// to "all".
//
// arg is an argument to the flavor.  Currently only "allow global" or
// "readonly global" takes an argument: a pattern for the realm
// that the global name must originate from.  If this arg is omitted, any
// global name is OK.
//
// When checking whether a client request should be admitted, we scan
// the export file in reverse order, looking for the first (pattern,
// flavor, arg) that matches.  If one is found, its level applies to
// this request.  If not, the request is denied.


static void
parseExportFile(const char* filename, Export *&head)
  throw(AccessControl::ParseError)
{
  char pattern[MAXNAMELEN], token[MAXNAMELEN], argbuf[MAXNAMELEN];

  FILE* f = fopen(filename, "r");
  if (f == NULL)
    {
      int saved_errno = errno;
      Text etxt = Basics::errno_Text(saved_errno);
      throw(AccessControl::ParseError(Text(filename) + ": " + etxt));
    }
  int c = getc(f);

  for (;;) {
    // Handle comments and includes
    while (skipComment(f, c)) ;
    if (c == EOF) break;
    if (c == '.') {
      c = getc(f);
      skipWhite(f, c);
      char filename2[MAXNAMELEN];
      parseFilename(f, c, filename2);
      parseExportFile(filename2, head);
    }
    while (skipComment(f, c)) ;
    if (c == EOF) break;

    // Get pattern
    parseName(f, c, pattern);
    Export::Level level;
    AccessControl::IdentityRep::Flavor flavor;
    const char* arg;

    if (c == ':') c = getc(f);
    for (;;) {
      // Get "level flavor arg" tuple
      if (skipComment(f, c)) break;
      if (c == EOF) throw(AccessControl::ParseError("incomplete line"));
      if (c == '\n') break;

      // Get level
      parseName(f, c, token);
      if (strcasecmp(token, "allow") == 0 ||
	  strcasecmp(token, "readwrite") == 0 ||
	  strcasecmp(token, "rw") == 0) {
	level = Export::allow;

      } else if (strcasecmp(token, "readonly") == 0 ||
		   strcasecmp(token, "ro") == 0) {
	level = Export::readOnly;

      } else if (strcasecmp(token, "deny") == 0) {
	level = Export::deny;

      } else {
	throw(AccessControl::ParseError(Text("unknown level ") + token));
      }

      // Get flavor
      flavor = AccessControl::IdentityRep::unspecified; // default
      skipWhite(f, c);
      if (c == EOF) throw(AccessControl::ParseError("incomplete line"));
      if (c != '\n' && c != ',') {
	parseName(f, c, token);
	if (strcasecmp(token, "unix") == 0) {
	  flavor = AccessControl::IdentityRep::unix_flavor;
	} else if (strcasecmp(token, "global") == 0) {
	  flavor = AccessControl::IdentityRep::global;
	} else if (strcasecmp(token, "gssapi") == 0) {
	  flavor = AccessControl::IdentityRep::gssapi;
	} else if (strcasecmp(token, "any") == 0 ||
		   strcasecmp(token, "all") == 0) {
	  flavor = AccessControl::IdentityRep::unspecified;
	} else {
	  throw(AccessControl::ParseError(Text("unknown flavor ") + token));
	}
      }

      arg = NULL; // default
      if (flavor == AccessControl::IdentityRep::global ||
	  flavor == AccessControl::IdentityRep::gssapi) {
	// Get arg
	skipWhite(f, c);
	if (c == EOF) throw(AccessControl::ParseError("incomplete line"));
	if (c != ',' && c != '\n') {
	  parseName(f, c, argbuf);
	  arg = strdup_with_new(argbuf);
	}
      }

      head = NEW_CONSTR(Export, (strdup_with_new(pattern), level,
				 flavor, arg, head));
      //cerr << pattern << ": " << level << " " << flavor << " " <<
      //  (arg ? arg : NULL) << endl;
      if (skipComment(f, c)) break;
      if (c != '\n') require(f, c, ',');
    }
  }
  fclose(f);
}


// Like Sequence::addhi, but don't add if this name is already in the sequence
static void
addhi_nondup(CharsSeq* seq, const char* name)
{
  int i;
  int sz = seq->size();
  for (i=0; i<sz; i++) {
    if (strcmp(seq->get(i), name) == 0) return;
  }
  seq->addhi(strdup_with_new(name));
}


// Free a set of user/group access tables that are no longer needed.
// These may be either old tables replaced by new ones, or ones which
// could not be constructed due to a parsing error.
static void
freeUserGroupTables(CharsToUIntTable *dead_globalToUnixUserTable,
		    CharsToUIntTable *dead_globalToUnixGroupTable,
		    UIntToCharsTable *dead_unixToGlobalUserTable,
		    UIntToCharsSeqTable *dead_unixToGlobalGroupsTable,
		    CharsToCharsSeqTable *dead_globalUserToGroupsTable,
		    CharsToCharsSeqTable *dead_globalAliasesTable) throw()
{
  if(dead_globalToUnixUserTable != NULL)
    {
      // Each CharsKey has an allocated string that needs to be freed.
      // However, we can't free them until we delete the table.  So,
      // we gather them up, delete the table, then delete the key
      // strings.
      CharsSeq dead_keys;
      CharsToUIntIter iter(dead_globalToUnixUserTable);
      CharsKey key;
      unsigned int val;
      while(iter.Next(key, val))
	{
	  assert(key.s != NULL);
	  dead_keys.addhi(key.s);
	}

      // Delte the table.
      delete dead_globalToUnixUserTable;

      // Delete the key strings.
      for(unsigned int i = 0; i < dead_keys.size(); i++)
	{
	  const char *key_str = dead_keys.get(i);
	  assert(key_str != NULL);
	  delete [] key_str;
	}
    }
  if(dead_globalToUnixGroupTable != NULL)
    {
      // Gather the key strings to be freed.
      CharsSeq dead_keys;
      CharsToUIntIter iter(dead_globalToUnixGroupTable);
      CharsKey key;
      unsigned int val;
      while(iter.Next(key, val))
	{
	  assert(key.s != NULL);
	  dead_keys.addhi(key.s);
	}

      // Delte the table.
      delete dead_globalToUnixGroupTable;

      // Delete the key strings.
      for(unsigned int i = 0; i < dead_keys.size(); i++)
	{
	  const char *key_str = dead_keys.get(i);
	  assert(key_str != NULL);
	  delete [] key_str;
	}
    }
  if(dead_unixToGlobalUserTable != NULL)
    {
      // The values in this table are pointers that need to be freed.
      UIntToCharsIter iter(dead_unixToGlobalUserTable);
      UIntKey key;
      const char *val;
      while(iter.Next(key, val))
	{
	  assert(val != NULL);
	  delete [] val;
	}

      // Delete the table itself.
      delete dead_unixToGlobalUserTable;
    }
  if(dead_unixToGlobalGroupsTable != NULL)
    {
      // The values in this table need to be freed.
      UIntToCharsSeqIter iter(dead_unixToGlobalGroupsTable);
      UIntKey key;
      CharsSeq *val;
      while(iter.Next(key, val))
	{
	  assert(val != NULL);
	  // The sequence elements must be deleted as well.
	  while(val->size() > 0)
	    {
	      const char *val_elem = val->remhi();
	      assert(val_elem != NULL);
	      delete [] val_elem;
	    }

	  // Delete the now empty sequence.
	  delete val;
	}

      // delete the table itself.
      delete dead_unixToGlobalGroupsTable;
    }

  if(dead_globalUserToGroupsTable != NULL)
    {
      // The values in this table need to be freed.
      CharsSeq dead_keys;
      CharsToCharsSeqIter iter(dead_globalUserToGroupsTable);
      CharsKey key;
      CharsSeq *val;
      while(iter.Next(key, val))
	{
	  assert(key.s != NULL);
	  dead_keys.addhi(key.s);

	  assert(val != NULL);
	  // The sequence elements must be deleted as well.
	  while(val->size() > 0)
	    {
	      const char *val_elem = val->remhi();
	      assert(val_elem != NULL);
	      delete [] val_elem;
	    }
	  delete val;
	}

      // Delete the table itself.
      delete dead_globalUserToGroupsTable;

      // Delete the key strings.
      for(unsigned int i = 0; i < dead_keys.size(); i++)
	{
	  const char *key_str = dead_keys.get(i);
	  assert(key_str != NULL);
	  delete [] key_str;
	}
    }

  if(dead_globalAliasesTable != NULL)
    {
      // The values in this table need to be freed.
      CharsSeq dead_keys;
      CharsToCharsSeqIter iter(dead_globalAliasesTable);
      CharsKey key;
      CharsSeq *val;
      while(iter.Next(key, val))
	{
	  assert(key.s != NULL);
	  dead_keys.addhi(key.s);

	  assert(val != NULL);
	  // The sequence elements must be deleted as well.
	  while(val->size() > 0)
	    {
	      const char *val_elem = val->remhi();
	      assert(val_elem != NULL);
	      delete [] val_elem;
	    }
	  delete val;
	}

      // delete the table itself.
      delete dead_globalAliasesTable;

      // Delete the key strings.
      for(unsigned int i = 0; i < dead_keys.size(); i++)
	{
	  const char *key_str = dead_keys.get(i);
	  assert(key_str != NULL);
	  delete [] key_str;
	}
    }
}

// Free a set of export access tables that are no longer needed.
// These may be either old tables replaced by new ones, or ones which
// could not be constructed due to a parsing error.
static void
freeExportTables(Export* dead_exportList,
		 ExportCache *dead_exportCache) throw()
{
  while(dead_exportList != NULL)
    {
      assert(dead_exportList->pattern != NULL);
      delete [] dead_exportList->pattern;
      if(dead_exportList->realm != NULL)
	{
	  delete [] dead_exportList->realm;
	}

      // Save the next element in the linked list.
      Export *next = dead_exportList->next;

      // Delete this element and move on to the next (if any).
      delete dead_exportList;
      dead_exportList = next;
    }

  if(dead_exportCache != NULL)
    {
      CharsSeq dead_keys;
      Table<ExportKey, Export*>::Iterator iter(dead_exportCache);
      ExportKey key;
      Export *val;
      while(iter.Next(key, val))
	{
	  // The values are just references to elements of the export
	  // list (which we've already deleted).  However the key may
	  // contain an allocated string that we need to delete.
	  if(key.arg != NULL)
	    {
	      dead_keys.addhi(key.arg);
	    }
	}

      // Delete the table itself.
      delete dead_exportCache;

      // Delete the realm strings.
      for(unsigned int i = 0; i < dead_keys.size(); i++)
	{
	  const char *key_str = dead_keys.get(i);
	  assert(key_str != NULL);
	  delete [] key_str;
	}
    }
}

// Make internal tables from the passwd and group files.
static void
newUserGroupTables(bool continue_on_error = true) throw(AccessControl::ParseError)
{
  // Locals representing the new tables we're constructing.
  CharsToUIntTable *new_globalToUnixUserTable = NULL;
  CharsToUIntTable *new_globalToUnixGroupTable = NULL;
  UIntToCharsTable *new_unixToGlobalUserTable = NULL;
  UIntToCharsSeqTable *new_unixToGlobalGroupsTable = NULL;
  CharsToCharsSeqTable *new_globalUserToGroupsTable = NULL;
  CharsToCharsSeqTable *new_globalAliasesTable = NULL;

  try
    {
      //
      // 1. Build maps between local unix uids and global user names
      //
      new_globalToUnixUserTable = NEW(CharsToUIntTable);
      new_unixToGlobalUserTable = NEW(UIntToCharsTable);

      { // start of mutex allocation block
	OS::Passwd pw_ent;
	OS::PasswdIter iterator;
	while(iterator.Next(pw_ent)) {
	  char* name = NEW_PTRFREE_ARRAY(char, (pw_ent.name.Length() +
						AccessControl::realmlen + 2));
	  strcpy(name, pw_ent.name.cchars());
	  strcat(name, "@");
	  strcat(name, AccessControl::realm);
	  new_globalToUnixUserTable->Put(name, pw_ent.uid);
	  const char *dummy;
	  if(!new_unixToGlobalUserTable->Get(UIntKey(pw_ent.uid), dummy)) {
	    new_unixToGlobalUserTable->Put(UIntKey(pw_ent.uid),
					   strdup_with_new(name));
	  }
	}
      } // end of mutex allocation block

      //
      // 2. Build maps between local unix gids and global group names
      //
      new_globalToUnixGroupTable = NEW(CharsToUIntTable);
      new_unixToGlobalGroupsTable = NEW(UIntToCharsSeqTable);

      { // start of mutex allocation block
	OS::Group gr_ent;
	OS::GroupIter iterator;
	while(iterator.Next(gr_ent)) {
	  char* gname = NEW_PTRFREE_ARRAY(char, (gr_ent.name.Length() +
						 AccessControl::realmlen + 3));
	  strcpy(gname, "^");
	  strcat(gname, gr_ent.name.cchars());
	  strcat(gname, "@");
	  strcat(gname, AccessControl::realm);
	  new_globalToUnixGroupTable->Put(gname, gr_ent.gid);
	  CharsSeq* seq;
	  if(!new_unixToGlobalGroupsTable->Get(UIntKey(gr_ent.gid), seq)) {
	    seq = NEW_CONSTR(CharsSeq, (1));
	    new_unixToGlobalGroupsTable->Put(UIntKey(gr_ent.gid), seq);
	  }
	  seq->addhi(strdup_with_new(gname));
	}
      } // end of mutex allocation block

      //
      // 3. Build table of known global group memberships for global
      // user names
      //
      // 3a. Get each local unix user's primary group from the passwd
      // file
      //
      new_globalUserToGroupsTable = NEW(CharsToCharsSeqTable);
      { // start of mutex allocation block
	OS::Passwd pw_ent;
	OS::PasswdIter iterator;
	while(iterator.Next(pw_ent)) {
	  char* name = NEW_PTRFREE_ARRAY(char, (pw_ent.name.Length() +
						AccessControl::realmlen + 2));
	  strcpy(name, pw_ent.name.cchars());
	  strcat(name, "@");
	  strcat(name, AccessControl::realm);
	  CharsSeq* seq;
	  if(new_unixToGlobalGroupsTable->Get(pw_ent.gid, seq)) {
	    seq = full_CharSeq_copy(*seq);
	  } 
	  else {
	    // user's primary gid is not in group file
	    // !!should we enter it as a numeric group name?
	    seq = NEW_CONSTR(CharsSeq, (0));
	  }
	  new_globalUserToGroupsTable->Put(name, seq);
	}
      } // end of mutex allocation block

      //
      // 3b. Get the additional local unix users in each group from
      // the group file
      //
      
      { // start of mutex allocation block
	OS::Group gr_ent;
	OS::GroupIter iterator;
	while(iterator.Next(gr_ent)) {
	  CharsSeq* gnames;
	  bool found = new_unixToGlobalGroupsTable->Get(gr_ent.gid, gnames);
	  if (!found)
	    // rare but possible; group file changed since step 2
	    continue;
	  for(int i = 0; i < gr_ent.members.size(); i++) {
	    Text grmem = gr_ent.members.get(i);
	    char* name = NEW_PTRFREE_ARRAY(char, (grmem.Length() +
						  AccessControl::realmlen + 2));
	    strcpy(name, grmem.cchars());
	    strcat(name, "@");
	    strcat(name, AccessControl::realm);
	    CharsSeq* seq;
	    if(new_globalUserToGroupsTable->Get(name, seq)) {
	      // Add this group under all its names
	      delete[] name;
	      for(int i = 0; i < gnames->size(); i++) {
		addhi_nondup(seq, gnames->get(i)); 
	      }
	    }
	    else {
	      // User in a group but not in passwd file.  Add anyway (?).
	      seq = full_CharSeq_copy(*gnames);
	      new_globalUserToGroupsTable->Put(name, seq);
	    }
	  }
	}
      } // end of mutex allocation block
      
      //
      // 3c. Add more global users and groups from the Vesta group
      // file
      //
      try {
	assert(vestaGroupFile != NULL);
	parseMembershipFile(vestaGroupFile, new_globalUserToGroupsTable);
      } catch (AccessControl::ParseError f) {
	Repos::dprintf(DBG_ALWAYS, "error parsing group file %s: %s\n",
		       vestaGroupFile, f.message.cchars());
	if(!continue_on_error)
	  {
	    f.fname = vestaGroupFile;
	    f.fkind = "group file";
	    throw f;
	  }
      }

      //
      // 4. Build table of global user and group name aliases from
      // Vesta alias file
      //
      new_globalAliasesTable = NEW(CharsToCharsSeqTable);
      try {
	assert(vestaAliasFile != NULL);
	parseMembershipFile(vestaAliasFile, new_globalAliasesTable,
			    AccessControl::rootUser);
      } catch (AccessControl::ParseError f) {
	Repos::dprintf(DBG_ALWAYS, "error parsing alias file %s: %s\n",
		       vestaAliasFile, f.message.cchars());
	if(!continue_on_error)
	  {
	    f.fname = vestaAliasFile;
	    f.fkind = "alias file";
	    throw f;
	  }
      }

      //
      // 4a. Add still more groups to globalUserToGroupsTable by
      // expanding global group aliases
      //
      CharsToCharsSeqIter iter(new_globalUserToGroupsTable);
      CharsKey namekey;
      CharsSeq* gnames;
      while (iter.Next(namekey, gnames)) {
	int i;
	int sz = gnames->size();
	for (i=0; i<sz; i++) {
	  CharsSeq* galiases;
	  if (new_globalAliasesTable->Get(gnames->get(i), galiases)) {
	    int j;
	    for (j=0; j<galiases->size(); j++) {
	      addhi_nondup(gnames, galiases->get(j));
	    }
	  }
	}
      }

      //
      // 4b. Add yet more groups by giving users the groups of their
      // aliases
      //
      CharsToCharsSeqIter iter2(new_globalAliasesTable);
      CharsSeq* aliases;
      while (iter2.Next(namekey, aliases)) {
	if (namekey.s[0] == '^') continue;
	if (!new_globalUserToGroupsTable->Get(namekey.s, gnames)) {
	  gnames = NEW(CharsSeq);
	  new_globalUserToGroupsTable->Put(strdup_with_new(namekey.s),
					   gnames);
	}
	int i;
	for (i=0; i<aliases->size(); i++) {
	  CharsSeq* gnames2;
	  if (new_globalUserToGroupsTable->Get(aliases->get(i), gnames2)) {
	    int j;
	    for (j=0; j<gnames2->size(); j++) {
	      addhi_nondup(gnames, gnames2->get(j));
	    }
	  }
	}
      }
    }
  catch(...)
    {
      // If we fail due to a parsing error, free any partially
      // constructed tables.
      freeUserGroupTables(new_globalToUnixUserTable,
			  new_globalToUnixGroupTable,
			  new_unixToGlobalUserTable,
			  new_unixToGlobalGroupsTable,
			  new_globalUserToGroupsTable,
			  new_globalAliasesTable);
      throw;
    }

  // Now acquire the global lock on the tables so we can replace the
  // existing ones (if any) with our new ones).
  userGroup_lock.acquireWrite();
  RWLOCK_LOCKED_REASON(&userGroup_lock, "newUserGroupTables");

  // Variables used to collect the old tables so we can free them
  // after replacing them.
  CharsToUIntTable *old_globalToUnixUserTable = 0;
  CharsToUIntTable *old_globalToUnixGroupTable = 0;
  UIntToCharsTable *old_unixToGlobalUserTable = 0;
  UIntToCharsSeqTable *old_unixToGlobalGroupsTable = 0;
  CharsToCharsSeqTable *old_globalUserToGroupsTable = 0;
  CharsToCharsSeqTable *old_globalAliasesTable = 0;

  // Replace the tables with the new ones.
  old_globalToUnixUserTable = globalToUnixUserTable;
  old_globalToUnixGroupTable = globalToUnixGroupTable;
  old_unixToGlobalUserTable = unixToGlobalUserTable;
  old_unixToGlobalGroupsTable = unixToGlobalGroupsTable;
  old_globalUserToGroupsTable = globalUserToGroupsTable;
  old_globalAliasesTable = globalAliasesTable;

  globalToUnixUserTable = new_globalToUnixUserTable;
  globalToUnixGroupTable = new_globalToUnixGroupTable;
  unixToGlobalUserTable = new_unixToGlobalUserTable;
  unixToGlobalGroupsTable = new_unixToGlobalGroupsTable;
  globalUserToGroupsTable = new_globalUserToGroupsTable;
  globalAliasesTable = new_globalAliasesTable;

  // Remember that we've refreshed the tables.
  last_refresh = time(0);

  // If we're supposed to, display the new access control tables.
  // (Note tht we have to do this before releasing the locks.)
  if (Repos::isDebugLevel(DBG_ACCESS)) {
    CharsToUIntIter iter1(new_globalToUnixUserTable);
    Repos::dprintf(DBG_ACCESS, "\nglobalToUnixUserTable:\n");
    CharsKey namekey;
    unsigned int id;
    while (iter1.Next(namekey, id)) {
      Repos::dprintf(DBG_ACCESS, "%s\t-> %u\n", namekey.s, id);
    }

    CharsToUIntIter iter2(new_globalToUnixGroupTable);
    Repos::dprintf(DBG_ACCESS, "\nglobalToUnixGroupTable:\n");
    while (iter2.Next(namekey, id)) {
      Repos::dprintf(DBG_ACCESS, "%s\t-> %u\n", namekey.s, id);
    }

    UIntToCharsIter iter3(new_unixToGlobalUserTable);
    Repos::dprintf(DBG_ACCESS, "\nunixToGlobalUserTable:\n");
    UIntKey idkey;
    const char* name;
    while (iter3.Next(idkey, name)) {
      Repos::dprintf(DBG_ACCESS, "%u\t-> %s\n", idkey.i, name);
    }
    
    UIntToCharsSeqIter iter4(new_unixToGlobalGroupsTable);
    CharsSeq* seq;
    Repos::dprintf(DBG_ACCESS, "\nunixToGlobalGroupsTable:\n");
    while (iter4.Next(idkey, seq)) {
      Repos::dprintf(DBG_ACCESS, "%u\t-> %s\n", idkey.i, seq->get(0));
      int i;
      for (i=1; i<seq->size(); i++) {
	Repos::dprintf(DBG_ACCESS, "\t   %s\n", seq->get(i));
      }
    }
    
    CharsToCharsSeqIter iter5(new_globalUserToGroupsTable);
    Repos::dprintf(DBG_ACCESS, "\nglobalUserToGroupsTable:\n");
    while (iter5.Next(namekey, seq)) {
      Repos::dprintf(DBG_ACCESS, "%s\t-> %s\n",
		     namekey.s, seq->size() ? seq->get(0) : "");
      int i;
      for (i=1; i<seq->size(); i++) {
	Repos::dprintf(DBG_ACCESS, "\t   %s\n", seq->get(i));
      }
    }

    CharsToCharsSeqIter iter6(new_globalAliasesTable);
    Repos::dprintf(DBG_ACCESS, "\nglobalAliasesTable:\n");
    while (iter6.Next(namekey, seq)) {
      Repos::dprintf(DBG_ACCESS, "%s\t-> %s\n", namekey.s, seq->get(0));
      int i;
      for (i=1; i<seq->size(); i++) {
	Repos::dprintf(DBG_ACCESS, "\t   %s\n", seq->get(i));
      }
    }
  }

  // Release the locks now that we've rpelaced the tables.
  userGroup_lock.releaseWrite();

  // Free all the old tables (if there were any).
  freeUserGroupTables(old_globalToUnixUserTable,
		      old_globalToUnixGroupTable,
		      old_unixToGlobalUserTable,
		      old_unixToGlobalGroupsTable,
		      old_globalUserToGroupsTable,
		      old_globalAliasesTable);
}

static void
newExportTables(bool continue_on_error = true) throw(AccessControl::ParseError)
{
  // Locals representing the new tables we're constructing.
  Export* new_exportList = NULL;
  ExportCache *new_exportCache = NULL;

  try
    {
      // Build export table from Vesta export file
      new_exportCache = NEW(ExportCache);
      try {
	parseExportFile(vestaExportFile, new_exportList);
      } catch (AccessControl::ParseError f) {
	Repos::dprintf(DBG_ALWAYS, "error parsing export file %s: %s\n",
		       vestaExportFile, f.message.cchars());
	if(!continue_on_error)
	  {
	    f.fname = vestaExportFile;
	    f.fkind = "export file";
	    throw f;
	  }
      }
    }
  catch(...)
    {
      // If we fail due to a parsing error, free any partially
      // constructed tables.
      freeExportTables(new_exportList,
		       new_exportCache);
      throw;
    }

  // Propagate keys from exportCache to new_exportCache with a read
  // lock on export_lock
  export_lock.acquireRead();
  if(exportCache != 0)
    {
      ExportCacheIter it(exportCache);
      ExportKey key; Export *val;
      while(it.Next(key, val))
	{
	  // Copy the argument (if any), as the old one will be freed.
	  if (key.arg) key.arg = strdup_with_new(key.arg);

	  // Insert an entry for this key into the new exportCache.
	  new_exportCache->Put(key, 0);
	}
    }
  export_lock.releaseRead();

  // Re-verify each key (client address) agains the new export rules
  // without export_lock held.  (This could take a while given reverse
  // DNS latency.)
  {
    ExportCacheIter it(new_exportCache);
    ExportKey key; Export *exp;
    while(it.Next(key, exp))
      {
	// Search for a matching export entry from the new list.
	exp = new_exportList;
	while((exp != NULL) && !exp->match(key))
	  exp = exp->next;

	// Update the entry for this key in the new exportCache with
	// the matching export rule (if any).
	new_exportCache->Put(key, exp, false);
      }
  }

  // Now acquire the global lock on the tables so we can replace the
  // existing ones (if any) with our new ones).
  export_lock.acquireWrite();

  // Variables used to collect the old tables so we can free them
  // after replacing them.
  Export* old_exportList = 0;
  ExportCache *old_exportCache = 0;

  // Replace the tables with the new ones.
  old_exportList = Export::first;
  old_exportCache = exportCache;

  Export::first = new_exportList;
  exportCache = new_exportCache;
  exportEpoch++;

  // If we're supposed to, display the new access control tables.
  // (Note tht we have to do this before releasing the locks.)
  if (Repos::isDebugLevel(DBG_ACCESS)) {
    Export* exp = new_exportList;
    Repos::dprintf(DBG_ACCESS, "\nexportTable:\n");
    while (exp) {
      Repos::dprintf(DBG_ACCESS, "%s %d %s -> %d\n",
		     exp->pattern, exp->flavor, exp->realm, exp->level);
      exp = exp->next;
    }
  }

  // Release the locks now that we've rpelaced the tables.
  export_lock.releaseWrite();

  // Free all the old tables (if there were any).
  freeExportTables(old_exportList,
		   old_exportCache);
}

// Call newTables if we haven't refreshed the access control tables
// recently.
static bool maybeNewTables()
{
  bool reload_complete = false;

  static Basics::mutex reloading_mu;
  static int reloading_in_progress = 0;

  // If it's been long enough since we last refreshed the tables...
  time_t now = time(0);
  if((now - last_refresh) > min_auto_refresh_delay)
    {
      // Make sure only one thread tries to re-load the user/group
      // information at a time.
      reloading_mu.lock();
      if(reloading_in_progress > 0)
	{
	  reloading_mu.unlock();
	  return false;
	}
      reloading_in_progress++;
      reloading_mu.unlock();

      // This function is called with the a read lock on the user and
      // group control tables held.  We unlock it now because
      // newTables will lock it when it replaces the access tables.
      userGroup_lock.releaseRead();
      try
	{
	  Repos::dprintf(DBG_ALWAYS,
			 "Starting automatic refresh of user/group access control information\n");
	  newUserGroupTables(/*continue_on_error=*/false);
	  Repos::dprintf(DBG_ALWAYS,
			 "Completed automatic refresh of user/group access control information\n");

	  // If we make it here without an exception, the relaod
	  // completed successfully.
	  reload_complete = true;
	}
      catch(...)
	{
	  Repos::dprintf(DBG_ALWAYS,
			 "WARNING: Automatic refresh of user/group access control information failed\n");
	}

      // Note that we're done with the automatic re-load
      reloading_mu.lock();
      reloading_in_progress--;
      assert(reloading_in_progress == 0);
      reloading_mu.unlock();

      // Reacquire the lock on the user and group tables.
      userGroup_lock.acquireRead();
    }
  return reload_complete;
}

const char*
AccessControl::GlobalIdentityRep::user(int n) throw ()
{
  if (n == 0) {
    return user_;
  }
  return this->AccessControl::IdentityRep::user(n);
}

const char*
AccessControl::GlobalIdentityRep::group(int n) throw ()
{
  return this->AccessControl::IdentityRep::group(n);
}

void AccessControl::GlobalIdentityRep::fill_caches() throw()
{
  assert(users_cache == 0);
  assert(groups_cache == 0);

  userGroup_lock.acquireRead();
  CharsSeq* seq;
  RWLOCK_LOCKED_REASON(&userGroup_lock, "GlobalIdentityRep::fill_caches");
  if (globalAliasesTable->Get(user_, seq))
    {
      users_cache = full_CharSeq_copy(*seq, 1);
    }
  else
    {
      users_cache = NEW_CONSTR(CharsSeq, (1));
    }
  users_cache->addlo(strdup_with_new(user_));

  // A GlobalIdentity user gets exactly the global groups associated
  // with his global name.  There is no support for groups that the
  // user entered by typing a passwd to the Unix newgrp command.
  if (globalUserToGroupsTable->Get(user_, seq))
    {
      groups_cache = full_CharSeq_copy(*seq);
    }
  else
    {
      // User has *no* group memberships in this case
      groups_cache = NEW_CONSTR(CharsSeq, (1));
    }
  userGroup_lock.releaseRead();
}

const char*
AccessControl::UnixIdentityRep::user(int n) throw ()
{
  return this->AccessControl::IdentityRep::user(n);
}

const char*
AccessControl::UnixIdentityRep::group(int n) throw ()
{
  return this->AccessControl::IdentityRep::group(n);
}

void AccessControl::UnixIdentityRep::fill_caches() throw()
{
  assert(users_cache == 0);
  assert(groups_cache == 0);

  userGroup_lock.acquireRead();

  RWLOCK_LOCKED_REASON(&userGroup_lock, "UnixIdentityRep::fill_caches (initial)");

  const char *user = unixToGlobalUserNL(aup_->aup_uid);

  RWLOCK_LOCKED_REASON(&userGroup_lock, "UnixIdentityRep::fill_caches (post-user)");

  CharsSeq* seq;
  if(globalAliasesTable->Get(user, seq))
    {
      users_cache = full_CharSeq_copy(*seq, 1);
    }
  else
    {
      users_cache = NEW_CONSTR(CharsSeq, (1));
    }
  users_cache->addlo(strdup_with_new(user));

  // A UnixIdentity user gets all the global groups associated with
  // his global name.

  // In addition, he may get the translation of each of the local gids
  // that are listed in his AuthUnix structure to a global group.
  // This feature has several problems, so it's turned off by default.
  // Here are the problems:

  // (1) It's normally redundant, since the gid list will normally be
  // exactly the gids of the global groups that the global name is a
  // member of.  There could be one additional gid if the user did a
  // newgrp to a group he was not a member of, but that is a rarely
  // used feature, and the user doesn't get access to that group if he
  // goes through SRPC and presents a GlobalIdentity, so making it
  // work through UnixIdentity has little value; it might even be more
  // confusing than useful.  The gid list could also be different if
  // group memberships have changed since the repository was last
  // restarted, but we don't handle the general case of passwd/group
  // changes after restart anyway, just simple cases of additions.

  // (2) Even if a gid has multiple names, only its first name is
  // returned.  This is basically a bug, but it's a pain to fix since
  // yielding multiple groups names per gid doesn't fit well into the
  // random-access group(i) style of iterating through the groups.

  // (3) Including these additional groups can make for slightly more
  // work during access checks, slowing down operations.

  if(globalUserToGroupsTable->Get(user, seq))
    {
      groups_cache = full_CharSeq_copy(*seq,
				       (honor_client_gids
					? aup_->aup_len+1
					: 0));
    }
  else
    {
      // Note: if honor_client_gids is false, this user has *no* group
      // memberships
      groups_cache = NEW_CONSTR(CharsSeq, (honor_client_gids
					    ? aup_->aup_len+1
					    : 1));
    }

  if(honor_client_gids)
    {
      groups_cache->addlo(strdup_with_new(unixToGlobalGroupNL(aup_->aup_gid)));
      RWLOCK_LOCKED_REASON(&userGroup_lock, "UnixIdentityRep::fill_caches (post-group)");
      for(unsigned int i = 0; i < aup_->aup_len; i++)
	{
	  groups_cache->addhi(strdup_with_new(unixToGlobalGroupNL(aup_->aup_gids[i])));
	  RWLOCK_LOCKED_REASON(&userGroup_lock, "UnixIdentityRep::fill_caches (post-group)");
	}
    }

  userGroup_lock.releaseRead();
}

void AccessControl::UnixIdentityRep::validate() throw()
{
  if(users_cache == 0) fill_caches();
}

uid_t 
AccessControl::globalToUnixUser(const char* user) throw ()
{
  unsigned int ret = vforeignUser;
  if(user)
    {
      unsigned int v;
      userGroup_lock.acquireRead();
      RWLOCK_LOCKED_REASON(&userGroup_lock, "globalToUnixUser");
      if (globalToUnixUserTable->Get(user, v)) {
	ret = v;
      } else {
	char buf[MAX_MACHINE_NAME + 32];
	if (sscanf(user, "%u@%s", &v, buf) == 2 &&
	    strcasecmp(buf, realm) == 0) {
	  // Interpret 123@foo as the user with uid 123.  This is
	  // an escape to handle users not in the local passwd file.
	  ret = v;
	}
      }
      userGroup_lock.releaseRead();
    }
  return ret;
}

gid_t 
AccessControl::globalToUnixGroup(const char* group) throw ()
{
  unsigned int ret = vforeignGroup;
  if(group)
    {
      unsigned int v;
      userGroup_lock.acquireRead();
      RWLOCK_LOCKED_REASON(&userGroup_lock, "globalToUnixGroup");
      if (globalToUnixGroupTable->Get(group, v)) {
	ret = v;
      } else {
	char buf[MAX_MACHINE_NAME + 32];
	if (sscanf(group, "^%u@%s", &v, buf) == 2 &&
	    strcasecmp(buf, realm) == 0) {
	  // Interpret ^123@foo as the group with gid 123.  This is
	  // an escape to handle groups not in the local group file.
	  ret = v;
	}
      }
      userGroup_lock.releaseRead();
    }
  return ret;
}

static const char*
unixToGlobalUserNL(uid_t uid) throw ()
{
  const char* v;
  if (unixToGlobalUserTable->Get(uid, v)) {
    return v;
  }

  // Uncommon case (uid was not in passwd file when table was built).
  // Try re-loading the access control tables (which we may not do if
  // it's been done recently and can fail if there are parsing errors
  // in any of the access control files).

  // If we managed to complete the reload and there is now an entry
  // for this uid, use that.
  if(maybeNewTables() && unixToGlobalUserTable->Get(uid, v))
    {
      return v;
    }

  // That failed, now we're going to need to modify the tables.
  userGroup_lock.releaseRead();

  // uid was not in passwd file when table was built, and either we
  // couldn't reload the tables or this uid doesn't exist in the new
  // table.  Try to get the user from the system user table.
  
  OS::Passwd pw_ent;
  if(OS::getPwUid(uid, pw_ent)) {
    // add user to globalToUnixUserTable and unixToGlobalUserTable
    char* name = NEW_PTRFREE_ARRAY(char, (pw_ent.name.Length() +
					  AccessControl::realmlen + 2));
    strcpy(name, pw_ent.name.cchars());
    strcat(name, "@");
    strcat(name, AccessControl::realm);
    // add user and his primary group to globalUserToGroupsTable
    CharsSeq* seq;
    if(unixToGlobalGroupsTable->Get(pw_ent.gid, seq)) {
      seq = full_CharSeq_copy(*seq);
    } 
    else {
      // user's primary gid is not in group file
      // !!should we enter it as a numeric group name?
      seq = NEW_CONSTR(CharsSeq, (0));
    }

    // Modify the tables after preparing all new entries.
    userGroup_lock.acquireWrite();
    
    RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalUserNL: adding known");

    // Final check needed, since we released our lock another thread
    // may have already filled this in.
    if(unixToGlobalUserTable->Get(uid, v)) {
      delete name;
      delete seq; // @@@ Need to free sequence entries
      userGroup_lock.releaseWrite();
      userGroup_lock.acquireRead();
      return v;
    }
    else {
      globalToUnixUserTable->Put(name, pw_ent.uid);
      unixToGlobalUserTable->Put(UIntKey(pw_ent.uid),
				 strdup_with_new(name));
      globalUserToGroupsTable->Put(strdup_with_new(name), seq);
    }
    userGroup_lock.releaseWrite();
    
    // Reacquire the lock on the access tables before returning.
    userGroup_lock.acquireRead();
    return name;
  }
  
  // Very uncommon case; uid is not in passwd file at all. Leaks
  // memory.  Could add to table to avoid the leak, but name might
  // be added to passwd file later, so maybe it's better to keep
  // looking there each time.
  char *buf = NEW_PTRFREE_ARRAY(char, (16 + strlen(AccessControl::realm)));
  sprintf(buf, "%u@%s", uid, AccessControl::realm);

  // Modify the tables
  userGroup_lock.acquireWrite();
  RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalUserNL: adding unknown");
  globalToUnixUserTable->Put(buf, uid);
  unixToGlobalUserTable->Put(UIntKey(uid), strdup_with_new(buf));
  userGroup_lock.releaseWrite();

  Repos::dprintf(DBG_ALWAYS,
		 "WARNING: request for uid %d which has no local mapping\n",
		 uid);

  // Reacquire the lock on the access tables before returning.
  userGroup_lock.acquireRead();
  return buf;
}

const char*
AccessControl::unixToGlobalUser(uid_t uid) throw ()
{
  userGroup_lock.acquireRead();
  RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalUser (pre)");
  const char* ret = unixToGlobalUserNL(uid);
  // We add a second locked reason here, because unixToGlobalUserNL
  // may release and re-acquire the lock if this user ID isn't already
  // known
  RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalUser (post)");
  userGroup_lock.releaseRead();
  return ret;
}

static const char*
unixToGlobalGroupNL(gid_t gid) throw ()
{
  CharsSeq* seq;
  if (unixToGlobalGroupsTable->Get(gid, seq)) {
    return seq->get(0);
  }

  // Uncommon case (gid was not in group file when table was built).
  // First, try re-loading the access control tables (which we may not
  // do if it's been done recently and can fail if there are parsing
  // errors in any of the access control files).

  // If we managed to complete the reload and there is now an entry
  // for this gid, use that.
  if(maybeNewTables() && unixToGlobalGroupsTable->Get(gid, seq))
    {
      return seq->get(0);
    }

  // That failed, now we're going to need to modify the tables.
  userGroup_lock.releaseRead();

  // gid was not in group file when table was built, and either we
  // couldn't reload or this gid doesn't exist in the new tables.  Try
  // to get the group from the system group table.
  OS::Group gr_ent;
  if(OS::getGrGid(gid, gr_ent)) {
    // add group to globalToUnixGroupTable and unixToGlobalGroupsTable
    char* gname = NEW_PTRFREE_ARRAY(char, (gr_ent.name.Length() +
					   AccessControl::realmlen + 3));
    strcpy(gname, "^");
    strcat(gname, gr_ent.name.cchars());
    strcat(gname, "@");
    strcat(gname, AccessControl::realm);
    seq = NEW_CONSTR(CharsSeq, (1));
    seq->addhi(strdup_with_new(gname));

    // Modify the tables
    userGroup_lock.acquireWrite();
    RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalGroupNL: adding known");
    globalToUnixGroupTable->Put(gname, gr_ent.gid);
    unixToGlobalGroupsTable->Put(UIntKey(gr_ent.gid), seq);

    // add group to globalUserToGroupsTable entry of each member
    for(int i = 0; i < gr_ent.members.size(); i++) {
      Text grmem = gr_ent.members.get(i);
      char* name = NEW_PTRFREE_ARRAY(char, (grmem.Length() +
					    AccessControl::realmlen + 2));
      strcpy(name, grmem.cchars());
      strcat(name, "@");
      strcat(name, AccessControl::realm);
      CharsSeq* seq;
      if (globalUserToGroupsTable->Get(name, seq)) {
	delete[] name;
      } 
      else {
	// User in a group but not in passwd file.  Add anyway (?).
	seq = NEW_CONSTR(CharsSeq, (1));
	seq->addhi(strdup_with_new(gname));
	globalUserToGroupsTable->Put(name, seq);
      }
      seq->addhi(strdup_with_new(gname));
    }
    userGroup_lock.releaseWrite();
    
    // Reacquire the lock on the access tables before returning.
    userGroup_lock.acquireRead();
    return gname;
  }

  // Very uncommon case; gid is not in groups file at all. Leaks
  // memory.  Could add to table to avoid the leak, but name might
  // be added to groups file later, so maybe it's better to keep
  // looking there each time.
  char *buf = NEW_PTRFREE_ARRAY(char, (16 + strlen(AccessControl::realm)));
  sprintf(buf, "^%u@%s", gid, AccessControl::realm);

  // Modify the tables
  userGroup_lock.acquireWrite();
  RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalGroupNL: adding unknown");
  globalToUnixGroupTable->Put(buf, gid);
  seq = NEW_CONSTR(CharsSeq, (1));
  seq->addhi(strdup_with_new(buf));
  unixToGlobalGroupsTable->Put(UIntKey(gid), seq);
  userGroup_lock.releaseWrite();

  Repos::dprintf(DBG_ALWAYS,
		 "WARNING: request for gid %d which has no local mapping\n",
		 gid);

  // Reacquire the lock on the access tables before returning.
  userGroup_lock.acquireRead();
  return buf;
}

const char*
AccessControl::unixToGlobalGroup(gid_t gid) throw ()
{
  userGroup_lock.acquireRead();
  RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalGroup (pre)");
  const char* ret = unixToGlobalGroupNL(gid);
  // We add a second locked reason here, because unixToGlobalGroupNL
  // may release and re-acquire the lock if this group ID isn't already
  // known
  RWLOCK_LOCKED_REASON(&userGroup_lock, "unixToGlobalGroup (post)");
  userGroup_lock.releaseRead();
  return ret;
}

bool
toUnixUserCallback(void* closure, const char* value)
{
  const char* at = strchr(value, '@');
  if (at && strcasecmp(at+1, AccessControl::realm) == 0) {
    *(uid_t*) closure = AccessControl::globalToUnixUser(value);
    return false;
  }
  return true;
}

// Find a user in the local realm on the owner ACL and return his
// local numeric uid.  If none is found, return vforeignUser.
uid_t
AccessControl::toUnixUser() throw ()
{
  uid_t ret = vforeignUser;
  owner.getAttrib("#owner", toUnixUserCallback, &ret);
  return ret;
}

bool
toUnixGroupCallback(void* closure, const char* value)
{
  const char* at = strchr(value, '@');
  if (at && strcasecmp(at+1, AccessControl::realm) == 0) {
    *(gid_t*) closure = AccessControl::globalToUnixGroup(value);
    return false;
  }
  return true;
}

// Find a group in the local realm on the group ACL and return its
// local numeric uid.  If none is found, return vforeignGroup.
gid_t
AccessControl::toUnixGroup() throw ()
{
  uid_t ret = vforeignGroup;
  owner.getAttrib("#group", toUnixGroupCallback, &ret);
  return ret;
}

bool
AccessControl::IdentityRep::userMatch(const char* name) throw ()
{
  int i = 0;
  const char* uname;
  while ((uname = user(i++)) != NULL) {
    if (strcasecmp(name, uname) == 0) return true;
  }
  return false;
}

bool
AccessControl::IdentityRep::groupMatch(const char* name) throw ()
{
  int i = 0;
  const char* gname;
  while ((gname = group(i++)) != NULL) {
    if (strcasecmp(name, gname) == 0) return true;
  }
  return false;
}

bool
AccessControl::IdentityRep::userMatch(const char* aname,
				      VestaAttribs attribs) throw ()
{
  int i = 0;
  const char* uname;
  while ((uname = user(i++)) != NULL) {
    if (attribs.inAttribs(aname, uname)) return true;
  }
  return false;
}

bool
AccessControl::IdentityRep::groupMatch(const char* aname,
				       VestaAttribs attribs) throw ()
{
  int i = 0;
  const char* gname;
  while ((gname = group(i++)) != NULL) {
    if (attribs.inAttribs(aname, gname)) return true;
  }
  return false;
}

bool
AccessControl::check(AccessControl::Identity who, AccessControl::Class cls,
		     const char* value)
  throw ()
{
  // Everything is OK for internal repository code
  if (who == NULL) return true;

  ModeBits ckbit;
  switch (cls) {
  case AccessControl::unrestricted:
    // Always OK.
    return true;
  case AccessControl::administrative:
    // OK for privileged users only.
    if (who->readOnly) return false;
    return who->userMatch(rootUser) || who->userMatch(vadminUser) ||
      who->userMatch(vwizardUser);
  case AccessControl::ownership:
    // OK for owner.
    // OK for privileged users.
    if (who->readOnly) return false;
    return who->userMatch("#owner", this->owner) ||
      who->userMatch(rootUser) || who->userMatch(vadminUser) ||
      who->userMatch(vwizardUser);
  case AccessControl::setuid:
    // OK for root.
    // OK for owner if value==NULL or who->userMatch(value)
    if (who->readOnly) return false;
    return ((who->userMatch("#owner", this->owner) &&
	     (value == NULL || who->userMatch(value))) ||
	    who->userMatch(rootUser));
  case AccessControl::setgid:
    // OK for root.
    // OK for owner if value==NULL or who->groupMatch(value)
    if (who->readOnly) return false;
    return ((who->userMatch("#owner", this->owner) &&
	     (value == NULL || who->groupMatch(value))) ||
	    who->userMatch(rootUser));
  case AccessControl::read:
    // Check further below.
    ckbit = 04;
    break;
  case AccessControl::write:
    if (who->readOnly) return false;
    // Check further below.
    ckbit = 02;
    break;
  case AccessControl::search:
    // Check further below.
    ckbit = 01;
    break;
  case AccessControl::del:
    if (who->readOnly) return false;
    // If delete is restricted, only OK for privileged users
    if (restrictDelete)
      return (who->userMatch(rootUser) || who->userMatch(vadminUser) ||
	      who->userMatch(vwizardUser));
    // Check further below.
    ckbit = 02;
    break;
  case AccessControl::agreement:
    // OK only when doing wizardly repairs, when you have special
    // knowledge that they will not break the replica agreement invariant.
    // Normally agreement access is used only by internal repository code.
    return who->userMatch(vwizardUser);
  default:
    // Undefined
    assert(false);
  }
    
  // Note: Because checking for owner access is a bit slow, and group
  // access is even slower, we check world, then owner, then group,
  // and if any check fails, we go on to the next.  This makes us more
  // liberal than traditional Unix, in which -r-xr-xrwx means that the
  // owner and group can't write, but others can -- a fairly useless
  // feature.

  // World access OK?
  if ((ckbit & this->mode) != 0) return true;

  // Owner access OK?
  if (who->userMatch("#owner", this->owner) &&
      (((ckbit << 6) & this->mode) != 0)) return true;

  // Group access OK?
  if (who->groupMatch("#group", this->group) &&
      (((ckbit << 3) & this->mode) != 0)) return true;

  switch (cls) {
  case AccessControl::unrestricted:
  case AccessControl::administrative:
  case AccessControl::ownership:
  case AccessControl::setuid:
  case AccessControl::setgid:
  case AccessControl::agreement:
    // These are all covered above, so we shouln't get here.
    assert(false);
    break;
  case AccessControl::read:
    // OK for privileged users
    if (who->userMatch(rootUser) || who->userMatch(vadminUser) ||
	who->userMatch(vwizardUser)) return true;
    break;
  case AccessControl::write:
    // OK for privileged users
    if (who->userMatch(rootUser) || who->userMatch(vadminUser) ||
	who->userMatch(vwizardUser)) return true;
    break;
  case AccessControl::search:
    // OK for privileged users
    if (who->userMatch(rootUser) || who->userMatch(vadminUser) ||
	who->userMatch(vwizardUser)) return true;
    break;
  case AccessControl::del:
    // We shouldn't get here if delete is restricted to administrators
    assert(!restrictDelete);
    // OK for privileged users
    if (who->userMatch(rootUser) || who->userMatch(vadminUser) ||
	who->userMatch(vwizardUser)) return true;
    break;
  default:
    // Undefined
    assert(false);
  }

  return false;
}

// Try to interpret the pattern in exp as a numeric address or subnet.
// Return -1 if it is not a numeric address or subnet, 0 if it is but
// it doesn't match, 1 if it is and does match.
//!! optimization: parse when pattern read from file, not later
static int
numericMatch(Export* exp, Bit32 src_addr)
{
  Bit32 addr, netmask;
  unsigned int a, b, c, d, e, f, g, h, n;
  n = sscanf(exp->pattern, "%3u.%3u.%3u.%3u/%3u.%3u.%3u.%3u",
	     &a, &b, &c, &d, &e, &f, &g, &h);
  if (n >= 4) {
    if (a > 255 || b > 255 || c > 255 || d > 255) return -1;
    addr = htonl((a<<24) + (b<<16) + (c<<8) + d);
  }
  if (n == 4) {
    // Single IP address
    netmask = htonl(0xffffffff);
  } else if (n == 5) {
    // Subnet with netmask given as count of leftmost set bits
    if (e > 32) return -1;
    netmask = e ? (htonl((0xffffffff) << (32 - e))) : 0;
  } else if (n == 8) {
    // Subnet with netmask given as bitmap
    if (e > 255 || f > 255 || g > 255 || h > 255) return -1;
    netmask = htonl((e<<24) + (f<<16) + (g<<8) + h);
  } else {
    return -1;
  }	

  if ((src_addr ^ addr) & netmask) {
    return 0;
  } else {
    return 1;
  }
}

//
// Match a hostname pattern.  
// '*' matches zero or more 
// '?' matches one non-'.' character.
// Other characters must match literally, except that
// case is ignored.
//
static bool
hostnameMatch(const char* pat, const char* name)
{
  for (;;) {
    if (*pat == '\0') {
      return (*name == '\0');
    } else if (*pat == '?') {
      if (*name == '\0' || *name == '.') return false;
      pat++;
      name++;
    } else if (*pat == '*') {
      if (*name == '\0' || *name == '.') {
	pat++;
      } else {
	name++;
      }
    } else {
      if (tolower(*pat) != tolower(*name)) return false;
      pat++;
      name++;
    }
  }
}

static bool
alphaMatch(Export* exp, const char *client_hostname)
{
  if ((client_hostname != 0) &&
      hostnameMatch(exp->pattern, client_hostname))
    return true;

  return false;
}

bool Export::match(ExportKey &key)
{
  // Check for flavor match
  if (this->flavor != AccessControl::IdentityRep::unspecified &&
      this->flavor != key.flavor)
    return false;

  // Check for pattern match
  int nm = numericMatch(this, key.addr);
  if (nm == 0 || (nm == -1) && !alphaMatch(this, key.hostname()))
    return false;

  // Check for realm match
  if (this->realm) {
    if (key.arg == NULL || !hostnameMatch(this->realm, key.arg))
    return false;
  }

  // All succeeded
  return true;
}

bool 
AccessControl::admit(Identity who) throw ()
{
  const char* realm = NULL;
  if (who->flavor == AccessControl::IdentityRep::global ||
      who->flavor == AccessControl::IdentityRep::gssapi) {
    realm = strrchr(who->user(0), '@');
    if (realm) realm++;
  }
  ExportKey k((unsigned int)who->origin.sin_addr.s_addr, who->flavor, realm);
  Export* exp;

  //cerr << "key " << hex << k.addr << " " << dec << k.flavor
  //     << " " << k.arg << endl;

  export_lock.acquireRead();
  if (!exportCache->Get(k, exp)) {
    //cerr << "miss ";

    // Save the current export epoch
    unsigned int curEpoch = exportEpoch;

    // Search for a match in the export table
    exp = Export::first;
    RECORD_TIME_POINT;
    while((exp != NULL) && !exp->match(k))
      exp = exp->next;
    RECORD_TIME_POINT;

    // Release our read lock and acquire a write lock to update
    // exportCache.
    export_lock.releaseRead();
    export_lock.acquireWrite();

    if(curEpoch != exportEpoch)
      {
	// Ooops, the export table changed between when we released
	// the read lock and acquired the write lock.  Search again
	// for a match before updating exportCache.
	exp = Export::first;
	RECORD_TIME_POINT;
	while((exp != NULL) && !exp->match(k))
	  exp = exp->next;
	RECORD_TIME_POINT;
      }

    if (realm) k.arg = strdup_with_new(realm);

    exportCache->Put(k, exp);
    export_lock.releaseWrite();
  } else {
    //cerr << "hit ";
    export_lock.releaseRead();
  }

  //if (exp == NULL) {
  //  cerr << "(no entry)" << endl;
  //} else {
  //  cerr << exp->pattern << " " << exp->level << " " << exp->flavor
  //       << " " << exp->realm << endl;
  //}


  if (exp == NULL) return false;

  switch (exp->level) {
  case Export::deny:
    return false;
  case Export::readOnly:
    who->readOnly = true;
    break;
  case Export::allow:
    break;
  }
  return true;
}

AccessControl::ModeBits
AccessControl::parseModeBits(const char* char_mode) throw ()
{
  int mb;
  sscanf(char_mode, "%o", &mb);
  return (AccessControl::ModeBits) mb & 0777;
}

const char*
AccessControl::formatModeBits(AccessControl::ModeBits mode) throw ()
{
  char* ret = NEW_PTRFREE_ARRAY(char, 4);
  sprintf(ret, "%03o", mode & 0777);
  return ret;
}

// Disused recovery callback, needed so that old logs can be read
static void
AumapCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    char* name;
    long lvalue;
    rr->getNewQuotedString(c, name);
    rr->getLong(c, lvalue);
    delete[] name;
}

static void
RumapCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    char* name;
    rr->getNewQuotedString(c, name);
    delete[] name;
}

static void
AgmapCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    char* name;
    long lvalue;
    rr->getNewQuotedString(c, name);
    rr->getLong(c, lvalue);
    delete[] name;
}

static void
RgmapCallback(RecoveryReader* rr, char& c)
     throw(VestaLog::Error, VestaLog::Eof)
{
    char* name;
    rr->getNewQuotedString(c, name);
    delete[] name;
}

// Module initialization
void
AccessControl::serverInit()
     throw(VestaConfig::failure /*, gssapi::failure*/)
{
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  // If realm is unitialized, initialize it.
  if(realm == 0)
    {
      realm =
	strdup(VestaConfig::get_Text("Repository", "realm").cchars());
      realmlen = strlen(realm);
    }

  // If defaultFlavor is unitialized, initialize it.
  if(defaultFlavor == AccessControl::IdentityRep::unspecified)
    {
      Text t = VestaConfig::get_Text("Repository", "default_flavor");
      if (strcasecmp(t.cchars(), "unix") == 0) {
	defaultFlavor = IdentityRep::unix_flavor;
      } else if (strcasecmp(t.cchars(), "global") == 0) {
	defaultFlavor = IdentityRep::global;
      } else if (strcasecmp(t.cchars(), "gssapi") == 0) {
	defaultFlavor = IdentityRep::gssapi;
      } else {
	throw(VestaConfig::failure("bad value for "
				   "[Repository]default_flavor"));
      }
    }

  AccessControl::commonInit();

  vestaGroupFile = strdup(VestaConfig::get_Text("Repository", "group_file")
			  .cchars());
  vestaAliasFile = strdup(VestaConfig::get_Text("Repository", "alias_file")
			  .cchars());
  vestaExportFile = strdup(VestaConfig::get_Text("Repository", "export_file")
			   .cchars());
  if(VestaConfig::is_set("Repository", "min_auto_access_refresh_delay"))
    {
      min_auto_refresh_delay = VestaConfig::get_int("Repository",
						    "min_auto_access_refresh_delay");
    }
  if(VestaConfig::is_set("Repository", "honor_client_gids"))
    {
      honor_client_gids = VestaConfig::get_bool("Repository",
						"honor_client_gids");
    }
  newUserGroupTables();
  newExportTables();
  RegisterRecoveryCallback("aumap", AumapCallback);
  RegisterRecoveryCallback("rumap", RumapCallback);
  RegisterRecoveryCallback("agmap", AgmapCallback);
  RegisterRecoveryCallback("rgmap", RgmapCallback);
}    

void AccessControl::refreshAccessTables() throw(AccessControl::ParseError)
{
  // Try to load new access control tables, but don't replace the
  // existing ones if there are parsing errors.
  newUserGroupTables(false);
  newExportTables(false);
}
