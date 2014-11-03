// Copyright (C) 2008, Vesta Free Software Project
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

// ----------------------------------------------------------------------

// Basics::IndentHelper is a simple class to make multi-level
// indentation in text output easier.

#ifndef _INDENTHELPER_H
#define _INDENTHELPER_H

#include <Basics.H>

namespace Basics
{
  class IndentHelper
  {
  private:
    // Current indentation level
    unsigned int level;
    // The text string to use for one level of indentation
    Text space;
  public:
    IndentHelper(unsigned int l = 0, const Text &s = " ") throw()
      : level(l), space(s)
    {
    }

    IndentHelper(const IndentHelper &other) throw()
      : level(other.level), space(other.space)
    {
    }

    inline IndentHelper &operator=(const IndentHelper &other) throw()
    {
      level = other.level;
      space = other.space;
      return *this;
    }

    // The += and -= operators increase and decrease indent by the
    // specified level.
    inline unsigned int operator+=(unsigned int more) throw()
    {
      level += more;
      return level;
    }
    inline unsigned int operator-=(unsigned int less) throw()
    {
      assert(level >= less);
      level -= less;
      return level;
    }

    // The ++ and -- operators increase and decrease indent by one
    // level.
    inline IndentHelper &operator++() throw()
    {
      level++;
      return *this;
    }
    inline IndentHelper operator++(int) throw()
    {
      IndentHelper result(*this);
      level++;
      return result;
    }
    IndentHelper &operator--() throw()
    {
      assert(level > 0);
      level--;
      return *this;
    }
    inline IndentHelper operator--(int) throw()
    {
      IndentHelper result(*this);
      assert(level > 0);
      level--;
      return result;
    }

    // Cast to an unsigned int to retrieve the indentation level
    inline operator unsigned int() const throw()
    {
      return level;
    }

    // The real purpose of this class: print out indentation to a
    // stream.
    inline void print(std::ostream &out) const throw()
    {
      for(unsigned int i = 0; i < level; i++)
	out << space;
    }
  };
}

inline std::ostream &operator<<(std::ostream &out, const Basics::IndentHelper &indent)
{
  indent.print(out);
  return out;
}

#endif
