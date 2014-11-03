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

// File: Pickle.C
// Last modified on Wed Apr 27 10:52:31 EDT 2005 by irina.furman@intel.com
//      modified on Fri Aug  6 17:21:24 EDT 2004 by ken@xorian.net
//      modified on Sat Mar 14 11:04:36 PST 1998 by yuanyu
//      modified on Tue Feb 27 15:47:00 PST 1996 by levin

#include "PickleStats.H"
#include <FP.H>
#include <CacheC.H>
#include <VestaSource.H>

using std::ios;
using std::streampos;
using std::istream;
using std::ifstream;
using std::cout;
using std::cin;
using std::cerr;
using std::endl;

static int pickleDpndSize = 0;
static int bangFPNum = 0;
static int typeFPNum = 0;
static int pickleSize = 0;
static int depFPNum = 0;
static int bvSize = 0;
static int bvNum = 0;
static int bvEmptySize = 0;
static int bvEmptyNum = 0;

static int modelValSize;
static int textValSize;
static int modelValNum;
static int textValNum;
static int bindingValSize;
static int bindingValNum;
static int exprSize;
static int exprNum;
static int nameExprSize;
static int nameExprNum;
static int constantExprSize;
static int constantExprNum;
static int funcExprSize;
static int funcExprNum;
static int assignExprSize;
static int assignExprNum;
static int selectExprSize;
static int selectExprNum;
static int opExprSize;
static int opExprNum;
static int unOpExprSize;
static int unOpExprNum;
static int primExprSize;
static int primExprNum;
static int argListExprSize;
static int argListExprNum;
static int listExprSize;
static int listExprNum;

bool PickleValDpndSize(bool cut, istream *pickle);

bool SkipLocation(istream *pickle) {
  // unpickle loc->line:
  pickle->seekg(sizeof(char), ios::cur);
  // unpickle loc->character:
  pickle->seekg(sizeof(char), ios::cur);
  return true;
}

bool PickleContextDpndSize(istream *pickle) {
  bool success = true;
  short int cLen;
  pickle->read((char*)&cLen, sizeof(cLen));
  while (success && cLen--) {
    short int nameLen;
    pickle->read((char*)&nameLen, sizeof(nameLen));
    if (nameLen < 0 || nameLen > pickleSize)
      throw FS::EndOfFile();
    pickle->seekg(nameLen, ios::cur);
    success = PickleValDpndSize(true, pickle);
  }
  return success;
}

bool PickleDepPathSize(istream *pickle) {
  // Allocate the DepPath:
  short int pathLen;
  pickle->read((char*)(&pathLen), sizeof(pathLen));
  // path kind:
  char pk;
  pickle->read((char*)(&pk), sizeof(pk));
  if (pk == '!')
    bangFPNum++;
  else if (pk == 'T')
    typeFPNum++;
  // arcs:  
  for (int i = 0; i < pathLen; i++) {
    short int len;
    pickle->read((char*)(&len), sizeof(len));
    if (len < 0 || len > pickleSize)
      throw FS::EndOfFile();
    pickle->seekg(len, ios::cur);
  }
  // fingerprint:
  depFPNum++;
  FP::Tag tag;
  tag.Read(*pickle);
  return true;
}

bool PickleDPathsSize(istream *pickle) {
  short int dSize;
  pickle->read((char*)(&dSize), sizeof(dSize));
  for (int i = 0; i < dSize; i++) {
    PickleDepPathSize(pickle);
    FP::Tag tag;
    tag.Read(*pickle);
  }
  return true;
}

bool PickleDPSSize(istream *pickle) {
  /* Unpickle the dps of the value.  */
  streampos startPos = pickle->tellg();
  BitVector bv;
  bv.Read(*pickle);
  pickleDpndSize += pickle->tellg() - startPos;
  bvNum++;
  bvSize += pickle->tellg() - startPos;
  if (bv.IsEmpty()) {
    bvEmptySize += pickle->tellg() - startPos;
    bvEmptyNum++;
  }
  return true;
}

