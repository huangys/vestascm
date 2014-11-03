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

// File: PrimRunTool.H

#ifndef PrimRunTool_H
#define PrimRunTool_H

#include "Val.H"
#include <FP.H>
#include <VestaSource.H>

typedef struct {
  SrcLoc *loc;
  TextVC *platform;
  ListVC *command_line;
  TextVC *stdin_data;
  TextVC *stdout_treatment;
  TextVC *stderr_treatment;
  TextVC *status_treatment;
  TextVC *signal_treatment;
  IntegerVC *fp_content;
  TextVC *wd_name;
  BooleanVC *existing_writable;
  BindingVC *dep_control;
  BindingVC *dot;
} RunToolArgs;

// Launch a tool:
Val RunTool(const RunToolArgs &args, VestaSource*& rootForTool);
/*
   run_tool_result = 
   [
     code: INT,              if 'signal' is 0, 'code' is the result
                             (i.e., the exit status) reported by the tool
     signal: INT,            if the tool terminated abnormally, this
                             specifies the reason.
     stdout_written: BOOL,   meaningful only for "reportError"
     stderr_written: BOOL,   meaningful only for "reportError"
     stdout: TEXT,           defined iff stdoutTreatment == "value"
     stderr: TEXT,           defined iff stderrTreatment == "value"
     root: BINDING,          files created elsewhere in the file system
  ]
  run_tool_result->dps records the files requested during this run.
*/

typedef struct {
  VestaSource::typeTag type;
  Text arc;
  unsigned int index;
} nodeinfo;

typedef nodeinfo* NodeInfo;

extern BindingVC *default_dep_control;

void DeleteRootForTool(VestaSource* rootForTool);
void CreateRootForDeriveds();
void DeleteRootForDeriveds();
VestaSource* CreateDerived();
void PrimRunToolInit();

// Convert the command-line of the tool invocation to a single text
// string.  (Used for debugging messages.)
Text ToolCommandLineAsText(const RunToolArgs &args);

#endif // PrimRunTool_H
