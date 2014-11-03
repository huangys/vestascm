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

// File: Dep.H

#ifndef Dep_H
#define Dep_H

#include "ValExpr.H"
#include <FV2.H>
#include <FP.H>
#include <SharedTable.H>

typedef FV2::T ArcSeq;

// The kind of a DepPath.
enum PathKind {
  NormPK = 'N', BangPK = '!', TypePK = 'T', LLenPK = 'L', BLenPK = 'B',
  ExprPK = 'E', DummyPK = 'D' };

//// DepPath
class DepPath {
  // The representation of a path:
public:
  class DepPathC {
  public:
    DepPathC(int size)
      : pKind(NormPK), pathFP("", 0)
	{ path = NEW_CONSTR(ArcSeq, (size)); };
    DepPathC(const Text& id, PathKind pk);
    DepPathC(ArcSeq *tpath, PathKind pk);
    DepPathC(const ArcSeq& tpath, PathKind pk, const FP::Tag& fp)
      : pKind(pk), pathFP(fp)
	{ path = NEW_CONSTR(ArcSeq, (tpath)); };
    DepPathC(ArcSeq *tpath, PathKind pk, const FP::Tag& fp)
      : path(tpath), pKind(pk), pathFP(fp) { /*SKIP*/ };
    // Constructors.

    PathKind pKind;
    // The kind of a DepPath.

    ArcSeq *path;
    // A path is a sequence of arcs (text).
    
    FP::Tag pathFP;
    // For efficient equality test only.
  };
      
  DepPathC *content;
  // The content of this DepPath.

  DepPath() { content = NULL; };
  // Default.  We do not allocate anything.

  DepPath(int size) { content = NEW_CONSTR(DepPathC, (size)); };
  // Construct an empty new DepPath.

  DepPath(ArcSeq *tpath, PathKind pk, const FP::Tag& fp)
    { content = NEW_CONSTR(DepPathC, (tpath, pk, fp)); };
  // Construct a new DepPath using a text sequence.

  DepPath(const Text& id, PathKind pk = NormPK)
    { content = NEW_CONSTR(DepPathC, (id, pk)); };
  // Construct a new DepPath with singleton id.

  DepPath(ArcSeq *path, PathKind pk)
    { content = NEW_CONSTR(DepPathC, (path, pk)); };
  // Construct a new DepPath using a copy of the text sequence path.

  DepPath(DepPathC *tcontent)
    { content = NEW_CONSTR(DepPathC, 
			   (*tcontent->path,
			     tcontent->pKind,
			     tcontent->pathFP)); };
  // Construct a new DepPath by deep copying.

  void DeepCopy(const DepPath& dp)
    { content = NEW_CONSTR(DepPathC, 
			   (*dp.content->path,
			     dp.content->pKind,
			     dp.content->pathFP)); };
  // Make this to be a deep copy of dp.

  DepPath* DeepCopy()
    { return NEW_CONSTR(DepPath, (this->content)); };
  // Make a deep copy of this.

  void Print(std::ostream& os);
  // Print this dependency path.

  int Size() { return content->path->size(); };
  // The length of this dependency path.

  void Extend(const Text& id, PathKind pk = NormPK);
  // Extend (destructively) this path by id.

  void Extend(const ArcSeq& p, PathKind pk = NormPK);
  // Extend (destructively) this path by p from the end.

  void ExtendLow(const DepPath *dp);
  // Extend (destructively) this path by p from the front.

  Word Hash() const throw () { return content->pathFP.Hash(); };
  // Return a hash value for this.

  friend bool operator== (const DepPath& p1, const DepPath& p2)
    throw () {
      return ((p1.content->pathFP == p2.content->pathFP) && 
	      (p1.content->pKind == p2.content->pKind)); };
  // Compare equality of two DepPaths.
};

//// DepPathTbl
class DepPathTbl {
public:
  typedef SharedTable<DepPath, Val>::T T;
  typedef SharedTable<DepPath, Val>::Iterator TIter;
  typedef SharedTable<DepPath, Val>::KVPair KVPair;
  typedef KVPair *KVPairPtr;

  class DPS: public T {
  public:
    DPS(int sizeHint = 0) throw (): T(sizeHint) { /*SKIP*/ };
    DPS(const DPS *other) throw (): T(other ? other->Size() : 0 )
    {
      if(other != 0)
	Copy(other);
    }
    bool Member(const DepPath& p) { KVPairPtr dummyKV; return Get(p, dummyKV); };
    DPS* Add(DepPath *dp, Val v, PathKind pk = DummyPK);
    DPS* AddExtend(DepPath *dp, Val v, PathKind pk, const Text& id);

    // Note: this function was formerly named "Merge"
    DPS* Union(DPS *pds) { if (pds != NULL) this->Copy(pds); return this; };

    void Print(std::ostream& os, const Text &prefix = "");

    DPS* Difference(DPS *dps);

    bool Empty() { return this->Size() == 0; }

    DPS* Intersection(DPS *dps);

    DPS *Restrict(const Text& id);
    bool ContainsPrefix(const Text& id);

    static DPS* Intersection(DPS *psList[], unsigned int len);
  };
};

typedef DepPathTbl::DPS DPaths;

#endif // Dep_H
