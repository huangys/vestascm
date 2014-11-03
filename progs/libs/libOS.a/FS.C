// Copyright (C) 2001, Compaq Computer Corporation

// This file is part of Vesta.

// Vesta is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// Vesta is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with Vesta; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include <errno.h>    // defines ENOENT error code
#include <fcntl.h>    // defines O_RDONLY, O_WRONLY, etc.
#include <string.h>   // for strerror(3)
#include <sys/stat.h> // for stat(2)
#include <unistd.h>   // for unlink(2), rmdir(2), etc.
#include <limits.h>
#include <Basics.H>
#include <BufStream.H>
#include "FS.H"

using std::ios;
using Basics::OBufStream;

static void PrintBanner(std::ostream &os, char c, int num) throw ()
{
    os << '\n';
    for (int i = 0; i < num; i++) os << c;
    os << '\n';
}

std::ostream& operator<<(std::ostream &os, const FS::Failure &f) throw ()
{
    PrintBanner(os, '*', 60);
    os << "FS::Failure:\n";
    os << "  opName = " << f.get_op() << "\n";
    os << "  arg(s) = " << f.get_arg() << "\n";
    os << "  errno  = " << f.get_errno();
    Text errmsg = Basics::errno_Text(f.get_errno());
    if (!errmsg.Empty()) os << " (" << errmsg << ")";
    PrintBanner(os, '*', 60);
    os.flush();
    return os;
}

std::ostream& operator<<(std::ostream &os, const FS::EndOfFile &f) throw ()
{
    PrintBanner(os, '*', 60);
    os << "FS::EndOfFile:\n";
    os << "  bytes requested = " << f.bytes_requested() << "\n";
    os << "  bytes read      = " << f.bytes_read() << "\n";
    os << "  final position  = " << f.final_position();
    PrintBanner(os, '*', 60);
    os.flush();
    return os;
}

void FS::OpenReadOnly(const Text &fname, /*OUT*/ std::ifstream &ifs)
  throw (FS::DoesNotExist, FS::Failure)
/* REQUIRES Unopened(ifs) */
{
  int errno_save;
  while(1)
    {
      ifs.open(fname.chars(), ios::in);
      errno_save = errno;

      // Exit the loop if the file was opened successfully, or if the
      // error wasn't EINTR.
      if(ifs.good() || (errno_save != EINTR))
	break;

      // Clear the stream state before we try again, as open may not
      // reset it to good for us.
      ifs.clear(ios::goodbit);
    }

  if(!ifs.good())
    {
      // The file could not be opened for reading; in this case, "errno"
      // is ENOENT if the file didn't exist (see open(2)).
      if(errno_save == ENOENT)
	throw FS::DoesNotExist();
      else
	throw FS::Failure(Text("FS::OpenReadOnly"), fname, errno_save);
    }
  assert(ifs.good());
}

void FS::OpenForWriting(const Text &fname, /*OUT*/ std::ofstream &ofs)
  throw (FS::Failure)
/* REQUIRES Unopened(ofs) */
{
  int errno_save;
  // Normal case: just use the ofstream open method.
  while(1)
    {
      ofs.open(fname.chars(), ios::out|ios::trunc);
      errno_save = errno;

      // Exit the loop if the file was opened successfully, or if
      // the error wasn't EINTR.
      if(ofs.good() || (errno_save != EINTR))
	break;

      // Clear the stream state before we try again, as open may
      // not reset it to good for us.
      ofs.clear(ios::goodbit);
    }
	  
  if(!ofs.good())
    {
      throw FS::Failure(Text("FS::OpenForWriting [ofstream::open]"),
			fname, errno_save);
    }
}

