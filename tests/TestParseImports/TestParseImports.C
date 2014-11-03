#include <iostream>
#include <Basics.H>
#include <ParseImports.H>

using std::cout;
using std::cerr;
using std::endl;
using std::ifstream;

Text program_name;

Text dump_import_id(ImportID id)
{
  Text ret;
  ImportID::Iterator iter = id.begin();
  while(iter != id.end()) {
    if(ret.Empty())
      ret = *iter;
    else
      ret = ret + "/" + *iter;
    iter++;
  }
  return ret;
}

unsigned int dump_imports(const ImportSeq &imports, const Text &model_path)
{
  unsigned int result = 0;
  ifstream model(model_path.cchars());
  for(unsigned int i = 0; i < imports.size(); i++)
    {
      const Import *imp = imports.get(i);
      cout << "  [" << i << "]:" << endl
	   << "    path       = " << imp->path << endl
	   << "    orig       = " << imp->orig << endl
	   << "    origOffset = " << imp->origOffset << endl
	   << "    id         = " << dump_import_id(imp->id) << endl
	   << "    start      = " << imp->start << endl
	   << "    end        = " << imp->end << endl
	   << "    local      = " << (imp->local?"true":"false") << endl
	   << "    fromForm   = " << (imp->fromForm?"true":"false") << endl
	   << "    noUpdate   = " << (imp->noUpdate?"true":"false") << endl;

      if(imp->end < imp->start)
	{
	  cout << "  ERROR: end < start" << endl;
	  result++;
	}
      else if(imp->orig.Length() != (imp->end - imp->start))
	{
	  cout << "  ERROR: orig length = " << imp->orig.Length()
	       << ", end-start = " << (imp->end - imp->start) << endl;
	  result++;
	}
      else
	{
	  try
	    {
	      unsigned long length = (imp->end - imp->start);
	      FS::Seek(model, imp->start);
	      char *buf = NEW_PTRFREE_ARRAY(char, length+1);
	      FS::Read(model, buf, length);
	      buf[length] = 0;
	      if(imp->orig != buf)
		{
		  cout << "  ERROR: orig read from file doesn't match: \""
		       << buf << "\"" << endl;
		  result++;
		}
	    }
	  catch(const FS::EndOfFile &f)
	    {
	      cout << "  ERROR: Unexpected end-of-file re-reading orig:" << endl
		   << f;
	      result++;
	    }
	  catch(const FS::Failure &f)
	    {
	      cout << "  ERROR: Error re-reading orig:" << endl
		   << f;
	      result++;
	    }
	}
      cout << endl;
    }
  return result;
}

int main(int argc, char **argv)
{
  program_name = argv[0];

  // get the working directory
  char wd_buff[PATH_MAX+1];
  char *res = getcwd(wd_buff, PATH_MAX+1); assert(res != (char *)NULL);
  res = strcat(wd_buff, "/"); assert(res != (char *)NULL);
  Text wd(wd_buff);

  unsigned int err_count = 0;

  try
    {
      for(unsigned int i = 1; i < argc; i++)
	{
	  Text model = ParseImports::ResolvePath(argv[i], wd);
	  try
	    {
	      ImportSeq imports(/*sizehint=*/ 10);
	      ParseImports::P(model, /*INOUT*/ imports);
	      cout << model << ":" << endl;
	      err_count += dump_imports(imports, model);
	    }
	  catch (FS::DoesNotExist)
	    {
	      cerr << endl << program_name << ": model file "
		   << model << " does not exist" << endl;
	      exit(1);
	    }
	}
    }
  catch (const FS::Failure &f)
    {
      cerr << endl << program_name << ": " << f << endl;
      exit(1);
    }
  catch (const ParseImports::Error &err)
    {
      cerr << endl << program_name  << ": " << err << endl;
      exit(1);
    }
  catch (const SRPC::failure &f)
    {
      cerr << program_name
	   << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
      exit(1);
    }

  return (err_count > 0) ? 2 : 0;
}
