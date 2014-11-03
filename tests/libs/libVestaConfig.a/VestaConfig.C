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

#include <Basics.H>
#include <BufStream.H>
#include <Generics.H>
#include <OS.H>
#include <FS.H>
#include "VestaConfig.H"
#include <dirent.h>
#include <algorithm>

using std::ifstream;
using std::endl;
using Basics::OBufStream;
using Basics::IBufStream;

//  **********************************************************
//  * Configuration parameters (documented in VestaConfig.H) *
//  **********************************************************

#define VESTA_CONFIG "VESTACONFIG"
#define DEFAULT_CONFIG_FILE_NAME "vesta.cfg"
#define DEFAULT_GLOBAL_CONFIG_FILE_NAME "/etc/vesta.cfg"
#define HOME_DIRECTORY "HOME"

#define MAX_INCLUDE_DEPTH 10

//  *******************************************
//  * Global variables (static class members) *
//  *******************************************

static Basics::mutex *m = 0;
static Text *location = 0;     // configuration file location - don't
			       // know where it is, yet
static int   include_depth = 0;

// Data structures holding parsed config file representation
static TextTextTbl *tbl = NULL;
typedef Table<Text,TextSeq *>::Default SectionVarsTbl;
typedef Table<Text,TextSeq *>::Iterator SectionVarsIter;
static SectionVarsTbl *vars_tbl = NULL;

bool remember_setting_location = false, remember_include_hierarchy = false;

typedef Table<Text, VestaConfig::SettingLocationSeq *>::Default SettingLocationTbl;
typedef Table<Text, VestaConfig::SettingLocationSeq *>::Iterator SettingLocationIter;
SettingLocationTbl *location_tbl = 0;

VestaConfig::IncludeTree *parsed_inc_tree = 0;

//  ******************
//  * Initialization *
//  ******************

extern "C" void VestaConfig_init_inner()
{
  m = NEW_PTRFREE(Basics::mutex);
  location = NEW(Text);
}

void VestaConfig_init()
{
  static pthread_once_t init_once = PTHREAD_ONCE_INIT;
  pthread_once(&init_once, VestaConfig_init_inner);
}

//  *************
//  * Utilities *
//  *************

static void report_error(const Text &msg, int line_no = -1)
{
  Text error_msg(msg);
  assert(location != 0);
  if (*location != "") {
    OBufStream s;
    if (line_no >= 0) s << ", line #" << line_no;
    error_msg = error_msg + " (file: " + *location + s.str() + ")";
  };
  throw(VestaConfig::failure(error_msg));
}

static void delete_parse()
{
  include_depth = 0;
  if (tbl != NULL) { delete tbl; tbl = NULL; }
  if (vars_tbl != NULL)
    {
      SectionVarsIter it(vars_tbl);
      Text sect;
      TextSeq *vars;
      while(it.Next(sect, vars))
	delete vars;
      delete vars_tbl; vars_tbl = NULL;
    }
  if(location_tbl != 0)
    {
      SettingLocationIter it(location_tbl);
      Text key;
      VestaConfig::SettingLocationSeq *seq;
      while(it.Next(key, seq))
	{
	  assert(seq != 0);
	  delete seq;
	}
      delete location_tbl;
      location_tbl = 0;
    }
  if(parsed_inc_tree != 0)
    {
      delete parsed_inc_tree;
      parsed_inc_tree = 0;
    }
}

VestaConfig::IncludeTree::IncludeTree(const IncludeTree &other)
  : file(other.file)
{
  for(int i = 0; i < other.included.size(); i++)
    {
      included.addhi(NEW_CONSTR(VestaConfig::IncludeTree, (*(other.included.get(i)))));
    }
}

VestaConfig::IncludeTree::~IncludeTree()
{
  while(included.size() > 0) delete included.remlo();
}

void VestaConfig::IncludeTree::Print(std::ostream &out, int indent)
{
  unsigned int i;
  for(i = 0; i < indent; i++)
    out << "\t";
  out << file << endl;
  for(i = 0; i < included.size(); i++)
    {
      VestaConfig::IncludeTree *kid = included.get(i);
      assert(kid != 0);
      kid->Print(out, indent+1);
    }
}

static Text make_key(const Text &section, const Text &name) {
  int len1 = section.Length();
  int len2 = name.Length() + 1; // include terminating NULL character
  char* buff = NEW_PTRFREE_ARRAY(char, 2 + len1 + len2);
  buff[0] = '['; buff[1+len1] = ']';
  memcpy(buff+1, section.cchars(), len1);
  memcpy(buff+2+len1, name.cchars(), len2);
  Text t(buff);
  delete [] buff;
  return t;
}