void FS::Seek(std::istream &ifs, std::streamoff offset, ios::seekdir orig)
  throw (FS::Failure)
{
  if(!(ifs.seekg(offset, orig)))
    {
      int errno_save = errno;
      OBufStream buff;
      buff << "offset = " << offset << ", orig = " << orig;
      throw FS::Failure(Text("FS::Seek(std::istream) [istream::seekg]"), Text(buff.str()),
			errno_save);
    }

  // In some (broken?) implementations, ifs.sync() is needed to make
  // subsequent stream operations correct.  (See OSF_QAR #73434 filed
  // by Allan Heydon on 7/21/99.)

  if(ifs.sync() == EOF)
    {
      int errno_save = errno;
      OBufStream buff;
      buff << "offset = " << offset << ", orig = " << orig;
      throw FS::Failure(Text("FS::Seek(std::istream) [istream::sync]"), Text(buff.str()),
			errno_save);
    }
}

void FS::Seek(std::ostream &ofs, std::streamoff offset, ios::seekdir orig)
  throw (FS::Failure)
{
    if (!(ofs.seekp(offset, orig))) {
      int errno_save = errno;
      OBufStream buff;
      buff << "offset = " << offset << ", orig = " << orig;
      throw FS::Failure(Text("FS::Seek(std::ostream)"), Text(buff.str()),
			errno_save);
    }
}

std::streampos FS::Posn(std::istream &ifs) throw (FS::Failure)
{
    std::streampos res = ifs.tellg();
    if (!ifs) throw FS::Failure(Text("FS::Posn(std::istream)"));
    return res;
}

std::streampos FS::Posn(std::ostream &ofs) throw (FS::Failure)
{
    std::streampos res = ofs.tellp();
    if (!ofs) throw FS::Failure(Text("FS::Posn(std::ostream)"));
    return res;
}

void FS::Read(std::istream &ifs, char *buff, int n)
  throw (FS::EndOfFile, FS::Failure)
{
    if (!(ifs.read(buff, n))) {
      int errno_save = errno;
      int n_read = ifs.gcount();
      if (n_read < n)
	{
	  off_t file_pos = ifs.tellg();
	  throw FS::EndOfFile(n, n_read, file_pos);
	}
      else {
	OBufStream buff;
	buff << "count = " << n;
	throw FS::Failure(Text("FS::Read"), Text(buff.str()),
			  errno_save);
      }
    }
}

void FS::Write(std::ostream &ofs, char *buff, int n) throw (FS::Failure)
{
    if (!(ofs.write(buff, n))) {
      int errno_save = errno;
      OBufStream buff;
      buff << "count = " << n;
      throw FS::Failure(Text("FS::Write"), Text(buff.str()),
			errno_save);
    }
}

void FS::Copy(std::istream &ifs, std::ostream &ofs, int len)
  throw (FS::EndOfFile, FS::Failure)
{
    const int BuffSz = 1024;
    char buff[BuffSz];
    while (len > 0) {
	int toCopy = min(len, BuffSz);
	FS::Read(ifs, buff, toCopy);
	FS::Write(ofs, buff, toCopy);
	len -= toCopy;
    }
}

bool FS::AtEOF(std::istream &is) throw ()
{
    if (is.eof()) return true;
    if (is.rdstate() != 0) assert(false);
    return (is.peek() == EOF);
}

void FS::Flush(std::ostream &ofs) throw (FS::Failure)
{
  // Check for errors both before and after the flush.  Note that in
  // this case we use good(), which will include the EOF bit, as some
  // C++ runtime libraries use that bit to represent failed writes
  // when the disk is full.
  if(!ofs.good())
     throw (FS::Failure(Text("FS::Flush(std::ostream) [on entry]")));
  ofs.flush();
  if(!ofs.good())
     throw (FS::Failure(Text("FS::Flush(std::ostream) [after flush]")));
}

void FS::Close(std::ifstream &ifs) throw (FS::Failure)
{
  // Some C++ runtime libraries will clear error bits after closing a
  // stream, so we check for errors before and after closing.  Note
  // that for input streams, we only test badbit, because eofbit and (at
  // least on some libraries) failbit will be set in the normal case of
  // failing to read beyond the end of file.
  if(ifs.bad())
    throw (FS::Failure(Text("FS::Close(std::ifstream) [on entry]")));
  ifs.close();
  if (ifs.bad())
    throw (FS::Failure(Text("FS::Close(std::ifstream) [after close]")));
}

