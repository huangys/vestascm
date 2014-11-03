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

// Created on Mon Jul 14 10:52:08 PDT 1997 by heydon

#include <sys/types.h> // for stat(2)
#include <sys/stat.h>  // for stat(2)
#if defined(__digital__)
#include <sys/mode.h>  // for testing if a path is a directory
#endif
#include <strings.h>   // for index(3)
#include <errno.h>

#include <Basics.H>
#include <Sequence.H>
#include <FS.H>
#include "ParseImports.H"

using std::ostream;

// Char_Buff

// An efficient character buffer.  Attempts to strike a balance
// between the use of a fixed array for efficiency during lexical
// analysis and allowing arbitrarily long strings to be collected.
// (Implemented in order to alleviate a buffer overrun with a fixed
// length buffer with no mechanism for overflow.)

struct Char_Buff
{
  // A fixed-length array which allows fast appending of individual
  // characaters.
  static const unsigned int len = 100;
  char chars[len+1];

  // The current position in appending to the array.
  unsigned int pos;

  // Text previously flushed from the fixed-length buffer (due to
  // overflows, etc.)
  Text content;

  inline Char_Buff()
    : pos(0)
  {
  }

  // Flush characters written to the fixed-length array to variable
  // length storage (possibly due to an overflow).
  inline void flush()
  {
    if(pos > 0)
      {
	chars[pos] = 0;
	content += chars;
	pos = 0;
      }
  }

  // Discard whetever characters have been accumulated and start over.
  inline void reset()
  {
    content = "";
    pos = 0;
  }

  // Make sure that we can append n characters to the fixed-length
  // array.
  inline void expect(unsigned int n)
  {
    // If this would push us past the limit of the fixed-length
    // array...
    if((pos + n) >= len)
      {
	// Flush to variable size storage.
	flush();
      }
  }

  // Return the total number of characters buffered, avoiding a call
  // to strlen when possible.
  inline unsigned int length()
  {
    return pos + (content.Empty() ? 0 : content.Length());
  }

  // Append a character to the buffer.
  inline Char_Buff &operator +=(char c)
  {
    expect(1);
    chars[pos++] = c;
    return *this;
  }

  // Remove and return the last character appended (think "ungetc").
  inline char pop()
  {
    // If there are characters in the fixed-length array, take the
    // last of those.
    if(pos > 0)
      {
	return chars[--pos];
      }
    // If we have no characters at all, return 0.
    else if(content.Empty())
      {
	// We should really throw an exception here.
	return 0;
      }
    // Extract and remove the last character from the variable size
    // storage.
    else
      {
	unsigned int clen = content.Length();
	char c = content[clen-1];
	content = content.Sub(0, clen-1);
	return c;
      }
  }

  // Provide equality and inequality operators which can avoid the
  // cost of an allocation when there are a small number of characters
  // in the buffer.
  inline bool operator ==(const char *text)
  {
    if(content.Empty())
      {
	chars[pos] = 0;
	return (strcmp(text, chars) == 0);
      }
    else
      {
	flush();
	return (content == text);
      }
  }
  inline bool operator !=(const char *text)
  {
    if(content.Empty())
      {
	chars[pos] = 0;
	return (strcmp(text, chars) != 0);
      }
    else
      {
	flush();
	return (content != text);
      }
  }

  // Return the first character in the buffer.  Used to test for
  // one-chacater tokens.
  inline char first() const throw()
  {
    return (content.Empty() ? chars[0] : content[0]);
  }
};

ostream&
operator<<(ostream &os, const ParseImports::Error &err) throw ()
{
    return (os << err.msg);
}

// Implementation of ParseImports::LocalModelSpace

char
ParseImports::LocalModelSpace::getC()
  throw (ParseImports::Error, FS::EndOfFile)
/* Read and return the next character. Throws "Error" in the event
   of a read error, and "FS::EndOfFile" if EOF is encountered. */
{
    int c = ifs.get();
    if (c == EOF || ifs.eof()) throw FS::EndOfFile();
    if (ifs.fail()) throw ParseImports::Error("failure reading model");
    return (char)c;
}

ParseImports::ModelSpace*
ParseImports::LocalModelSpace::open(const Text &modelname) const
  throw(ParseImports::Error, FS::Failure, FS::DoesNotExist)
{
  return NEW_CONSTR(LocalModelSpace, (modelname));
}