bool PickleLenDPSSize(istream *pickle) {
  streampos startPos = pickle->tellg();
  short int len;
  pickle->read((char*)&len, sizeof(len));
  while (len-- > 0) {
    PickleDepPathSize(pickle);
  }
  pickleDpndSize += pickle->tellg() - startPos;
  return true;
}

bool PickleExprDpndSize(istream *pickle) {
  exprNum++;
  SkipLocation(pickle);
  char ek;
  pickle->read((char*)&ek, sizeof(ek));
  switch (ek) {
  case ConstantEK:
    {
      streampos startPos = pickle->tellg();
      PickleValDpndSize(true, pickle);
      constantExprSize += pickle->tellg() - startPos;
      constantExprNum++;
      break;
    }
  case IfEK:
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    break;
  case ComputedEK:
    PickleExprDpndSize(pickle);
    break;
  case ExprListEK: case StmtListEK: case ListEK:
    {
      short int len;
      pickle->read((char*)&len, sizeof(len));
      while (len--)
	PickleExprDpndSize(pickle);
      listExprSize += sizeof(len);
      listExprNum++;
      break;
    }
  case ArgListEK: 
    {
      pickle->seekg(sizeof(Bit64), ios::cur);
      short int len;
      pickle->read((char*)&len, sizeof(len));
      while (len--)
	PickleExprDpndSize(pickle);
      argListExprSize += sizeof(Bit64) + sizeof(len);
      argListExprNum++;
      break;
    }
  case AssignEK:
    {
      PickleExprDpndSize(pickle);
      PickleExprDpndSize(pickle);
      pickle->seekg(sizeof(char), ios::cur);
      assignExprSize += sizeof(char);
      assignExprNum++;
      break;
    }
  case BindEK:
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    break;
  case NameEK:
    {
      short int len;
      pickle->read((char*)&len, sizeof(len));
      if (len < 0 || len > pickleSize)
	throw FS::EndOfFile();
      pickle->seekg(len, ios::cur);
      nameExprSize += sizeof(len) + len;
      nameExprNum++;
      break;
    }
  case BindingEK:
    {
      short int len;
      pickle->read((char*)&len, sizeof(len));
      while (len--)
	PickleExprDpndSize(pickle);
      break;
    }
  case ApplyOpEK:
    {
      // the operator:
      short int len;
      pickle->read((char*)&len, sizeof(len));
      if (len < 0 || len > pickleSize)
	throw FS::EndOfFile();
      pickle->seekg(len, ios::cur);
      // the operands:
      PickleExprDpndSize(pickle);
      PickleExprDpndSize(pickle);
      opExprSize += sizeof(len) + len;
      opExprNum++;
      break;
    }
  case ApplyUnOpEK:
    {
      // the operator:
      short int len;
      pickle->read((char*)&len, sizeof(len));
      if (len < 0 || len > pickleSize)
	throw FS::EndOfFile();
      pickle->seekg(len, ios::cur);
      // the operand:
      PickleExprDpndSize(pickle);
      unOpExprSize += sizeof(len) + len;
      unOpExprNum++;
      break;
    }
  case ModelEK:
    {
      PickleExprDpndSize(pickle);
      PickleExprDpndSize(pickle);
      PickleExprDpndSize(pickle);
      pickle->seekg(32, ios::cur);
      break;
    }
  case FileEK:
    {
      PickleExprDpndSize(pickle);
      short int len;
      pickle->read((char*)&len, sizeof(len));
      if (len < 0 || len > pickleSize)
	throw FS::EndOfFile();
      pickle->seekg(len, ios::cur);
      pickle->seekg(32, ios::cur);
      pickle->seekg(sizeof(bool), ios::cur);
      break;
    }
  case PrimitiveEK:
    {
      short int len;
      pickle->read((char*)&len, sizeof(len));
      if (len < 0 || len > pickleSize)
	throw FS::EndOfFile();
      pickle->seekg(len, ios::cur);
      primExprSize += sizeof(len) + len;
      primExprNum++;
      break;
    }
  case PairEK:
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    break;
  case SelectEK:
    {
      PickleExprDpndSize(pickle);
      PickleExprDpndSize(pickle);
      pickle->seekg(sizeof(char), ios::cur);
      selectExprSize += sizeof(char);
      selectExprNum++;
      break;
    }
  case FuncEK:
    {
      short int nameLen;
      pickle->read((char*)&nameLen, sizeof(nameLen));
      if (nameLen < 0 || nameLen > pickleSize)
	throw FS::EndOfFile();
      pickle->seekg(nameLen, ios::cur);
      pickle->seekg(sizeof(char), ios::cur);
      PickleExprDpndSize(pickle);
      PickleExprDpndSize(pickle);
      funcExprSize += sizeof(nameLen) + nameLen + sizeof(char);
      funcExprNum++;
      break;
    }
  case BlockEK:
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    pickle->seekg(sizeof(bool), ios::cur);
    break;
  case IterateEK:
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    break;
  case ApplyEK:
    PickleExprDpndSize(pickle);
    PickleExprDpndSize(pickle);
    break;
  case ErrorEK:    
    break;
  default:
    break;
  }
  return true;
}