void FS::Close(std::ofstream &ofs) throw (FS::Failure)
{
  // Some C++ runtime libraries will clear error bits after closing a
  // stream, and there could be buffered writes pending, so we check
  // for problems at three points: on entry, after flushing any
  // pending writes, and after closing the stream.  Note that in this
  // case we use good(), which will include the EOF bit, as some C++
  // runtime libraries use that bit to represent failed writes when
  // the disk is full.
  if(!ofs.good())
     throw (FS::Failure(Text("FS::Close(std::ofstream) [on entry]")));
  ofs.flush();
  if(!ofs.good())
     throw (FS::Failure(Text("FS::Close(std::ofstream) [after flush]")));
  ofs.close();
  if(!ofs.good())
     throw (FS::Failure(Text("FS::Close(std::ofstream) [after close]")));
}

void FS::Close(std::fstream &fs) throw (FS::Failure)
{
  // Some C++ runtime libraries will clear error bits after closing a
  // stream, and there could be buffered writes pending.  However, in
  // the case of an std::fstream, it's not clear what the correct
  // behavior should be, since it could represent either or both input
  // and ouptput.

  // As with std::ifstream, we use the bad() operator, which ignores eofbit
  // and failbit, because this could be an input file stream that has
  // reached the end of file.  However, this could miss some real
  // error cases, as some C++ runtime libraries eofbit to represent
  // failed writes when the disk is full.

  if(fs.bad())
    throw (FS::Failure(Text("FS::Close(std::fstream) [on entry]")));
  // We also flush pending writes before closing, in case those write
  // fail.  (This should be safe for input streams.)
  fs.flush();
  if(fs.bad())
     throw (FS::Failure(Text("FS::Close(std::fstream) [after flush]")));
  fs.close();
  if (fs.bad())
    throw (FS::Failure(Text("FS::Close(std::fstream) [after close]")));
}

void FS::Delete(const Text &fname) throw (FS::Failure)
{
  bool fail;
  do
    {
      fail = (unlink(fname.cchars()) != 0);
    }
  while(fail && (errno == EINTR));

  if(fail)
    {
      throw (FS::Failure(Text("FS::Delete"), fname));
    }
}

bool FS::DeleteDir(const Text &dirname) throw (FS::Failure)
{
  bool fail;
  // According to the Single UNIX Speciciation, rmdir(2) should never
  // fail with EINTR and yet unlink(2) is allowed to fail with EINTR.
  // It therefore seems prudent to handle it here.
  do
    { 
      fail = (rmdir(dirname.cchars()) < 0);
    }
  while(fail && (errno == EINTR));

  // For "directory not empty" some platforms use EEXIST and some use
  // ENOTEMPTY.  We just accept either.
  if (fail && (errno != EEXIST) && (errno != ENOTEMPTY))
    {
      throw (FS::Failure(Text("FS::DeleteDir"), dirname));
    }

  return !fail;
}

bool FS::Exists(const Text &fname) throw ()
{
  struct stat l_stat_buf;
  // Note: stat(2) is not allowed to fail with EINTR.
  bool fail = (stat(fname.cchars(), &l_stat_buf) != 0);
  return !fail;
}

bool FS::IsFile(const Text &fname) throw ()
{
  struct stat l_stat_buf;
  // Note: stat(2) is not allowed to fail with EINTR.
  bool fail = (stat(fname.cchars(), &l_stat_buf) != 0);
  return (!fail && S_ISREG(l_stat_buf.st_mode));
}

bool FS::IsDirectory(const Text &fname) throw ()
{
  struct stat l_stat_buf;
  // Note: stat(2) is not allowed to fail with EINTR.
  bool fail = (stat(fname.cchars(), &l_stat_buf) != 0);
  return (!fail && S_ISDIR(l_stat_buf.st_mode));
}

bool FS::IsSymlink(const Text &fname) throw ()
{
  struct stat l_stat_buf;
  // Note: lstat(2) is not allowed to fail with EINTR.
  bool fail = (lstat(fname.cchars(), &l_stat_buf) != 0);
  return (!fail && S_ISLNK(l_stat_buf.st_mode));
}