ParseImports::LocalModelSpace::LocalModelSpace(const Text &modelname)
  throw(FS::Failure, FS::DoesNotExist)
{
  try
    {
      FS::OpenReadOnly(modelname, /*OUT*/ this->ifs);
    }
  catch (FS::DoesNotExist)
    {
      errno = ENOENT; // set errno to indicate non-existent file
      throw FS::DoesNotExist();
    }
}

ParseImports::ModelSpace::type
ParseImports::LocalModelSpace::getType(const Text &name) const throw()
{
    struct stat stat_buff;
    int stat_res = stat(name.cchars(), &stat_buff);
    if (stat_res == 0) {
      if (S_ISDIR(stat_buff.st_mode)) {
	return ParseImports::ModelSpace::directory;
      }
      if (S_ISREG(stat_buff.st_mode)) {
	return ParseImports::ModelSpace::file;
      }
    }
    return ParseImports::ModelSpace::none;
}

// end of LocalModelSpace implementation

static void
SkipOneLineComment(ParseImports::ModelSpace &model) throw (ParseImports::Error)
/* Skip characters in model up to and including the next newline. */
{
    try {
	while (model.getC() != '\n') /* SKIP */;
    } catch (FS::EndOfFile) {
        throw ParseImports::Error("premature EOF in // comment");
    }
}

static Text
SkipBracketedComment(ParseImports::ModelSpace &model)
     throw (ParseImports::Error)
/* Skip characters in model until end-of-comment bracket characters
   are read. If the first character of the comment is '*', then the
   comment is assumed to be a pragma, and the contents of the pragma
   are returned; otherwise, returns the empty string. */
{
    try {
	bool inPragma = false;
	Char_Buff pragma;
	char last = model.getC(), c;
	if (last == '*') inPragma = true;
	while ((c = model.getC()) != '/' || last != '*') {
	    if (inPragma) {
		pragma += c;
	    }
	    last = c;
	}
	if (inPragma) {
	    if (pragma.length() < 1) {
		// comment of the form "/**/"; not a pragma
		inPragma = false;
	    } else {
	        pragma.flush();
		int len = pragma.content.Length();
		int i = len;
		while (i > 0 && pragma.content[i-1] == '*') i--;
		assert(i < len);
		pragma.content = pragma.content.Sub(0, i);
	    }
	}
	// NOTE: The following can produce a dangling pointer if this
        // program is not linked with the garbage collector!
	return (inPragma ? pragma.content : Text(""));
    }
    catch (FS::EndOfFile) {
        throw ParseImports::Error("premature EOF in /*...*/ comment");
    }
}

static Text
SkipWhite(ParseImports::ModelSpace &model, Char_Buff &buff)
  throw (ParseImports::Error, FS::EndOfFile)
/* Skip whitespace and comments in model, storing the first meaningful
   character read into "buff" and setting "pos" to 1. The the white space
   ends in a pragma, the contents of the pragma are returned; otherwise,
   returns NULL. */
{
    Text res("");
    char c;
    while (true) {
	while ((c = model.getC()) == ' ' || c == '\n' || c == '\t') /*SKIP*/;
	buff.reset();
	buff += c;
	if (c != '/') break;
	c = model.getC();
	buff += c;
	switch (c) {
	  case '/':
	    SkipOneLineComment(model);
	    res = "";
	    break;
	  case '*':
	    res = SkipBracketedComment(model);
	    break;
	  default:
	    model.ungetC(buff.pop());
	    return res;
	}
    }
    return res;
}

static void
Scan(ParseImports::ModelSpace &model, Char_Buff &buff,
     const char *stopChars) throw (ParseImports::Error)
/* Read characters from model into "buff" starting at position "pos" until one
   of the characters in "stopChars" is read or EOF is encountered. If one of
   the characters in "stopChars" is read, it is returned to the stream. It is
   a checked run-time error to try to put more than "buffLen" characters into
   "buff". Set "pos" on exit to the total number of characters in "buff". */
{
    char c;
    try {
	while (true) {
	    c = model.getC();
	    if (index(stopChars, c) != (char *)NULL) {
		model.ungetC(c);
		break;
	    }
	    buff += c;
	}
    }
    catch (FS::EndOfFile) {
	/* SKIP */
    }
}

// an interval
class Intv {
public:
  int start, end;
  Text pragma;
};