bool PickleValDpndSize(bool cut, istream *pickle) {
  bool success = true;
  
  // path of the value:
  streampos startPos = pickle->tellg();
  char hasPath;
  pickle->read((char*)&hasPath, sizeof(hasPath));
  if ((bool)hasPath) PickleDepPathSize(pickle);
  pickleDpndSize += pickle->tellg() - startPos;

  // subvalues of the value:
  if (!hasPath || !cut) {
    char vk;
    pickle->read((char*)&vk, sizeof(vk));

    switch (vk) {
    case BooleanVK:
      pickle->seekg(sizeof(char), ios::cur);
      break;
    case IntegerVK:
      pickle->seekg(sizeof(int), ios::cur);
      break;
    case PrimitiveVK:
      {
	short int nameLen;
	pickle->read((char*)&nameLen, sizeof(nameLen));
	if (nameLen < 0 || nameLen > pickleSize)
	  throw FS::EndOfFile();
	pickle->seekg(nameLen, ios::cur);
	break;
      }
    case ListVK:
      {
	short int len;
	pickle->read((char*)&len, sizeof(len));
	while ((len-- > 0) && PickleValDpndSize(cut, pickle));
	success = PickleLenDPSSize(pickle);
	break;
      }
    case BindingVK:
      {
	short int len;
	pickle->read((char*)&len, sizeof(len));
	bindingValSize += sizeof(len);
	short int nameLen;
	while (success && len--) {
	  pickle->read((char*)&nameLen, sizeof(nameLen));
	  if (nameLen < 0 || nameLen > pickleSize)
	    throw FS::EndOfFile();
	  pickle->seekg(nameLen, ios::cur);
	  bindingValSize += sizeof(nameLen) + nameLen;
	  success = PickleValDpndSize(cut, pickle);
	}
	success = PickleLenDPSSize(pickle);
	bindingValNum++;
	break;
      }
    case TextVK:
      {
	streampos startPos = pickle->tellg();
	char hasTxt;
	pickle->read((char*)&hasTxt, sizeof(hasTxt));
	if ((bool)hasTxt) {
	  int tLen;
	  pickle->read((char*)&tLen, sizeof(tLen));
	  if (tLen < 0 || tLen > pickleSize)
	    throw FS::EndOfFile();
	  pickle->seekg(tLen, ios::cur);
	}
	else {
	  // Must have sid.
	  short int nameLen;
	  pickle->read((char*)&nameLen, sizeof(nameLen));
	  if (nameLen < 0 || nameLen > pickleSize)
	    throw FS::EndOfFile();
	  pickle->seekg(nameLen, ios::cur);
	  pickle->seekg(sizeof(ShortId), ios::cur);
	  FP::Tag tag;
	  tag.Read(*pickle);
	}
	textValSize += pickle->tellg() - startPos;
	textValNum++;
	break;
      }
    case ClosureVK:
      {
	// the function body:
	short int pathNameLen;
	pickle->read((char*)(&pathNameLen), sizeof(pathNameLen));
	if (pathNameLen < 0 || pathNameLen > pickleSize)
	  throw FS::EndOfFile();
	pickle->seekg(pathNameLen, ios::cur);
	pickle->seekg(sizeof(ShortId), ios::cur);
	pickle->seekg(sizeof(int), ios::cur);
	streampos startPos = pickle->tellg();

/*	char unpickled;
	pickle->read((char*)&unpickled, sizeof(unpickled));
	if (unpickled)
	  pickle->seekg(sizeof(int), ios::cur);
	else
	  success = PickleExprDpndSize(pickle);
*/
	PickleExprDpndSize(pickle);
	exprSize += pickle->tellg() - startPos;

	// the context:
	if (success)
	  success = PickleContextDpndSize(pickle);

	// the name:
	if (success) {
	  pickle->seekg(sizeof(bool), ios::cur);
	  int nameLen;
	  pickle->read((char*)&nameLen, sizeof(nameLen));
	  if (nameLen < 0 || nameLen > pickleSize)
	    throw FS::EndOfFile();
	  pickle->seekg(nameLen, ios::cur);
	}
	break;
      }
    case ModelVK:
      {
	streampos startPos = pickle->tellg();

	// Name of the model:
	short int nameLen;
	pickle->read((char*)&nameLen, sizeof(nameLen));
	if (nameLen < 0 || nameLen > pickleSize)
	  throw FS::EndOfFile();
	pickle->seekg(nameLen, ios::cur);

	// Sid of the model:
	pickle->seekg(sizeof(ShortId), ios::cur);

	// Lid for the model root:
	char lidLen;
	pickle->read((char*)&lidLen, sizeof(char));
	pickle->seekg(lidLen, ios::cur);

	// VestaSource of the model root:
	// We leave master, pseudoInode, ac, rep, and attribs uninitialized,
	// since we know we do not need them.  We believe we do not need the
	// sid and fptag, but we decided to pickle/unpickle them just in case.
	pickle->seekg(sizeof(ShortId), ios::cur);
	FP::Tag rootTag;
	rootTag.Read(*pickle);

	// Fingerprint:
	FP::Tag tag;
	tag.Read(*pickle);
	FP::Tag lidTag;
	lidTag.Read(*pickle);
	modelValSize += pickle->tellg() - startPos;
	modelValNum++;
	break;
      }
    case ErrorVK:
      break;
    default:
      cerr << "Bad value type in unpickling. Cache data may be corrupted!\n";
      success = false;
      break;
    }
  }
  if (success)
    success = PickleDPSSize(pickle);
  return success;
}

