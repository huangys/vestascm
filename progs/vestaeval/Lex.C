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

/* File: Lex.C                                                 */

#include "ModelState.H"
#include "Lex.H"
#include "Location.H"
#include "Expr.H"
#include "Val.H"
#include "Err.H"
#include <Table.H>
#include <iostream>

using std::istream;
using OS::cio;

// Extern global variables:
SrcLoc *noLoc = NEW(SrcLoc);

// Lex global variables:
istream *lexIn;
char lookAhead1, lookAhead2;
int lookaheads;
int lineNumber, charNumber;

static TokenClass CharMap[300];
static Table<Text,TokenClass>::Default ReservedWords(64);
static SrcLoc *currentLoc = NEW(SrcLoc);

Token::Token()
: bytesLength(128), length(0), loc(noLoc) {
  bytes = NEW_PTRFREE_ARRAY(char, 128);
  bytes[length] = 0;
}

void Token::AppendChar(char c) {
  bytes[length++] = c;
  if (length == bytesLength) {
    bytesLength = bytesLength * 2;
    char *newBytes = NEW_PTRFREE_ARRAY(char, bytesLength);
    memcpy(newBytes, bytes, length);
    delete[] bytes;
    bytes = newBytes;
  }
}

void Token::TokenAssign(Token& tk) {
  this->tclass = tk.tclass;
  this->expr = tk.expr;
  this->loc = tk.loc;
  char* tempBytes = this->bytes;
  int tempLength = this->length, tempBytesLength = this->bytesLength;
  this->bytes = tk.bytes;
  this->length = tk.length;
  this->bytesLength = tk.bytesLength;
  tk.bytes = tempBytes;
  tk.length = this->length;
  tk.bytesLength = tempBytesLength;
}

char GetChar() {
  char res;
  switch (lookaheads) {
  case 0:
    res = (char)lexIn->get(); 
    if (res == '\n') {
      lineNumber++;
      charNumber = 0;
    }
    else
      charNumber++;
    break;
  case 1:
    res = lookAhead1; 
    lookaheads--;
    break;
  case 2:
    res = lookAhead1; 
    lookAhead1 = lookAhead2; 
    lookaheads--;
    break;
  default:
    InternalError(cio().start_err(), cio().helper(true),
		  "GetChar");
    // Unreached: InternalError never returns
    abort();
  }
  return res;
}
    
inline static void UngetChar(char c) {
  switch (lookaheads) {
  case 0:
    lookAhead1 = c; 
    lookaheads++;
    break;
  case 1:
    lookAhead2 = lookAhead1; 
    lookAhead1 = c; 
    lookaheads++;
    break;
  default:
    InternalError(cio().start_err(), cio().helper(true),
		  "UngetChar");
    // Unreached: InternalError never returns
    abort();
  }
}

static char SkipWhitespace() {
  while (true) {
    char c = GetChar();
    switch (c) {
    case ' ':  case '\t': case '\f': case '\n': case '\r':
      break;
    default:
      return c;
    }
  }
}

// Nested Slash-Star comments are not permitted.
void Token::ScanComment(char c) {
  bool done = false;

  // assert((c == '*') || (c == '/'))
  if (c == '*') {
    while (!done) {
      c = GetChar();
      switch (c) {
	case '/':
	  c = GetChar();
	  if (c == '*') {
	    SrcLoc loc(lineNumber, charNumber, currentLoc->file, currentLoc->shortId);
	    Error(cio().start_err(), "Nested /* comment.\n", &loc);
	    cio().end_err();	    
	    throw "\nParsing terminated.\n";
	  }
	  else
	    UngetChar(c);
	  break;
	case '*':
	  c = GetChar();
	  if (c == '/')
	    done = true;
	  else
	    UngetChar(c);
	  break;
	case ((char) EOF):
	  {
	    SrcLoc loc(lineNumber, charNumber, currentLoc->file, currentLoc->shortId);
	    Error(cio().start_err(), "Unterminated comment. EOF in ScanComment.\n", &loc);
	    cio().end_err();	    
	    throw "\nParsing terminated.\n";
	  }
	default:
	  break;
	}
    }
  }
  else {
    while (c != '\n' && c != ((char) EOF))
      c = GetChar();
  }
}