static Intv
Lex(ParseImports::ModelSpace &model, Char_Buff &buff)
  throw (ParseImports::Error, FS::EndOfFile)
/* Read the next lexeme from model, and return it in "buff" as a
   null-terminated string. Any initial whitespace and comments are skipped.
   Returns the position in model of the first non-whitespace character read
   into "buff". "buffLen" specifies the length of the buffer "buff"; it a
   checked run-time error to read a lexeme that is too long to fit in the
   buffer. */
{
    Intv res;
    res.pragma = SkipWhite(model, buff);
    assert(buff.length() == 1);
    try {
	res.start = model.tell() - 1;
	if (index("=;,{}[]", buff.first()) == (char *)NULL) {
	    // not a one-character lexeme; scan rest of lexeme
	    Scan(model, buff, "=;,[]{} \t\n");
	}
	buff.flush();
	res.end = model.tell();
    }
    catch (FS::Failure) {
	throw ParseImports::Error("failure reading model");
    }
    return res;
}

// remove double-quotes from a string.  (Note that this doesn't check
// for balanced quotes or any sort of escaping, it simply removes all
// double-quote characters from the string.)
static Text StripQuotes(const Text &in, int *origOffset = 0)
{
  Text result = in;

  int start = 0, match;
  while ((match = result.FindChar('"', start)) >= 0) {
    if((origOffset != 0) && (match < *origOffset))
      (*origOffset)--;
    result = result.Sub(0, match) + result.Sub(match+1);
    start = match;
  }

  return result;
}

Text
ParseImports::ResolvePath(const Text &path, const Text &wd,
			  const ParseImports::ModelSpace *ms, int *origOffset)
  throw (ParseImports::Error, FS::Failure, SRPC::failure)
{
    bool free_ms = false;
    if (ms == NULL) {
      free_ms = true;
      ms = NEW(LocalModelSpace);
    }
    Text fullPath(path);

    // remove double-quotes from "fullPath"
    fullPath = StripQuotes(fullPath, origOffset);

    // make "fullPath" absolute
    if (fullPath[0] != '/') {
        if(origOffset != 0) *origOffset += wd.Length();
	fullPath = wd + fullPath;
    }

    switch (ms->getType(fullPath)) {
    case ParseImports::ModelSpace::directory:
      // "fullPath" is a directory; append "/build.ves"
      fullPath += (fullPath[fullPath.Length()-1] == '/')
	? "build.ves" : "/build.ves";
      break;

    case ParseImports::ModelSpace::file:
    default:
      break;

    case ParseImports::ModelSpace::none:
      // Try appending .ves if not already there
      if (fullPath.Sub(fullPath.Length()-4) != Text(".ves")) {
	Text fullPath2(fullPath); fullPath2 += ".ves";	
	if (ms->getType(fullPath2) == ParseImports::ModelSpace::file) {
	  fullPath = fullPath2;
	}
      }
      break;
    }

    if(free_ms)
      // We allocated this, so free it
      delete ms;
    return fullPath;
}

static void
AddPath(/*INOUT*/ ImportSeq &seq, const Text &orig,
	const Text &path, int origOffset, const ImportID &id,
	const Text &wd, const Intv &bounds, 
	const ParseImports::ModelSpace &ms, bool fromForm=false)
  throw (ParseImports::Error, FS::Failure, SRPC::failure)
{
    // form full pathname
    // Also, allow pathname to be entirely in quotes, 
    // as in from "/vesta/foo.bar.net/project/" import ...
    bool local = !((path[0] == '"' && path[1] == '/') || (path[0] == '/'));
    Text fullPath(ParseImports::ResolvePath(path, wd, &ms, &origOffset));

    // allocate new "Import" object with proper parameters
    bool noUpdate = ((bounds.pragma == "NOUPDATE") ||
		     (bounds.pragma == "noupdate"));
    Import *imp = NEW_CONSTR(Import, (fullPath, orig, origOffset, id,
				      bounds.start, bounds.end,
				      local, fromForm, noUpdate));

    // add "fullPath" to "seq"
    seq.addhi(imp);
}

static void
AddFromPath(/*INOUT*/ ImportSeq &seq, const Text &orig, const Text &path,
	    int origOffset,
	    const ImportID &id, const Text &wd, const Intv &bounds,
	    const ParseImports::ModelSpace &ms)
  throw (ParseImports::Error, FS::Failure, SRPC::failure)
{
  AddPath(/*INOUT*/ seq, orig, path, origOffset, id, wd, bounds, ms, /*fromForm=*/ true);
}

