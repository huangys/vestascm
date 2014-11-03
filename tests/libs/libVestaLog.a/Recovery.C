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
// Recovery.C
// Code to drive recovery from log for repository server
// 

#include "VestaLog.H"
#include "Recovery.H"
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

using std::istream;

// Methods for RecoveryReader
RecoveryReader::RecoveryReader(VestaLog* vl) throw ()
{
    vl_ = vl;
    is_ = NULL;
}

RecoveryReader::RecoveryReader(istream* is) throw ()
{
    vl_ = NULL;
    is_ = is;
}

void
RecoveryReader::get(char &c) throw(VestaLog::Eof, VestaLog::Error)
{
    if (vl_ != NULL) {
	vl_->get(c);
    } else {
	if (is_->get(c)) return;
	if (is_->bad())
	  throw VestaLog::Error(errno, Text("RecoveryReader::get got \"") +
				Basics::errno_Text(errno) + "\" on read");
	throw VestaLog::Eof();
    }
}

void
RecoveryReader::get(char* p, int n, char term)
  throw(VestaLog::Eof, VestaLog::Error)
{
    if (vl_ != NULL) {
	vl_->get(p, n, term);
    } else {
	if (is_->get(p, n, term)) return;
	if (is_->bad())
	  throw VestaLog::Error(errno, Text("RecoveryReader::get got \"") +
				Basics::errno_Text(errno) + "\" on read");
	throw VestaLog::Eof();
    }
}

int
RecoveryReader::read(char* p, int n) throw(VestaLog::Error)
{
    if (vl_ != NULL) {
	return vl_->read(p, n);
    } else {
	if (is_->read(p, n)) {
	    return n;
	} else if (is_->bad()) {
	    throw VestaLog::Error(errno, Text("RecoveryReader::read got \"") +
				  Basics::errno_Text(errno) + "\" on read");
	} else {
	    return is_->gcount();
	}
    }
}

void
RecoveryReader::readAll(char* p, int n) throw(VestaLog::Eof, VestaLog::Error)
{
    if (vl_ != NULL) {
	vl_->readAll(p, n);
	return;
    } else {
	if (is_->read(p, n)) {
	    return;
	} else if (is_->bad()) {
	    throw VestaLog::Error(errno,
				  Text("RecoveryReader::readAll got \"") +
				  Basics::errno_Text(errno) + "\" on read");
	} else {
	    throw VestaLog::Eof();
	}
    }
}

bool
RecoveryReader::eof() throw(VestaLog::Error)
{
    if (vl_ != NULL) {
	return vl_->eof();
    } else if (is_->peek() == EOF) {
	if (is_->bad()) {
	    throw VestaLog::Error(errno,
				  Text("RecoveryReader::eof got \"") +
				  Basics::errno_Text(errno) + "\" on peek");
	}
	return true;
    }
    return false;
}

// Helper routines for RecoverFrom.  The c argument is a lookahead
// character, already read from the reader but not yet used.

void
RecoveryReader::skipWhite(char& c) throw(VestaLog::Eof, VestaLog::Error)
{
    while (isspace(c)) {
	this->get(c);
    }
}	

void
RecoveryReader::requireChar(char& c, char required)
  throw(VestaLog::Eof, VestaLog::Error)
{
    this->skipWhite(c);
    if (c != required) {
	throw VestaLog::Error(0, Text("RecoveryReader::requireChar: ") +
			      "expected '" + required + "'; got '" + c + "'");
    }
    this->get(c);
}	

void
RecoveryReader::getIdent(char& c, Ident id)
  throw(VestaLog::Eof, VestaLog::Error)
{
    int guard = sizeof(Ident) - 1;
    char* p = &id[0];
    this->skipWhite(c);
    while (isalnum(c)) {
	if (guard-- <= 0) {
	    id[sizeof(Ident) - 1] = '\0';
	    throw VestaLog::Error(0, Text("RecoveryReader::getIdent: ") +
				  "ident too long: " + id + "...");
	}
	*p++ = c;
	this->get(c);
    }
    *p = '\0';
}	

void
RecoveryReader::getLong(char& c, long& n)
  throw(VestaLog::Eof, VestaLog::Error)
{
    char digits[256]; // arbitrary limit on number length
    char *endptr;
    int guard = sizeof(digits) - 1;
    char* p = &digits[0];
    this->skipWhite(c);
    while (isxdigit(c) || c == 'x' || c == 'X' || c == '-' || c == '+') {
	if (guard-- <= 0) {
	    digits[sizeof(Ident) - 1] = '\0';
	    throw VestaLog::Error(0, Text("RecoveryReader::getLong: ") +
				  "number too long: " + digits + "...");
	}
	*p++ = c;
	this->get(c);
    }
    *p = '\0';
    n = strtol(digits, &endptr, 0);
    if (*endptr != '\0') {
	n = 0;
	throw VestaLog::Error(0, Text("RecoveryReader::getLong: ") +
			      "garbled number: " + digits);
    }
}

void
RecoveryReader::getULong(char& c, unsigned long& n)
  throw(VestaLog::Eof, VestaLog::Error)
{
    char digits[256]; // arbitrary limit on number length
    char *endptr;
    int guard = sizeof(digits) - 1;
    char* p = &digits[0];
    this->skipWhite(c);
    while (isxdigit(c) || c == 'x' || c == 'X' || c == '-' || c == '+') {
	if (guard-- <= 0) {
	    digits[sizeof(Ident) - 1] = '\0';
	    throw VestaLog::Error(0, Text("RecoveryReader::getULong: ") +
				  "number too long: " + digits + "...");
	}
	*p++ = c;
	this->get(c);
    }
    *p = '\0';
    n = strtoul(digits, &endptr, 0);
    if (*endptr != '\0') {
	n = 0;
	throw VestaLog::Error(0, Text("RecoveryReader::getULong: ") +
			      "garbled number: " + digits);
    }
}

