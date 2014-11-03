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

// File: Dep.C

#include "Dep.H"
#include "Files.H"
#include "Val.H"
#include <FP.H>
#include <SharedTable.H>

using std::ostream;
using std::endl;

// DepPathC
DepPath::DepPathC::DepPathC(const Text& id, PathKind pk)
  : pKind(pk), pathFP(id) {
  this->path = NEW(ArcSeq);
  this->path->addhi(id);
}

DepPath::DepPathC::DepPathC(ArcSeq *tpath, PathKind pk)
  : pKind(pk), path(tpath) {
  RawFP fp = POLY_ONE;
  if (tpath->size() != 0) {
    FP::Tag::ExtendRaw(/*INOUT*/ fp, tpath->get(0));
    for (int i = 1; i < tpath->size(); i++) {
      FP::Tag::ExtendRaw(/*INOUT*/ fp, PathnameSep);
      FP::Tag::ExtendRaw(/*INOUT*/ fp, tpath->get(i));
    }
  }
  this->pathFP.Permute(fp);
}

// DepPath
void DepPath::Print(ostream& os) 
{
  Text arc;
  os << (char)(this->content->pKind);
  ArcSeq *path = this->content->path;
  for (int i = 0; i < path->size(); i++) {
    arc = path->get(i);
    os << "/" << arc;
  }
}

void DepPath::Extend(const Text& id, PathKind pk) {
  // assert(this->content->path->size() != 0);
  this->content->path->addhi(id);
  RawFP fp;
  this->content->pathFP.Unpermute(/*OUT*/ fp);
  FP::Tag::ExtendRaw(/*INOUT*/ fp, PathnameSep);
  FP::Tag::ExtendRaw(/*INOUT*/ fp, id);
  this->content->pathFP.Permute(fp);
  this->content->pKind = pk;
}

void DepPath::Extend(const ArcSeq& p, PathKind pk) {
  /* assert(this->content->pKind == NormPK) &&
     assert(this->content->path->size() != 0)  */
  RawFP fp;
  this->content->pathFP.Unpermute(/*OUT*/ fp);
  for (int i = 0; i < p.size(); i++) {
    this->content->path->addhi(p.get(i));
    FP::Tag::ExtendRaw(/*INOUT*/ fp, PathnameSep);
    FP::Tag::ExtendRaw(/*INOUT*/ fp, p.get(i));
  }
  this->content->pathFP.Permute(fp);
  this->content->pKind = pk;
}

void DepPath::ExtendLow(const DepPath *dp) {
  // assert(dp->content->path->size() != 0);
  ArcSeq *p = dp->content->path;
  ArcSeq *p1 = this->content->path;
  RawFP fp;
  dp->content->pathFP.Unpermute(/*OUT*/ fp);
  int i;
  for (i = 0; i < content->path->size(); i++) {
    FP::Tag::ExtendRaw(/*INOUT*/ fp, PathnameSep);
    FP::Tag::ExtendRaw(/*INOUT*/ fp, p1->get(i));
  }
  this->content->pathFP.Permute(fp);
  for (i = p->size() - 1; i >= 0; i--) {
    p1->addlo(p->get(i));
  }
}

// DepPathTbl
DPaths* DepPathTbl::DPS::Add(DepPath *dp, Val v, PathKind pk) {
  if (dp != NULL) {
    KVPairPtr ptr;
    if (pk == DummyPK)
      ptr = NEW_CONSTR(KVPair, (*dp, v));
    else {
      // We do not copy path, since only pKind is changed.
      DepPath newPath(dp->content->path, pk, dp->content->pathFP);
      ptr = NEW_CONSTR(KVPair, (newPath, v));
    }
    this->Put(ptr); 
  }
  return this;
}

DPaths* DepPathTbl::DPS::AddExtend(DepPath *dp, Val v, 
				   PathKind pk, const Text& id) {
  if (dp != NULL) {
    // We have to deepcopy dp for newPath, since both path and 
    // pKind are changed.
    DepPath newPath(dp->content);
    newPath.Extend(id);
    newPath.content->pKind = pk;
    KVPairPtr ptr = NEW_CONSTR(KVPair, (newPath, v));
    this->Put(ptr);
  }
  return this;
}

void DepPathTbl::DPS::Print(ostream& os, const Text &prefix) 
{
  TIter iter(this);
  KVPairPtr ptr;
  int n = 0;
  while (iter.Next(ptr)) {
    os << endl << prefix << "  " << n++ << ". ";
    ptr->key.Print(os);
    os << " : ";
    ptr->val->PrintD(os);
  }
  os << endl;
}

DPaths* DepPathTbl::DPS::Difference(DPaths *dps)
{
  DepPathTbl::TIter iter(this);
  DepPathTbl::KVPairPtr ptr;
  DPaths *newDps = NEW_CONSTR(DPaths, (this->Size()));
  while (iter.Next(ptr)) {
    if (!dps->Member(ptr->key))
      (void)newDps->Put(ptr);
  }

  if(newDps->Size() == 0) return 0;
  return newDps;
}

DPaths* DepPathTbl::DPS::Intersection(DPaths* dps)
{
  DPaths *list[2];
  list[0] = this;
  list[1] = dps;
  return Intersection(list, 2);
}

DPaths* DepPathTbl::DPS::Intersection(DPaths *psList[], unsigned int len)
{
  assert(len > 0);

  DepPathTbl::TIter iter(psList[0]);
  DepPathTbl::KVPairPtr ptr;
  DPaths *newDps = NEW(DPaths);
  while (iter.Next(ptr)) {
    bool shared = true;
    // Test if it is in the intersection
    int i;
    for (i = 1; i < len; i++) {
      if (!psList[i]->Member(ptr->key)) {
	shared = false;
	break;
      }
    }
    if (shared) {
      // Copy it to result
      (void)newDps->Put(ptr);
    }
  }

  if(newDps->Size() == 0) return 0;
  return newDps;
}

DPaths *DepPathTbl::DPS::Restrict(const Text& id)
{
  DepPathTbl::TIter iter(this);
  DepPathTbl::KVPairPtr ptr;
  DPaths *newDps = NEW(DPaths);
  while (iter.Next(ptr)) {
    if (ptr->key.content->path->getlo() == id) {
      (void)newDps->Put(ptr);
    }
  }

  if(newDps->Size() == 0) return 0;
  return newDps;
}

bool DepPathTbl::DPS::ContainsPrefix(const Text& id)
{
  DepPathTbl::TIter iter(this);
  DepPathTbl::KVPairPtr ptr;
  while (iter.Next(ptr)) {
    if (ptr->key.content->path->getlo() == id)
      return true;
  }
  return false;
}