static void
ExpectToken(const char *expected, Char_Buff &found)
  throw (ParseImports::Error)
{
  if (found != expected)
    {
      Text msg("expecting '");
      msg += expected;
      msg += "'; found '";
      // Note that found is non-const as we need to flush it.
      found.flush();
      msg += found.content;
      msg += "'";
      throw ParseImports::Error(msg);
    }
}

static void
ReadIncIdReq(ParseImports::ModelSpace &model, Char_Buff &buff,
	     const Text &wd, /*INOUT*/ ImportSeq &seq)
     throw (ParseImports::Error, FS::EndOfFile, FS::Failure, SRPC::failure)
/* Grammar to process:
|    IncIdReq  ::= import IncItemR*;
|    IncItemR  ::= IncSpecR | IncListR
|    IncSpecR  ::= Id = DelimPath
|    IncListR  ::= Id = `[' IncSpecR*, `]'
*/
{
    // skip "import"
    ExpectToken("import", buff);
    (void)Lex(model, buff);
    buff.flush();
    Text id(StripQuotes(buff.content));
    ImportID import_id;
    import_id.addhi(id);

    // read IncItemR's
    while ((buff != "import") && (buff != "from") &&
           (buff != "{")) {
	// skip initial Id
	(void)Lex(model, buff);

	// skip "="
	ExpectToken("=", buff);
	Intv bounds = Lex(model, buff);

	// determine if this is a IncSpecR or an IncListR
	if (buff != "[") {
	  // IncSpecR
	  buff.flush();
	  Text orig(buff.content);
	  AddPath(/*INOUT*/ seq, orig, orig, 0, import_id, wd, bounds, model);
	} else {
	    // IncListR
	    (void)Lex(model, buff);
            buff.flush();
            id = StripQuotes(buff.content);
	    import_id.addhi(id);
	    while (buff != "]") {
		// skip Id
		(void)Lex(model, buff);

		// skip "="
		ExpectToken("=", buff);
		Intv bounds = Lex(model, buff);

		// process path
		buff.flush();
		Text orig(buff.content);
		AddPath(/*INOUT*/ seq, orig, orig, 0, import_id, wd, bounds, model);
		(void)Lex(model, buff);

		// skip trailing "," if present
		if (buff == ",") {
		    (void)Lex(model, buff);
                    buff.flush();
		    import_id.remhi();
		    id = StripQuotes(buff.content);
		    import_id.addhi(id);
		} else {
		    ExpectToken("]", buff);
		}
	    }
	    import_id.remhi();
	}
	(void)Lex(model, buff);

	// skip trailing ";" if present
	if (buff == ";") {
	    (void)Lex(model, buff);
            buff.flush();
	    import_id.remhi();
            id = StripQuotes(buff.content);
	    import_id.addhi(id);
	} else {
	    // last element -- exit loop
	    break;
	}
    }
    import_id.remhi();
}

static void
ReadIncIdOpt(ParseImports::ModelSpace &model, Char_Buff &buff,
  const Text &wd, /*INOUT*/ ImportSeq &seq)
  throw (ParseImports::Error, FS::EndOfFile)
