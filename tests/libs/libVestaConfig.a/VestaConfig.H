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


// The VestaConfig class provides an interface to the central configuration
// file for a Vesta installation.  The configuration file is stored at
// an installation-specific location; see the comments on the get() method.
//
// The configuration file itself is a text file with the following syntax
//
// config file = line*
// line = ws ( sect | option | comment ) ws new-line
// sect = '[' ws name ws ']' | '[' "include" name ']'
// option = name ws '=' ws value
// comment = ( ';' | "//" ) non-new-line* | <empty>
// name = ( letter | digit | special )+
// value = non-new-line*
// new-line = <ASCII CR> | <ASCII LF>
// non-new-line = <any ASCII character except those for new-line>
// ws = <space> | <tab>
//
// This syntax is slightly ambiguous, but the intent should be clear.
// The file is a sequence of lines, each of which is either a section name
// in square brackets, a <name,value> pair separated by an equal sign, or
// a comment.  Leading and trailing white space is ignored, as are blank
// lines and comment lines.  A comment line has a semicolon or two slashes
// as its first non-whitespace characters.
//
// The configuration file defines a two-level naming structure, with
// sections as the upper level and the names of <name, value> pairs as the
// lower level.  Once the file has been parsed, the basic operation is
// to lookup a <section, name> pair, obtaining the associated value.
//
// There is a limited facility for one configuration file to refer to
// another one.  Notice the second alternative for the 'sect' nonterminal
// in the grammar above.  When this form is used, it doesn't introduce
// a section name; instead, the specified file is read and processed instead.
// That is, if the file contains a line of the form:
//     [include /proj/vesta/pkg/config-file/vesta.cfg]
// then the line will effectively be replaced by the contents of the file
// /proj/vesta/pkg/config-file/vesta.cfg
//
// This mechanism is chiefly intended for overriding a set of shared
// defaults.  A configuration file containing standard definitions is
// stored in a public location (e.g., /etc/vesta.cfg; or at SRC,
// /udir/vestasrv/misc/vesta.cfg).  A second configuration file is stored
// in the user's home directory or at the location specified by the
// environment variable VESTACONFIG.  This second file begins with an
// "include" line that mentions the first file, then continues with
// additional sections that either add to or override the standard
// definitions.  Thus, it is not an error for a <section, name> pair to
// appear more than once in the course of configuration file processing
// (which may involve reading multiple files).  The last-encountered value
// wins.

#ifndef _VestaConfig_H
#define _VestaConfig_H

#include <Basics.H>
#include <Generics.H>

namespace VestaConfig {

  struct failure {
    Text msg;
    inline failure(const Text &msg) { this->msg = msg; };
    inline failure(const failure &f) { this->msg = f.msg; };
    inline failure& operator=(const failure &f) {
      this->msg = f.msg; return *this; };
  };

  // A record of which file and what line a particular setting was
  // made at, and to which value it was set.
  struct SettingLocation
  {
    Text file;
    unsigned int line;
    Text val;
    SettingLocation() {}
    SettingLocation(const Text &f, unsigned int l, const Text &v)
      : file(f), line(l), val(v)
    {
    }
  };
  // A sequence of setting locations, used to represent all the places
  // where a particlar setting was assigned a value
  typedef Sequence<SettingLocation> SettingLocationSeq;

  // A record of which config files included which other config files.
  struct IncludeTree
  {
    Text file;
    Sequence<IncludeTree *> included;
    IncludeTree() {}
    IncludeTree(const Text &f) : file(f) {}
    IncludeTree(const IncludeTree &other);
    ~IncludeTree();
    void Print(std::ostream &out, int indent = 0);
  };

  //  *********************************
  //  *  Configuration file location  *
  //  *********************************

  void set_location(const Text &file_name);
  // 'file_name' specifies the location of the configuration file.  This
  // file's contents are used by subsequent calls of 'get' (see below).

  Text get_location() throw(failure);
  // Returns the location of the configuration file.  The location is
  // is determined as described under 'get', below.

  void record_setting_locations() throw(failure);
  // Request that the file locations of settings be remembered during
  // parsing.  Enables the "get_setting_locations" function below.
  // Must be called before the config file is first parsed.

  void record_include_hierarchy() throw(failure);
  // Request that this config file include hierarchy be remembered
  // during parsing.  Enables the "get_include_hierarchy" function
  // below.  Must be called before the config file is first parsed.