// Nested pragmas are not permitted.
bool Token::ScanPragma() {
  bool done = false;
  char c = SkipWhitespace();

  while (!done) {
    switch (c) {
    case '*':
      {
	char c1 = GetChar();
	char c2 = GetChar();
	// Actual pragma
	if ((c1 == '*') && (c2 == '/'))
	  done = true;
	// Comment that starts with "/**"
	else if(c1 == '/')
	  {
	    // Discard any colleced bytes
	    StartBytes();
	    // Put the character following the comment back.
	    UngetChar(c2);
	    // Indicate that this wasn't a pragma.
	    return false;
	  }
	else {
	  AppendChar(c);
	  c = c1;
	  UngetChar(c2);
	}
	break;
      }
    case ((char) EOF):
      Error(cio().start_err(), "Unterminated pragma.\n", currentLoc);
      cio().end_err();      
      throw "\nParsing terminated.\n";
    default:
      AppendChar(c);
      c = GetChar();
      break;
    }
  }
  // Remove white space at the tail.
  int index = length - 1;
  while (index >= 0) {
    switch (bytes[index]) {
    case ' ':  case '\t': case '\f': case '\n':
      index--;
      break;
    default:
      length = index + 1;
      index = -1;
      break;
    }
  }
  EndBytes();
  this->tclass = TkPragma;
  this->loc = currentLoc->Copy();

  // Indicate that this was a pragma.
  return true;
}

void Token::ScanIdNumber(char c) {
  AppendChar(c);
  while (true) {
    c = GetChar();
    if ((c >= 'a') && (c <= 'z') ||
	(c >= 'A') && (c <= 'Z') ||
	(c >= '0') && (c <= '9') ||
	(c == '_') ||
	(c == '.'))
      AppendChar(c);
    else {
      UngetChar(c);
      break;
    }
  }
  EndBytes();
  this->loc = currentLoc->Copy();
  Text id(Bytes());
  if (!ReservedWords.Get(id, tclass)) {
    // if it is not reserved, it is either an identifier or a number.
    Basics::int32 n = 0;
    unsigned int idx = 0, base = 10;
    if (id[0] == '0') {
      if (id[1] == 'x' || id[1] == 'X') {
	idx = 2;
	base = 16;
      } else {
	idx = 1;
	base = 8;
      }
    }
    bool isNumber = true;
    for (int i = idx; i < id.Length(); i++) {
      char ch = id[i];
      switch (ch) {
      case '0': case '1': case '2': case '3': case '4': case '5':
      case '6': case '7':
        n = base * n + (ch - '0');
	break;
      case '8': case '9':
	if (base == 8)
	  isNumber = false;
	else
	  n = base * n + (ch - '0');
	break;
      case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	if (base != 16)
	  isNumber = false;
	else 
	  n = base * n + (10 + ch - 'a');
        break;
      case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	if (base != 16)
	  isNumber = false;
	else
	  n = base * n + (10 + ch - 'A');
        break;
      default:
	isNumber = false;
        break;
      }
      if (n < 0) {
	Error(cio().start_err(), "Integer too large.\n", currentLoc);
	cio().end_err();	
	throw "\nParsing terminated.\n";
      }
      if (!isNumber) break;
    }
    // if it is not a number, it is treated as an identifier.
    if (isNumber) {
      tclass = TkNumber;
      expr = NEW_CONSTR(ConstantEC, (NEW_CONSTR(IntegerVC, (n)), this->loc));
    }
    else {
      tclass = TkId;
      expr = NEW_CONSTR(NameEC, (id, this->loc));
    }
  }
}