bool PickleDpndSize(istream *pickle) {
  modelValSize = modelValNum = 0;
  textValSize = textValNum = 0;
  bindingValSize = bindingValNum = 0;
  exprSize = exprNum = 0;
  nameExprSize = nameExprNum = 0;
  constantExprSize = constantExprNum = 0;
  funcExprSize = funcExprNum = 0;
  assignExprSize = assignExprNum = 0;
  selectExprSize = selectExprNum = 0;
  opExprSize = opExprNum = 0;
  unOpExprSize = unOpExprNum = 0;
  primExprSize = primExprNum = 0;
  argListExprSize = argListExprNum = 0;
  listExprSize = listExprNum = 0;
  try {
    // Version number:
    pickle->seekg(sizeof(int), ios::cur);
    // Dependency:
    streampos startPos = pickle->tellg();
    bool success = PickleDPathsSize(pickle);
    // Value:
    if (success) {
      pickleDpndSize = pickle->tellg() - startPos;
      return PickleValDpndSize(true, pickle);
    }
  } catch (FS::Failure f) {
    cerr << f;
  } catch (FS::EndOfFile f) {
    cerr << "EOF while collecting data for pickle value.\n";
  } catch (...) {
    cerr << "Unknown exception in collecting data for pickle value.\n";
  }
  return false;
}

int tellg(istream *is) { return is->tellg(); }