Text FS::Realpath(const Text &fname, bool must_exist)
  throw (FS::DoesNotExist, FS::Failure)
{
  // deal with any non-existant ending path components
  Text missing("");
  Text arg = fname;
  if(! FS::Exists(arg)) {
    if(must_exist) {
      throw FS::DoesNotExist();
    } 
    do {
      Text arc;
      FS::SplitPath(arg, arc, arg);
      if(missing.Empty()) {
	missing = arc;
      } else {
	missing = Text::printf("%s/%s", arc.cchars(), missing.cchars());
      }
    } while (! FS::Exists(arg));
  }

  // save the cwd
  Text cwd = FS::getcwd();

  // figure out if fname is a directory (or a symlink
  // to a directory).  If not, deal with the case
  // where it's a symlink to a file, and then
  // get the enclosing directory.
  Text dir_name;
  Text end;
  if(FS::IsDirectory(arg)) {
    dir_name = arg;
    end = "";
  } else if(FS::IsSymlink(arg)) {
    Text target = arg;
    if(target[0] != '/') {
      target = Text::printf("%s/%s", cwd.cchars(), target.cchars());
    }
    do {
      Text link = target;
      target = FS::readlink(link);
      if(target[0] != '/') {
	Text link_dir;
        FS::SplitPath(link, end, link_dir);
	target = Text::printf("%s/%s", link_dir.cchars(), target.cchars());
      }
    } while(FS::IsSymlink(target));
    FS::SplitPath(target, end, dir_name);
  } else {
    FS::SplitPath(arg, end, dir_name);
  }
  // after spliting the path of something that does
  // exist, the parent must be a directory.
  assert(FS::IsDirectory(dir_name));

  // change to dir_name and getcwd() it.  That should
  // implicitly resolve all symbolic links and return
  // the direct filesystem absolute path to dir_name.
  if(chdir(dir_name.cchars())) {
    //FIXME: which errno cases should we handle explicitly?
    throw (FS::Failure(Text("FS::Realpath"),
		       Text::printf("changing to %s", dir_name.cchars())));
  }
  Text real_name = FS::getcwd();

  // add back in the file name if fname wasn't a directory
  if(! end.Empty()) {
    real_name = Text::printf("%s/%s", real_name.cchars(), end.cchars());
  }

  // make sure we didn't mess anything up
  assert(FS::Exists(real_name));

  // return to our original cwd
  if(chdir(cwd.cchars())) {
    //FIXME: again, handle any specific errno's?
    throw (FS::Failure(Text("FS::Realpath"),
		       Text::printf("returning to %s", cwd.cchars())));
  }

  // re-attach any non-existant portions of the path
  if(! missing.Empty()) {
    real_name = Text::printf("%s/%s", real_name.cchars(), missing.cchars());
  }
  return real_name;
}

void FS::Touch(const Text &fname, mode_t mode, bool allowOverwrite)
  throw (FS::AlreadyExists, FS::Failure)
{
  int oflag = O_CREAT|O_WRONLY;
  oflag |= allowOverwrite ? O_TRUNC : O_EXCL;
  int fd;
  int errno_save;
  do
    {
      fd = ::open(fname.cchars(), oflag, mode);
      errno_save = errno;
    }
  // Exit the loop if the file was opened successfully, or if the
  // error wasn't EINTR.
  while((fd < 0) && (errno == EINTR));
  if (fd < 0) {
    if (errno_save == EEXIST) {
      assert(!allowOverwrite);
      throw FS::AlreadyExists();
    } else {
      throw FS::Failure(Text("FS::Touch [open(2)]"), fname, errno_save);
    }
  }
  int close_res;
  do
    close_res = ::close(fd);
  while((close_res != 0) && (errno == EINTR));
  if(close_res != 0)
    throw FS::Failure(Text("FS::Touch [close(2)]"), fname);
}