  //  ********************************************
  //  *  Reading fields from configuration file  *
  //  ********************************************

  bool get(const Text &section, const Text &name, Text &value)
    throw(failure);
  // The configuration file is examined for a section whose name is 'section'.
  // If such a section is found and it contains a <name, value> pair whose
  // name matches the 'name' argument, then the value is stored in the
  // 'value' argument and this function returns true.  If no matching
  // section and name can be found, the function leaves 'value' unchanged
  // and returns false.  If the configuration file cannot be
  // successfully read and parsed, 'failure' is thrown.
  //
  // The location of the configuration file is determined as follows:
  //    1.  If 'set_location' was previously called, the path it
  //        specified is used.  If the specified file can't be read,
  //        'failure' is thrown.
  //    2.  If 'set_location' was not previously invoked, the value of
  //        the environment variable VESTACONFIG is used.  If the
  //        specified file can't be found, 'failure' is thrown.
  //    3.  If VESTACONFIG isn't defined, the environment variable
  //        HOME is used to define a directory from which 'vesta.cfg'
  //        is read.  If it exists, it is read and used for all
  //        subsequent calls of 'get', unless 'set_location' is
  //        subsequently called.
  //    4.  If 'vesta.cfg' is not found in $HOME, /etc/vesta.cfg is
  //        tried next.  If it exists, it is read and used for all
  //        subsequent calls of 'get', unless 'set_location' is
  //        subsequently called.  If it does not exist, 'failure' is
  //        thrown. 

  bool is_set(const Text &section, const Text &name)
    throw(failure);
  // This function is semantically equivalent to:
  //    Text value;
  //    return get(section, name, value);
  // It avoids the need for declaring a dummy variable to call get to
  // determine whether a variable is set.

  Text get_Text(const Text &section, const Text &name)
    throw(failure);
  // This function is semantically equivalent to:
  //    Text value;
  //    if (get(section, name, value)) return value;
  //    throw(failure(<suitable message about [section]name not found>));

  int get_int(const Text &section, const Text &name)
    throw(failure);
  // This function is semantically equivalent to:
  //    istrstream ss(get_Text(section, name).chars());
  //    int value;
  //    if (ss >> value) return value;
  //    throw(failure(<message about [section]name not an integer>));

  bool get_bool(const Text &section, const Text &name)
    throw(failure);
  // This function inteprets the setting of a configuration variable
  // as a boolean.  If it is equal (witout regard to case) to "yes",
  // "on", or "true", or can be parsed as an integer that is non-zero,
  // it's interpreted as a true value.  If it is equal (witout regard
  // to case) to "no", "off", or "false", or can be parsed as an
  // integer that is zero, it's interpreted as a false value.
  // Otherwise an expetion is thrown.  (Exceptions can also be thrown
  // if the value isn't set or if there are parsing errors.)

  float get_float(const Text &section, const Text &name)
    throw(failure);
  // This function is semantically equivalent to:
  //    istrstream ss(get_Text(section, name).chars());
  //    float value;
  //    if (ss >> value) return value;
  //    throw(failure(<message about [section]name not an integer>));

  TextSeq sections()
    throw(failure);
  // Returns all the sections in the configuration file.  If the
  // configuration file cannot be successfully read and parsed,
  // 'failure' is thrown.

  TextSeq section_vars(const Text &section)
    throw(failure);
  // Returns all the names in the given section in the configuration
  // file.  If the specified section doesn't exist, the empty sequence
  // is returned.  If the configuration file cannot be successfully
  // read and parsed, 'failure' is thrown.

  SettingLocationSeq get_setting_locations(const Text &section, const Text &name)
    throw(failure);
  // Returns all the locations where a particular setting was assigned
  // a value.  "record_setting_locations" must be called to enable
  // this function.  If "record_setting_locations" was not called
  // before the config file was parsed, this function will throw an
  // exception.

  IncludeTree *get_include_hierarchy()
    throw(failure);
  // Returns the config file include hierarchy recorded during
  // parsing.  "record_include_hierarchy" must be called to enable
  // this unction.  If "record_include_hierarchy" was not called
  // before the config file was parsed, this function will throw an
  // exception.
}

#endif  /* _VestaConfig_H */