void Token::ScanText() {
  char c;
  int val, j;
  bool done = false;

  while (!done) {
    c = GetChar();
    switch (c) {
    case '"':
      done = true;
      break;
    case '\\':
      c = GetChar();
      switch (c) {
      case 'n':  AppendChar('\n'); break;
      case 't':  AppendChar('\t'); break;
      case 'v':  AppendChar('\v'); break;
      case 'b':  AppendChar('\b'); break;
      case 'r':  AppendChar('\r'); break;
      case 'f':  AppendChar('\f'); break;
      case 'a':  AppendChar('\a'); break;
      case '\\': AppendChar('\\'); break;
      case '"':  AppendChar('\"'); break;
      case 'x': case 'X':
	val = 0;
	c = GetChar();
	for (j = 0; j < 2; j++) {
	  switch (c) {
	  case '0': case '1': case '2': case '3': case '4': case '5':
	  case '6': case '7': case '8': case '9':
            val = 16 * val + c - '0';                
	    c = GetChar();
	    break;
          case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	    val = 16 * val + 10 + c - 'A';
	    c = GetChar();
	    break;
          case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	    val = 16 * val + 10 + c - 'a';
	    c = GetChar();
	    break;
	  default:
	    if (j == 0) {
	      UngetChar(c);
	      Error(cio().start_err(), "Illegal escape character in text.\n", currentLoc);
	      cio().end_err();	      
	      throw "\nParsing terminated.\n";
	    }
	    goto xend;
	  }
	}
  xend: UngetChar(c);
	AppendChar((unsigned char)val);
	break;
      default:
	val = 0;
	for (j = 0; j < 3; j++) {
	  switch (c) {
	  case '0': case '1': case '2': case '3': case '4': case '5':
	  case '6': case '7':
            val = 8 * val + c - '0';                
	    c = GetChar();
	    break;
	  default:
	    if (j == 0) {
	      UngetChar(c);
	      Error(cio().start_err(), "Illegal escape character in text.\n", currentLoc);
	      cio().end_err();
	      throw "\nParsing terminated.\n";
	    }
	    goto oend;
	  }
	}
  oend: UngetChar(c);
	AppendChar((unsigned char)val);
	break;
      }
      break;
    case ((char) EOF): case '\n':  // error trap
      Error(cio().start_err(), "Text not terminated at end of line or file.\n", 
	    currentLoc);
      cio().end_err();      
      throw("\nParsing terminated.\n");
    default:
      AppendChar(c);
      break;
    }
  }
  EndBytes();
  this->tclass = TkString;
  this->loc = currentLoc->Copy();
  expr = NEW_CONSTR(ConstantEC, 
		    (NEW_CONSTR(TextVC, (Text(Bytes()))), this->loc));
}

void Token::Next() {
  while (true) {
    char c = SkipWhitespace();
    currentLoc->line = lineNumber;
    currentLoc->character = charNumber;
    StartBytes();
    switch (c) {
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
    case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
    case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
    case 's': case 't': case 'u': case 'v': case 'w': case 'x':
    case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
    case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
    case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
    case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
    case 'Y': case 'Z':
    case '.': case '_':
    case '0': case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
      this->ScanIdNumber(c);
      return;
    case '"':
      this->ScanText();
      return;
    case '/':
      {
	char c1 = GetChar();
	if (c1 == '/') {
	  this->ScanComment(c);
	  break;
	}
	else if (c1 == '*') {
	  char c2 = GetChar();
	  if (c2 == '*') {
	    if(this->ScanPragma())
	      {
		return;
	      }
	    else
	      {
		break;
	      }
	  }
	  else {
	    UngetChar(c2);
	    this->ScanComment(c1);
	    break;
	  }
	}
	tclass = TkSlash;
	this->AppendChar(c);
	UngetChar(c1);
	this->EndBytes();
	this->loc = currentLoc->Copy();
	return;
      }
    case '<':
      {
	this->AppendChar(c);
	c = GetChar();
	if (c == '=') {
	  this->AppendChar(c);
	  tclass = TkLessEq;
	}
	else {
	  UngetChar(c);
	  tclass = TkLess;
	}
	this->EndBytes();
	this->loc = currentLoc->Copy();
	return;
      }
    case '&': case '|': case '=': case '!': case '>': case '+':
      this->AppendChar(c);
      tclass = CharMap[c];
      c = GetChar();
      switch(c) {
      case '&': case '|': case '=': case '>': case '+':
	this->AppendChar(c);
	this->EndBytes();
	if (ReservedWords.Get(Text(bytes, (void*)1), tclass)) {
	  this->loc = currentLoc->Copy();
	  return;
	}
	this->UnAppendChar();
	// NB: No break here:
      default:
	UngetChar(c);
	this->EndBytes();
	this->loc = currentLoc->Copy();
	return;
      }
    case '\\': case ':': case ',': case '$': case '-': case '%': case '?':
    case ';': case '*':
    case '[': case ']': case '{': case '}': case '(': case ')':
      tclass = CharMap[c];
      this->AppendChar(c);
      this->EndBytes();
      this->loc = currentLoc->Copy();
      return;
    case ((char) EOF):
      tclass = TkEOF;
      return;
    default:      
      Error(cio().start_err(), Text("Bad character `") + c + "'.\n", currentLoc);
      cio().end_err();      
      throw("\nParsing terminated.\n");
    }
  }
}