static void locate_file();
static void parse();
static void parse_file(const Text &file_name, VestaConfig::IncludeTree *include_tree);
static bool include_file(const Text &line, VestaConfig::IncludeTree *include_tree);

//  *********************************
//  * Configuration file processing *
//  *********************************

void VestaConfig::set_location(const Text &file_name)
{
  VestaConfig_init();
  m->lock();
  delete_parse();
  assert(location != 0);
  *location = file_name;
  m->unlock();
}

Text VestaConfig::get_location() throw(VestaConfig::failure)
{
  Text loc;
  VestaConfig_init();
  m->lock();
  try {
    locate_file();
  } catch (...) { m->unlock(); throw; }
  assert(location != 0);
  loc = *location;
  m->unlock();
  return loc;
}

static void locate_file()
{
  // Assumes m is locked.
  assert(location != 0);
  if (*location != "") return;

  // set_location was never called
  char *v(getenv(VESTA_CONFIG));
  if (v != NULL) { *location = v; return; }

  // The environment variable VESTA_CONFIG isn't defined.
  // Try home directory.
  struct stat dummy;
  v = getenv(HOME_DIRECTORY);
  if (v != NULL) {
    Text path = Text(v) + PathnameSep + DEFAULT_CONFIG_FILE_NAME;
    if (stat(path.chars(), &dummy) == SYSOK) { *location = path; return; }
  }

  // Try default global configuration file.
  if (stat(DEFAULT_GLOBAL_CONFIG_FILE_NAME, &dummy) == SYSOK) {
      *location = Text(DEFAULT_GLOBAL_CONFIG_FILE_NAME);
      return;
  }

  report_error("Can't find configuration file!");
}

static Text strip_whitespace(const Text &t)
{
  int j = t.Length()-1;
  while (j > 0 && (t[j] == ' ' || t[j] == '\t')) j--;
  int i = 0;
  while (i <= j && (t[i] == ' ' || t[i] == '\t')) i++;
  return t.Sub(i, j-i+1);
}

static void parse()
{
  // Assumes m is locked.

  if (tbl != NULL) return;     // already parsed

  locate_file();

  if(remember_include_hierarchy)
    {
      parsed_inc_tree = NEW_CONSTR(VestaConfig::IncludeTree, (*location));
    }
 else
   {
     assert(parsed_inc_tree == 0);
   }

  try {
    assert(location != 0);
    parse_file(location->chars(), parsed_inc_tree);
  } catch (VestaConfig::failure f) { delete_parse(); throw(f); }

  if (tbl == NULL) report_error("Empty configuration file");
}