/* Grammar to process:
|    IncIdOpt ::= from DelimPath import IncItemO*;
|    IncItemO ::= IncSpecO | IncListO
|    IncSpecO ::= [ Id = ] Path [ Delim ]
|    IncListO ::= Id = `[' IncSpecO*, `]'
*/
{
    // skip "from"
    ExpectToken("from", buff);
    (void)Lex(model, buff);

    // remember DelimPath
    buff.flush();
    Text rootPath(buff.content);
    if (rootPath[rootPath.Length()-1] != '/') {
	rootPath += '/';
    }
    (void)Lex(model, buff);

    // skip "import"
    ExpectToken("import", buff);
    Intv bounds = Lex(model, buff);

    ImportID import_id;
    
    while ((buff != "import") && (buff != "from") &&
           (buff != "{")) {

        // save current lexeme in case we are in IncSpecO case w/ no "Id ="
        buff.flush();
	Text path(buff.content);
	(void)Lex(model, buff);

	// test for "="
	if (buff != "=") {
	  // we were in the IncSpecO case with no leading "Id =". The
	  // id is the the first segment of path.
	  int first_slash = path.FindChar('/');
	  Text id = StripQuotes((first_slash>0)
				?path.Sub(0, first_slash)
				:path);
	  import_id.addhi(id);

	  AddFromPath(/*INOUT*/ seq, path, rootPath+path,
		      rootPath.Length(), import_id,
		      wd, bounds, model);
	} else {
	    // we could now be in either the IncSpecO or IncListO case
	    bounds = Lex(model, buff); // read token after "="
	    if (buff != "[") {
	        Text id(StripQuotes(path));
		import_id.addhi(id);
		// IncSpecO case with leading "Id ="
	        buff.flush();
		Text path(buff.content);
		AddFromPath(/*INOUT*/ seq, path, rootPath+path,
			    rootPath.Length(), import_id, 
			    wd, bounds, model);
	    } else {
		// IncListO case
	      Text id(StripQuotes(path));
	      import_id.addhi(id);
	        bounds = Lex(model, buff); // read token after "["
		while (buff != "]") {
		    // remember lexeme
		    buff.flush();
		    Text path(buff.content);
		    (void)Lex(model, buff); // read "=", ",", or "]"

		    // determine if we are in "Id =" case or not
		    if (buff != "=") {
		      int first_slash = path.FindChar('/');
		      Text id = StripQuotes((first_slash>0)
					    ?path.Sub(0, first_slash)
					    :path);
		      import_id.addhi(id);
		      AddFromPath(/*INOUT*/ seq, path, rootPath+path,
				  rootPath.Length(), import_id,
				  wd, bounds, model);
		    } else {
		        Text id(StripQuotes(path));
			import_id.addhi(id);
			// read path
			bounds = Lex(model, buff);
			buff.flush();
			Text path(buff.content);
			AddFromPath(/*INOUT*/ seq,  path, rootPath+path,
				    rootPath.Length(), import_id, 
				    wd, bounds, model);
			(void)Lex(model, buff); // read "," or "]"
		    }

		    import_id.remhi();
		    // skip trailing "," if present
		    if (buff == ",") {
			bounds = Lex(model, buff);
		    } else {
			ExpectToken("]", buff);
		    }
		}
		import_id.remhi();
	    }
	    (void)Lex(model, buff);
	}

	// skip trailing ";" if present
	if (buff == ";") {
	    bounds = Lex(model, buff);
	} else {
	    // last element -- exit loop
	    break;
	}
    }
}

static void
ScanImports(ParseImports::ModelSpace &model, const Text &wd,
  /*INOUT*/ ImportSeq &seq) throw (ParseImports::Error, FS::Failure, SRPC::failure)
/* Scan the model "model" for imports, and add the names of any models
   it imports to "seq". Relative model names are resolved relative
   to the directory named "wd", which ends in a "/". */
{
  Char_Buff buff;
    try {
	Lex(model, buff);
	while (true) {
	    if (buff == "{") {
		// start of Block clause
		break;
	    } else if (buff == "import") {
		// IncIdReq clause
		ReadIncIdReq(model, buff, wd, /*INOUT*/ seq);
	    } else if (buff == "from") {
		// IncIdOpt clause
		ReadIncIdOpt(model, buff, wd, /*INOUT*/ seq);
	    } else {
		// skip this lexeme and read the next one
		Lex(model, buff);
	    }
	}
    }
    catch (FS::EndOfFile) {
	throw ParseImports::Error("premature end-of-file");
    }
}

void
ParseImports::P(const Text &modelname, /*INOUT*/ ImportSeq &seq,
		const ParseImports::ModelSpace* ms)
  throw(FS::DoesNotExist, FS::Failure, ParseImports::Error, SRPC::failure)
/* Read the imports in the model named by the absolute pathname "model", and
   append the names of any such models to "seq". */
{
    // open the file for reading
    ParseImports::ModelSpace *model;
    if (ms == NULL) {
      model = NEW_CONSTR(LocalModelSpace, (modelname));
    } else {
        model = ms->open(modelname);
    }

    // form name of directory in which "model" resides
    int last_slash = modelname.FindCharR('/'); assert(last_slash >= 0);
    Text wd = modelname.Sub(0, last_slash+1);

    // parse the file
    ScanImports(*model, wd, /*INOUT*/ seq);

    // close the file
    delete(model);
}