Text FS::ParentDir(const Text &fname) throw (FS::Failure)
{
  Text name, parent;
  SplitPath(fname, name, parent);
  return parent;
}


Text FS::GetAbsolutePath(const Text &name) throw(FS::Failure)
{
  Text apath = name;
  if(name.Empty() || name[0] != '/') {
    Text cwd = FS::getcwd();
    if (name.Empty()) {
      apath = cwd;
    } 
    else {
      apath = cwd + "/" + name;
    }
  }
  return apath;
}

Text FS::RemoveSpecialArcs(const Text &name) throw()
{
  int len = name.Length();
  int start = 1;
  Text cname = "";
  while (start < len) {
    int slash = name.FindChar('/', start);
    if (slash == -1) 
      slash = len;
    Text comp = name.Sub(start, slash - start);
    if (comp == "." || comp == "") {
      // skip
    } 
    else if (comp == "..") {
      // delete previous component
      int prev = cname.FindCharR('/');
      if(prev > -1)
	cname = cname.Sub(0, prev);
      else
	cname = "";
    } 
    else {
      // keep
      cname = cname + "/" + comp;
    }
    start = slash + 1;
  }
  if(cname == "")
    cname = "/";
  return cname;
}

void FS::SplitPath(const Text &fname, 
		   /*OUT*/ Text &name, /*OUT*/ Text &parent) throw(FS::Failure)
{
  Text apath = GetAbsolutePath(fname);
  name = RemoveSpecialArcs(apath);

  int last_slash_pos = name.FindCharR(PathnameSep);

  while(last_slash_pos != -1) {
    if(last_slash_pos + 1 < name.Length()) {
      // get everything after last slash for the name and 
      // before the last slash for the path
      if(last_slash_pos == 0)
	parent = "/";
      else
	parent = name.Sub(0, last_slash_pos);
      name = name.Sub(last_slash_pos + 1);  
      break;
    }
    else {
      // there is nothing after last slash.
      if(last_slash_pos) {
	// get path till the last slash and work on it.
	name = name.Sub(0, last_slash_pos);
	last_slash_pos = name.FindCharR(PathnameSep);
      }
      else { // fname is something like one slash ore more.
	name = "/";
	parent = "/";
	break;
      }
    }
  }
}

Text FS::TempFname(Text prefix, Text (*suffix_func)(), mode_t mode)
  throw (FS::Failure)
{
  int oflag = O_CREAT|O_WRONLY|O_EXCL;
  Text fname;
  int fd;
  int errno_save;
  while(1)
    {
      // Generate a candidate name;
      fname = prefix+suffix_func();
      fd = ::open(fname.cchars(), oflag, mode);
      errno_save = errno;
      // If this filename already exists, just try again with a new
      // name.
      if((fd == -1) && (errno_save == EEXIST))
	continue;
      // If we succeeded in creating the file, or if there was an
      // error other than EINTR, exit the loop.
      else if((fd != -1) || (errno_save != EINTR))
	break;
    }
  // If we encountered an error while trying to create the file, throw
  // an exception.
  if (fd < 0)
    {
      assert(errno_save != EEXIST);
      throw FS::Failure(Text("FS::TempFname [open(2)]"), fname, errno_save);
    }
  // Close the file descriptor we opened.
  int close_res;
  do
    close_res = ::close(fd);
  while((close_res != 0) && (errno == EINTR));
  if(close_res != 0)
    throw FS::Failure(Text("FS::TempFname [close(2)]"), fname);
  // Return the name of the temporary file we created.
  return fname;
}

Text FS::MakeTempDir(Text prefix, Text (*suffix_func)(), mode_t mode)
  throw(FS::Failure)
{
  Text dname;
  int errno_save;
  while(1)
    {
      dname = prefix+suffix_func();
      int err = mkdir(dname.cchars(), mode);
      errno_save = errno;
      if(err == 0)
	{
	  // We uscceeded in creating the directory: we're done
	  break;
	}
      else if(errno_save != EEXIST)
	{
	  // Some error other than the name already existing: throw an
	  // exception

	  throw FS::Failure(Text("FS::MakeTempDir [mkdir(2)]"), dname, errno_save);
	}
    }
  return dname;
}