static void parse_file(const Text &file_name, VestaConfig::IncludeTree *include_tree) {
  ifstream cf(file_name.chars());
  if (!cf) throw VestaConfig::failure("Can't open file: " + file_name);

  int line_no = 0;
  Text section;

  try {
    while (!cf.eof()) {
      Text line("");
      for (;;)  {
	char buff[100];
	(void) cf.getline(buff, sizeof(buff));
	line = line + buff;
#if defined(__DECCXX)
	// Work around a bug in the Compaq C++ iostream library.
	// According to the ISO C++ standard, getline is supposed to
	// set the 'fail' state bit when the buffer is too small for
	// the line.  The Compaq C++ RTL doesn't do so.  This makes it
	// impossible to differentiate between a long line which we
	// should continue to read and a line which fills the buffer
	// an integral number of times.
	if (cf.gcount() != (sizeof(buff) - 1)) break;
#else
	if(!cf.fail() || cf.eof()) break; 
#endif
	// we overflowed the buffer, so compliant implementations of
	// basic_ios will set the fail bit.  We clear it now, as we're
	// about to read the next chunk of the line.
	cf.clear();
      }

      line_no++;
      line = strip_whitespace(line);

      if (!(line.Length() == 0 || line[0] == ';' ||
	    (line.Length() > 1 && line[0] == '/' && line[1] == '/'))) {
	// Not a blank or comment line; analyze it.
	if (line[0] == '[') {
	  if (line[line.Length()-1] != ']') 
	    throw VestaConfig::failure("Missing ]");
	  line = strip_whitespace(line.Sub(1, line.Length()-2));
	  // Look for special case of [ include <filename> ]
	  if (include_file(line, include_tree))
	      section = Text("");
	  else
	      section = line;
	}
	else {
	  int i = line.FindChar('=');
	  if (i < 0) throw VestaConfig::failure("Missing =");
	  if (section.Empty())
	    throw VestaConfig::failure("Missing section name");
	  Text name = strip_whitespace(line.Sub(0, i));
	  if (name.Empty())
	    throw VestaConfig::failure("Missing name field");
	  Text key(make_key(section, name));
	  Text value(strip_whitespace(line.Sub(i+1)));
	  if (tbl == NULL)
	    tbl = NEW(TextTextTbl);
	  bool intbl = tbl->Put(key, value);
	  if(!intbl)
	    {
	      // No previous value for this setting.  Record it in
	      // vars_tbl.
	      if(vars_tbl == 0)
		vars_tbl = NEW(SectionVarsTbl);
	      TextSeq *sect_vars;
	      if(!vars_tbl->Get(section, sect_vars))
		{
		  sect_vars = NEW(TextSeq);
		  vars_tbl->Put(section, sect_vars);
		}
	      sect_vars->addhi(name);
	    }
	  if(remember_setting_location)
	    {
	      if(location_tbl == 0)
		{
		  assert(!intbl);
		  location_tbl = NEW(SettingLocationTbl);
		}
	      VestaConfig::SettingLocationSeq *location_seq = 0;
	      if(!location_tbl->Get(key, location_seq))
		{
		  location_seq = NEW(VestaConfig::SettingLocationSeq);
		  location_tbl->Put(key, location_seq);
		}
	      location_seq->addhi(VestaConfig::SettingLocation(file_name, line_no, value));
	    }
	}
      }
    }
  } catch (VestaConfig::failure f) {
    Text error_msg;
    OBufStream s;
    s << ", line #" << line_no;
    // Add backtrace line to error report.
    error_msg = f.msg + "\nfile: " + file_name + s.str();
    throw(VestaConfig::failure(error_msg));
  }
  
}

static TextSeq list_include(const Text &name) {
  TextSeq ret;
  if (FS::IsDirectory(name)) {
    // including a directory means include all the files in it
    DIR *dirPt = opendir(name.cchars());
    if(dirPt == NULL) {
      throw VestaConfig::failure("Can't open directory: " + name);
    }
    TextSeq included_files;
    while(dirent *dp = readdir(dirPt)) {
      if(strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")) {
	included_files.addhi(dp->d_name);
      }
    }
    (void)closedir(dirPt);
    // sort the entries into the proper include order
    std::sort(included_files.begin(), included_files.end(),
	      Basics::TextLessThanWithInts);
    Text inc_dir = name + "/";
    while(included_files.size()) {
      TextSeq kids = list_include(inc_dir + included_files.remlo());
      while(kids.size()) {
	ret.addhi(kids.remlo());
      }
    }
  } else { // must be a file
    ret.addhi(name);
  }

  return ret;
}

static bool include_file(const Text &line, VestaConfig::IncludeTree *include_tree) {
  int i;
  const Text incl("include");

  if ((i = line.FindChar(' ')) < 0 || line.Sub(0, i) != incl) return false;
  Text file_name(strip_whitespace(line.Sub(i)));
  if (file_name.Length() == 0)
      return false;  // ordinary section named [include]


  include_depth++;
  if (include_depth > MAX_INCLUDE_DEPTH)
    throw VestaConfig::failure("[include] statements are nested too deeply.");

  TextSeq included_files = list_include(file_name);

  while(included_files.size()) {
    Text inc_file = included_files.remlo();
    VestaConfig::IncludeTree *child_inc_tree =
      ((include_tree == 0) ? 0
       : NEW_CONSTR(VestaConfig::IncludeTree, (inc_file)));
    parse_file(inc_file, child_inc_tree);
    if(include_tree != 0) include_tree->included.addhi(child_inc_tree);
  }

  include_depth--;

  return true;  
}

void VestaConfig::record_setting_locations()
  throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();
  if(tbl != NULL)
    {
      m->unlock();
      report_error("record_setting_locations must be called before the file is parsed");
    }
  remember_setting_location = true;
  m->unlock();
}

void VestaConfig::record_include_hierarchy()
  throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();
  if(tbl != NULL)
    {
      m->unlock();
      report_error("record_include_hierarchy must be called before the file is parsed");
    }
  remember_include_hierarchy = true;
  m->unlock();
}