int main(int argc, char* argv[]) {
  istream *pickle = &cin;
  bool verbose = false;

  int i = 1;
  while (i < argc) {
    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "-verbose") == 0) {
 	verbose = true;
	i++;
      }
      else {
	cout << "\n++++ unrecognized flag: " << argv[i] << ".\n\n";
	exit(0);
      }
    }
    else
      pickle = NEW_CONSTR(ifstream, (argv[i++]));
  } // end of while

  if (verbose) cout << "Cache entries:\n";
  int valTotal = 0;
  int depTotal = 0;
  int entryNum = 0;
  int valMax = 0;
  int depMax = 0;
  int maxSize = 0;
  int modelValMax = 0;
  int textValMax = 0;
  while (pickle->read((char*)&pickleSize, sizeof(int))) {
    if (PickleDpndSize(pickle)) {
      valTotal += pickleSize - pickleDpndSize;
      depTotal += pickleDpndSize;
      entryNum++;
      if (maxSize < pickleSize) {
	valMax = pickleSize - pickleDpndSize;
	modelValMax = modelValSize;
	textValMax = textValSize;
	depMax = pickleDpndSize;
	maxSize = pickleSize;
      }
      if (verbose) {
	cout << "Val: " << (pickleSize - pickleDpndSize) << endl;
	cout << "  ModelVal: " << "[ size=" << modelValSize 
                               << ", num=" << modelValNum << " ]\n";
	cout << "  TextVal:" << "[ size=" << textValSize
                             << ", num=" << textValNum << " ]\n";
	cout << "  BindingVal: " << "[ size=" << bindingValSize
                                 << ", num=" << bindingValNum << " ]\n";
	cout << "  Expr: " << "[ size=" << exprSize
                           << ", num=" << exprNum << " ]\n";
	cout << "    NameExpr: " << "[ size=" << nameExprSize
                                 << ", num=" << nameExprNum << " ]\n";
	cout << "    ConstantExpr: " << "[ size=" << constantExprSize
                                     << ", num=" << constantExprNum << " ]\n";
	cout << "    FuncExpr: " << "[ size=" << funcExprSize
                                 << ", num=" << funcExprNum << " ]\n";
	cout << "    AssignExpr: " << "[ size=" << assignExprSize
                                   << ", num=" << assignExprNum << " ]\n";
	cout << "    SelectExpr: " << "[ size=" << selectExprSize
                                   << ", num=" << selectExprNum << " ]\n";
	cout << "    OpExpr: " << "[ size=" << opExprSize
                               << ", num=" << opExprNum << " ]\n";
	cout << "    UnOpExpr: " << "[ size=" << unOpExprSize
                                 << ", num=" << unOpExprNum << " ]\n";
	cout << "    PrimExpr: " << "[ size=" << primExprSize
                                 << ", num=" << primExprNum << " ]\n";
	cout << "    ArgListExpr: " << "[ size=" << argListExprSize
                                    << ", num=" << argListExprNum << " ]\n";
	cout << "    ListExpr: " << "[ size=" << listExprSize
                                 << ", num=" << listExprNum << " ]\n";
	cout << "Dpnd: " << pickleDpndSize << "\n\n";
      }
    }
    else {
      cerr << "Failed in PickleDpndSize.\n";
      break;
    }
  }
  if (entryNum > 0) {
    cout << "Number of Entry: " << entryNum << endl;
    cout << "FP: " << depFPNum << endl;
    cout << "BangFP: " << bangFPNum << endl;
    cout << "TypeFP: " << typeFPNum << endl;
    cout << "Bit Vectors: " << bvNum << ", " << bvSize << endl;
    cout << "Empty Bit Vectors: " << bvEmptyNum << ", " << bvEmptySize << endl;

    cout << "Max:" << endl;
    cout << "Val: " << valMax << endl;
    cout << "Dpnd: " << depMax << "\n\n";

    cout << "Average:" << endl;
    cout << "Val: " << (valTotal/entryNum) << endl;
    cout << "Dpnd: " << (depTotal/entryNum) << "\n\n";
  }
  else
    cerr << "The input file is empty.\n";
  return 0;
}