void Token::LexFlush() {
  while (true) {
    int c = lexIn->get();
    if (c == EOF) break;
  }
}

void Token::Init(const Text& model, ShortId sid) {
  tclass = TkErr;
  expr   = NULL;
  currentLoc->Init(model, sid);
  this->loc = currentLoc;
}

/* These names should agree with the enumerated values of the "TokenClass"
   enumeration type. */
Text TokenNames[TkIllegal+1] =
  { "binding", "bool", "do", "else", "ERR", "FALSE", "files",
    "foreach", "from", "function", "if", "in", "import", "int",
    "list", "return", "text", "then", "TRUE", "type", "value",

    "Id", "Number", "String",

    "And", "EqEq", "NotEq", "GreaterEq", "Implies", "LessEq", "Or",
    "PlusPlus",

    "BackSlash", "Bang", "Colon", "Comma", "Dollar", "Equal", "Greater",
    "Less", "Minus", "Percent", "Plus", "Query", "Semicolon", "Slash",
    "Star", "Underscore",

    "LBrace", "RBrace", "LBracket", "RBracket", "LParen", "RParen",
    
    "Pragma",

    "End of File", "Illegal Token"
 };

extern "C" void LexInit_inner() {
  /* define the single character tokens: */
  CharMap['\\']= TkBackSlash;
  CharMap['!'] = TkBang;
  CharMap[':'] = TkColon;
  CharMap[','] = TkComma;
  CharMap['$'] = TkDollar;
  CharMap['='] = TkEqual;
  CharMap['>'] = TkGreater;
  CharMap['<'] = TkLess;
  CharMap['-'] = TkMinus;
  CharMap['%'] = TkPercent;
  CharMap['+'] = TkPlus;
  CharMap['?'] = TkQuery;
  CharMap[';'] = TkSemicolon;
  CharMap['/'] = TkSlash;
  CharMap['*'] = TkStar;
  CharMap['_'] = TkUnderscore;
  CharMap['{'] = TkLBrace;
  CharMap['}'] = TkRBrace;
  CharMap['['] = TkLBracket;
  CharMap[']'] = TkRBracket;
  CharMap['('] = TkLParen;
  CharMap[')'] = TkRParen;

  ReservedWords.Put("binding",  TkBinding);
  ReservedWords.Put("bool",     TkBool);
  ReservedWords.Put("do",       TkDo);
  ReservedWords.Put("else",     TkElse);
  ReservedWords.Put("ERR",      TkErr);
  ReservedWords.Put("FALSE",    TkFalse);
  ReservedWords.Put("files",    TkFiles);
  ReservedWords.Put("foreach",  TkForeach);
  ReservedWords.Put("from",     TkFrom);
  ReservedWords.Put("function", TkFunction);
  ReservedWords.Put("if",       TkIf);
  ReservedWords.Put("in",       TkIn);
  ReservedWords.Put("import",   TkImport);
  ReservedWords.Put("int",      TkInt);
  ReservedWords.Put("list",     TkList);
  ReservedWords.Put("return",   TkReturn);
  ReservedWords.Put("text",     TkText);
  ReservedWords.Put("then",     TkThen);
  ReservedWords.Put("TRUE",     TkTrue);
  ReservedWords.Put("type",     TkType);
  ReservedWords.Put("value",    TkValue);
  ReservedWords.Put("&&",       TkAnd);
  ReservedWords.Put("==",       TkEqEq);
  ReservedWords.Put("!=",       TkNotEq);
  ReservedWords.Put(">=",       TkGreaterEq);
  ReservedWords.Put("=>",       TkImplies);
  ReservedWords.Put("<=",       TkLessEq);
  ReservedWords.Put("||",       TkOr);
  ReservedWords.Put("++",       TkPlusPlus);
  ReservedWords.Put("/**",      TkPragma);
}

void LexInit()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, LexInit_inner);
}