//  *****************
//  * Interrogation *
//  *****************

bool VestaConfig::get(const Text &section, const Text &name, Text &value)
     throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();

  try {
    parse();
  } catch (failure f) { m->unlock(); throw(f); }

  Text key(make_key(section, name));

  bool result = tbl->Get(key, value);

  m->unlock();
  return result;
}

Text VestaConfig::get_Text(const Text &section, const Text &name)
    throw(VestaConfig::failure)
{
  Text value;
  if (get(section, name, value)) return value;
  report_error("The name '" + name + "' in section '" + section +
		"' could not be found");
  return value; // not reached
}

int VestaConfig::get_int(const Text &section, const Text &name)
    throw(VestaConfig::failure)
{
  Text txt(get_Text(section, name));
  IBufStream ss(txt.chars());
  int value;
  if (!(ss >> value))
    report_error("The value for ([" + section + "], "
		 + name + ") isn't an integer");
  return value;
}

bool VestaConfig::get_bool(const Text &section, const Text &name)
  throw(VestaConfig::failure)
{
  Text txt(get_Text(section, name));

  if((strcasecmp("yes", txt.cchars()) == 0) ||
     (strcasecmp("on", txt.cchars()) == 0) ||
     (strcasecmp("true", txt.cchars()) == 0))
    {
      return true;
    }
  else if((strcasecmp("no", txt.cchars()) == 0) ||
	  (strcasecmp("off", txt.cchars()) == 0) ||
	  (strcasecmp("false", txt.cchars()) == 0))
    {
      return false;
    }
  else
    {
      IBufStream ss(txt.chars());
      int value;
      if (!(ss >> value))
	report_error("The value for ([" + section + "], "
		     + name + ") isn't a boolean");
      return (value != 0);
    }
}

bool VestaConfig::is_set(const Text &section, const Text &name)
  throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();

  try {
    parse();
  } catch (failure f) { m->unlock(); throw(f); }

  Text key(make_key(section, name));

  Text value;
  bool result = tbl->Get(key, value);

  m->unlock();
  return result;
}

float VestaConfig::get_float(const Text &section, const Text &name)
    throw(VestaConfig::failure)
{
  Text txt(get_Text(section, name));
  IBufStream ss(txt.chars());
  float value;
  if (!(ss >> value))
    report_error("The value for ([" + section + "], "
		 + name + ") isn't a floating-point number");
  return value;
}

TextSeq VestaConfig::sections()
  throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();

  try {
    parse();
  } catch (failure f) { m->unlock(); throw(f); }

  TextSeq result;

  if(vars_tbl != 0)
    {
      SectionVarsIter it(vars_tbl);
      Text sect;
      TextSeq *vars;
      while(it.Next(sect, vars))
	result.addhi(sect);
    }

  m->unlock();

  return result;
}

TextSeq VestaConfig::section_vars(const Text &section)
  throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();

  try {
    parse();
  } catch (failure f) { m->unlock(); throw(f); }

  TextSeq result, *sect_vars = 0;

  if((vars_tbl != 0) && vars_tbl->Get(section, sect_vars))
    result = *sect_vars;

  m->unlock();

  return result;
}

VestaConfig::SettingLocationSeq
VestaConfig::get_setting_locations(const Text &section, const Text &name)
  throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();

  if(!remember_setting_location)
    {
      m->unlock();
      report_error("Must call record_setting_locations before get_setting_locations");
    }

  try {
    parse();
  } catch (failure f) { m->unlock(); throw(f); }

  Text key(make_key(section, name));

  SettingLocationSeq result, *setting_location = 0;

  if((location_tbl != 0) && location_tbl->Get(key, setting_location))
    result = *setting_location;

  m->unlock();

  return result;
}

VestaConfig::IncludeTree *VestaConfig::get_include_hierarchy()
  throw(VestaConfig::failure)
{
  VestaConfig_init();
  m->lock();

  if(!remember_include_hierarchy)
    {
      m->unlock();
      report_error("Must call record_include_hierarchy before get_include_hierarchy");
    }

  try {
    parse();
  } catch (failure f) { m->unlock(); throw(f); }

  assert(parsed_inc_tree != 0);

  VestaConfig::IncludeTree *result =
    NEW_CONSTR(VestaConfig::IncludeTree, (*parsed_inc_tree));

  m->unlock();

  return result;
}
