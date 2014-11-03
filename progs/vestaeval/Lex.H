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

// File: Lex.H                                                
// Last Modified On Fri Aug  6 11:59:17 EDT 2004 by ken@xorian.net 
//      Modified On Mon Feb  2 15:38:44 PST 1998 by yuanyu    
//      Modified On Fri Oct 31 10:02:42 PST 1997 by heydon    
//      Modified On Fri Jan 26 18:11:09 PST 1996 by horning   
//      Modified On Tue Dec  7 16:10:29 PST 1993 by hanna     

#ifndef Lex_H
#define Lex_H

#include "Location.H"
#include "ValExpr.H"
#include <VestaSource.H>
#include <iostream>

// Where we are currently lexing (position of the current token):
extern Text fileName;
extern ShortId shortId;
extern int lineNumber;
extern int charNumber;

// NB: When this enumeration is modified, it is essential to make
// corresponding modifications to the table initializations of TokenNames,
// CharMap, and ReservedWords in the function LexInit in Lex.C.
enum TokenClass
  { TkBinding, TkBool, TkDo, TkElse, TkErr, TkFalse, TkFiles,
    TkForeach, TkFrom, TkFunction, TkIf, TkIn, TkImport, TkInt,
    TkList, TkReturn, TkText, TkThen, TkTrue, TkType, TkValue,

    TkId, TkNumber, TkString,

    TkAnd, TkEqEq, TkNotEq, TkGreaterEq, TkImplies, TkLessEq, TkOr,
    TkPlusPlus,

    TkBackSlash, TkBang, TkColon, TkComma, TkDollar, TkEqual, TkGreater,
    TkLess, TkMinus, TkPercent, TkPlus, TkQuery, TkSemicolon, TkSlash,
    TkStar, TkUnderscore,

    TkLBrace, TkRBrace, TkLBracket, TkRBracket, TkLParen, TkRParen,

    TkPragma,

    TkEOF, TkIllegal };

// For printout:
extern Text TokenNames[];

class Token {
public:
  Token();

  TokenClass tclass;
  Expr expr;
  SrcLoc *loc;

  void Init(const Text& model, ShortId sid);
  void Next();
  void LexFlush();

  void StartBytes() { length = 0; bytes[0] = 0; };
  void AppendChar(char c);
  void UnAppendChar() { length--; };

  void EndBytes() { bytes[length] = 0; };
  char *Bytes() { return bytes; };
  Text AsText() { return Text(bytes); }; 
  int Length() { return length; };

  void TokenAssign(Token& tk);

private:
  char *bytes;
  int bytesLength;
  int length;

  void ScanIdNumber(char c);
  void ScanText();
  bool ScanPragma();
  void ScanComment(char c);

  Token(const Token&);
  Token &operator=(const Token&);
};

void LexInit();

#endif // Lex_H