Text FS::CommonParent(const Text &fname1, const Text &fname2, 
		      /*OUT*/Text* rest1, /*OUT*/Text* rest2)
{
  // the pathnames should be in absolute path format
  assert(fname1[0] == '/');
  assert(fname2[0] == '/');

  Text common = "";

  unsigned int last_sep = 0;
  unsigned int i = 0;
  while(fname1[i]  != 0 && fname2[i] != 0 && fname1[i] == fname2[i]) {
    if(fname1[i] == '/')
      last_sep = i;
    i++;
  }

  if(((fname1[i] == 0) && (fname2[i] == '/')) ||
     ((fname1[i] == '/') && (fname2[i] == 0)) ||
     ((fname1[i] == 0) && (fname2[i] == 0))) {
    // Treat the end of the string as equivalent to a pathname
    // separator.
    last_sep = i;
  }

  if(rest1 != 0) {
    if(last_sep == 0)
      *rest1 = fname1;
    else if(last_sep + 1 >= fname1.Length())
      *rest1 = "";
    else
      *rest1 = fname1.Sub(last_sep+1);
  }
  if(rest2 != 0) {
    if(last_sep == 0)
      *rest2 = fname2;
    else if(last_sep + 1 >= fname2.Length())
      *rest2 = "";
    else
      *rest2 = fname2.Sub(last_sep+1);
  }
  return fname1.Sub(0, last_sep);
}

Text FS::GetFirstArc(const Text& name, /*OUT*/Text* rest)
{
  Text first_arc;
  if(!name.Empty()) {
    // remove last slash
    int end_slash = name.Length();
    if(name[end_slash - 1] == '/') {
      end_slash--;
    }
    // skip first slash
    int start = 0;
    if(name[start] == '/') {
      start++;
    }
    // find first slash after the one in the beggining
    int slash = start;
    while(slash < end_slash) {
      if(name[slash] == '/')
	break;
      slash++;
    }

    first_arc = name.Sub(start, slash - start);
    if(rest != 0) {
      if(slash == end_slash) {
	*rest = "";
      }
      else {
	slash++;
	*rest = name.Sub(slash, end_slash - slash);
      }
    }
  } // if name is not empty
  return first_arc;
}

Text FS::getcwd() throw (FS::Failure)
{
  unsigned int size = PATH_MAX+1;
  Text result;
  while(1)
    {
      {
	char *cwdbuf = NEW_PTRFREE_ARRAY(char, size);
	char *cwd = ::getcwd(cwdbuf, size);
	if(cwd != 0)
	  {
	    result = cwd;
	    delete [] cwdbuf;
	    break;
	  }
	else if(errno != ERANGE)
	  {
	    delete [] cwdbuf;
	    throw FS::Failure(Text("FS::getcwd"),
			      "", errno);
	  }
	delete [] cwdbuf;
      }
      // We only get here if getcwd fails with ERANGE, which means the
      // buffer was too small.
      size += PATH_MAX;
    }
  return result;
}

Text FS::readlink(const Text &fname)
  throw (FS::Failure, FS::DoesNotExist)
{
  unsigned int size = PATH_MAX+1;
  Text result;
  while(1)
    {
      {
	char *targetbuf = NEW_PTRFREE_ARRAY(char, size);
	ssize_t ret = ::readlink(fname.cchars(), targetbuf, size);
	if(ret >= 0 && ret < size)
	  {
	    targetbuf[ret] = '\0';
	    result = targetbuf;
	    delete [] targetbuf;
	    break;
	  }
	else if(ret < 0)
	  {
	    delete [] targetbuf;
	    if(errno == ENOENT)
	      {
		throw FS::DoesNotExist();
	      }
	    else
	      {
		throw FS::Failure(Text("FS::readlink"),
			      fname, errno);
	      }
	  }
	delete [] targetbuf;
      }
      // We only get here if readlink returns size, which means the
      // buffer was too small.
      size += PATH_MAX;
    }
  return result;
}