void
RecoveryReader::getLongId(char& c, Byte32& value)
  throw(VestaLog::Eof, VestaLog::Error)
{
    int i;
    char chr[3];
    chr[2] = '\000';
    this->skipWhite(c);
    if (!isxdigit(c)) {
	throw VestaLog::Error(0, Text("RecoveryReader::getLongId: ") +
			      "bad LongId: " + c + "...");
    }
    for (i=0; i<32; i++) {
	if (!isxdigit(c)) break;
	int byte;
	chr[0] = c;
	this->get(c);
	if (isxdigit(c)) {
	    chr[1] = c;
	    this->get(c);
	} else {
	    chr[1] = '0';
	}
	value.byte[i] = strtol(chr, NULL, 16);
    }
    for (; i<32; i++) {
	value.byte[i] = 0;
    }
}

void
RecoveryReader::getQuotedString(char& c, char* buf, int maxLen)
  throw(VestaLog::Eof, VestaLog::Error)
{
    this->skipWhite(c);
    this->requireChar(c, '\"');
    char *p = buf;
    while (maxLen > 0) {
	if (c == '\\') {
	    this->get(c);
	    *p++ = c;
	    maxLen--;
	    this->get(c);
	} else if (c == '\"') {
	    *p = '\000';
	    this->get(c);
	    return;
	} else if (c == '\000') {
	    *p = '\000';
	    throw
	      VestaLog::Error(0, Text("RecoveryReader::getQuotedString: ") +
			      "illegal null character after: " + buf);
	} else {
	    *p++ = c;
	    maxLen--;
	    this->get(c);
	}
    }
    *--p = '\000';  // overwrite last character to null-terminate string
    throw VestaLog::Error(0, Text("RecoveryReader::getQuotedString: ") +
			  "string too long: " + buf + "...\n");
}

void
RecoveryReader::getNewQuotedString(char& c, char*& string)
  throw(VestaLog::Eof, VestaLog::Error)
{
    this->skipWhite(c);
    this->requireChar(c, '\"');
    int stringLen = 64;
    int len = 0;
    char* p = string = NEW_PTRFREE_ARRAY(char, stringLen);
    for (;;) {
	while (len < stringLen) {
	    if (c == '\\') {
		this->get(c);
		*p++ = c;
		len++;
		this->get(c);
	    } else if (c == '\"') {
		*p = '\000';
		this->get(c);
		return;
	    } else if (c == '\000') {
		*p = '\000';
		VestaLog::Error
		  f(0, Text("RecoveryReader::getNewQuotedString: ") +
		    "illegal null character after: " + string);
		delete [] string;
		throw f;
	    } else {
		*p++ = c;
		len++;
		this->get(c);
	    }
	}
	// realloc, the hard way :-)
	int oldLen = stringLen;
	char *oldString = string;
	stringLen *= 2;
	string = NEW_PTRFREE_ARRAY(char, stringLen);
	memcpy(string, oldString, oldLen);
	delete [] oldString;

	p = &string[len];
    }
}

struct RecoveryDispatch {
    RecoveryDispatch* next;
    char* id;
    RecoveryCallback* rc;
};

// No locking here because the data is constant after initialization
static RecoveryDispatch* RDHead;

void
RegisterRecoveryCallback(RecoveryReader::Ident id, RecoveryCallback* rc)
  throw ()
{
  RecoveryDispatch* rd = NEW(RecoveryDispatch);
  rd->next = RDHead;
  rd->id = strdup(id);
  rd->rc = rc;
  RDHead = rd;
}

// Read records from a RecoveryReader, determine their type, and call
// type-specific code to process them.  This code is used to process
// both checkpoints and logs, because the repository server writes the
// same types of records to both.  (Hence the need for the
// RecoverReader wrapper class.)  Essentially this is a
// recursive-descent parser for log records.  The types are so simple
// that there isn't really any recursion, however.
//
void
RecoverFrom(RecoveryReader* rr) throw(VestaLog::Error)
{
    char c;
    RecoveryReader::Ident id;

    try {
	try {
	    rr->get(c);		// Prime the pump
	} catch (VestaLog::Eof) {
	    return;		// normal end of log
	}
	for (;;) {
	    try {
		rr->skipWhite(c);
	    } catch (VestaLog::Eof) {
		return;		// normal end of log
	    }
	    rr->requireChar(c, '(');
	    rr->skipWhite(c);
	    rr->getIdent(c, id);    
	    
	    // Dispatch on id
	    RecoveryDispatch* rd = RDHead;
	    for (;;) {
		if (rd == NULL) {
		    throw VestaLog::Error(0,
		      Text("RecoveryReader::recoverFrom: unknown record type ")
                      + id);
		}
		if (strcmp(id, rd->id) == 0) {
		    rd->rc(rr, c);
		    break;
		}
		rd = rd->next;
	    }
	    
	    try {
		rr->requireChar(c, ')');
	    } catch (VestaLog::Eof) {
		return;		// log ends following the ')'; this is normal
	    }
	}
    } catch (VestaLog::Eof) {
	throw VestaLog::Error(0,
	  "RecoveryReader::recoverFrom: unexpected end of file");
    }
}
