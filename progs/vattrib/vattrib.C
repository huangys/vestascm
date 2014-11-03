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

//
// vattrib.C
//
// Read or modify attributes of a source in the Vesta repository.
// See vattrib.1.mtex for documentation.
//

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>
#include <VestaSource.H>
#include <VDirSurrogate.H>
#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif
#include "ReposUI.H"

#include <iomanip>

using std::cout;
using std::cerr;
using std::endl;
using std::setw;
Text program_name;
bool multiple;
Text timefmt;

void
Usage()
{
    cerr << "Usage: " << program_name <<
      " [-q] [-t timestamp] [-m] [-T] [-L] [-R repos] [-M] [-H hints]" 
	 << endl <<
      "        { -x |" << endl <<
      "          -i attrib value |" << endl <<
      "          -g attrib |" << endl <<
      "          -G attrib |" << endl <<
      "          -n | [-l] | -h |" << endl <<
      "          -s attrib value |" << endl <<
      "          -c attrib |" << endl <<
      "          -a attrib value |" << endl <<
      "          -r attrib value |" << endl <<
      "          -C value }" << endl <<
      "        [source...]" << endl << endl;
    exit(3);
}

bool
coutValueCallback(void* closure, const char* value)
{
  if(closure != 0)
    {
      Text *indent = (Text *) closure;
      cout << *indent;
    }
  cout << value << endl;
  return true;
}

struct listAllClosure {
  VestaSource* sourceVS;
  Text indent;
};

bool
listAllCallback(void* closure, const char* value)
{
  listAllClosure* cl = (listAllClosure*) closure;
  cout << cl->indent << value << endl;
  Text indent = cl->indent + "\t";
  cl->sourceVS->getAttrib(value, coutValueCallback, (void *) &indent);
  return true;
}

bool
coutHistoryCallback(void* closure, VestaSource::attribOp op,
		    const char* name, const char* value,
		    time_t timestamp)
{
    char timebuf[256];
    strftime(timebuf, sizeof(timebuf), timefmt.cchars(), localtime(&timestamp));
    const char *op_string = VestaSource::attribOpString(op);
    // chop off the "op" from the start of the string
    if((op_string[0] == 'o') && (op_string[1] == 'p'))
      {
	op_string += 2;
      }
    cout  << ' ' << timebuf << ' '
      << setw(6) << op_string << ' '
      << name << '\t' << value << endl;
    return true;
}


// In order to allow vattrib on multiple sources we parse command line first, 
// remembering each source and it's flags into Action structure, 
// where each Action contains pointer to the next one. If there is Usage
// problem vattrib terminates parsing process. At the end of parsing
// process all sources are found and we have a list of Actions to perform. 
// If there is Usage problem vattrib terminates the parsing process. 
// Main while loop performs each Action from the list.


typedef struct ActionFlags {
  int quiet_flag;
  int exist_flag;
  int in_flag;
  int get_flag;
  int getone_flag;
  int list_flag;
  int listall_flag;
  int history_flag;
  int set_flag;
  int clear_flag;
  int add_flag;
  int remove_flag;
  int type_flag;
  int master_flag;
  int compare_flag;
  int last_modified_flag; 
} ActionFlags;


typedef struct Action {
  ActionFlags flags;
  time_t timestamp;
  Text attrib;
  Text value;
  Text csource;
  VestaSource* sourceVS;  

  Action();
  ~Action() {}
} Action; 

Action::Action()
  : timestamp(0), sourceVS(0)
{
  memset(&flags, 0, sizeof(ActionFlags));
}

void
commandLineParsing(int argc, char* argv[], Sequence<Action*>& list, 
		   Text& lhost, Text& lport) throw()
{
  int new_action_ind = 1;
  multiple = false;
  
  bool done = false;
  if((optind < argc) && (argv[optind][0] != '-'))
    done = true;

  Text repos, hints;
  bool at_master = false;
  Action* current = NEW(Action);

  // Get default hints
  Text defhints;
  (void) VestaConfig::get("UserInterface", "DefaultHints", defhints);
 
  while(new_action_ind) {
    int c;
    new_action_ind = 0;
   
    while(!done) {
      int args = 0;

      c = getopt(argc, argv, "qt:xigGnlhscarCTLmR:MH:");
      if(c == EOF) break;
      switch (c) {
      case 'q': current->flags.quiet_flag++; continue;
      case 't':
        current->timestamp = strtol(optarg, NULL, 0);
        continue;

      case 'x': current->flags.exist_flag++; break;
      case 'i': current->flags.in_flag++; args = 2; break;
      case 'g':	current->flags.get_flag++; args = 1; break;
      case 'G': current->flags.getone_flag++; args = 1; break;
      case 'n': current->flags.list_flag++; break;
      case 'l': current->flags.listall_flag++; break;
      case 'h': current->flags.history_flag++; break;
      case 's': current->flags.set_flag++; args = 2; break;
      case 'c': current->flags.clear_flag++; args = 1; break;
      case 'a': current->flags.add_flag++; args = 2; break;
      case 'r': current->flags.remove_flag++; args = 2;	break;
      case 'C': current->flags.compare_flag++; args = 1; break;

      case 'T': current->flags.type_flag++; break;
      case 'm':	current->flags.master_flag++; break;
      case 'L': current->flags.last_modified_flag++; break;
      case 'R':	repos = optarg; break;
      case 'M': at_master = true; break;
      case 'H':	hints = optarg; break;

      case '?':
      default:
	 Usage();
      } //switch

      // get attrib and value parameters of the current action 
      if(current->attrib == "" && current->value == ""){
	switch(args) {
	case 2:
	  if(optind + 2 > argc)
	    Usage();
	  current->attrib = argv[optind++];
	  current->value = argv[optind++];
	  break;
	case 1:
	  if(optind + 1 > argc)
	    Usage();
	  current->attrib = argv[optind];
	  current->value = argv[optind];
	  optind++;
	  break;
	default: break;
	} 
      }
     
      // check if all flags for current source were read
      if((optind >= argc) || ((optind < argc) && (argv[optind][0] != '-'))) {
	done = true; 
      }

    }//while
    done = false;

    // get current source and the first index of next action
    Text source;
    if(optind < argc){
      source = argv[optind];
      if(optind + 1 < argc) {
	if(!multiple)
	  multiple = true;
	new_action_ind = optind + 1;
      }
      else
	new_action_ind = 0;
    }
    else
      source = ".";

    // validate flags
    // check that -q -m is not used with -T or/and -L flag
    if(current->flags.master_flag && current->flags.quiet_flag &&
       (current->flags.last_modified_flag || current->flags.type_flag)) {
      cerr << program_name 
	   << ": flags -q -m can not be used with other flags" << endl;
      Usage();
    }

    if(current->flags.quiet_flag && 
       !(current->flags.exist_flag || current->flags.in_flag || 
	 current->flags.getone_flag || current->flags.compare_flag ||
	 current->flags.master_flag)) {
      cerr << program_name
	   << ": -q flag is meaningful only with -m, -x, -i, -G or -C"
	   << endl;
      Usage();
    }

    switch (current->flags.exist_flag + current->flags.in_flag + 
	    current->flags.get_flag + current->flags.getone_flag + 
	    current->flags.list_flag + current->flags.listall_flag +
	    current->flags.history_flag + current->flags.set_flag + 
	    current->flags.clear_flag + current->flags.add_flag + 
	    current->flags.remove_flag + current->flags.compare_flag){
      case 0:
        if(!current->flags.master_flag && !current->flags.type_flag &&
	   !current->flags.last_modified_flag){
	  current->flags.listall_flag++;
	  current->flags.type_flag++;
	  current->flags.master_flag++;
        }
        break;
      case 1:
	// -q -m should not be used with one of 
	// -x, -i, -g, -G, -n, -l, -h, -s, -c, -a, -r, -C  
	if(current->flags.master_flag && current->flags.quiet_flag) {
	  cerr << program_name 
	       << ": flags -q -m can not be used with other flags" << endl;
	  Usage();
	}
        break;
      default:
        cerr << program_name 
	     << ": flags -x, -i, -g, -G, -n, -l, -h, -s, -c, -a, -r, -C "
	     << endl << "are mutually exclusive" << endl;
        Usage();
    }

    if(current->timestamp != 0 && 
       !(current->flags.set_flag || current->flags.clear_flag || 
	 current->flags.add_flag || current->flags.remove_flag)) {
      cerr << program_name
	   << ": -t flag is meaningful only with -s, -c, -a, or -r"
	   << endl;
      Usage();
    }

    // validate flags for multiple call
    if(multiple) {
      if(current->flags.in_flag) {
	cerr << program_name
	     << ": -i flag cannot be used with multiple objects" << endl;
	Usage();
      }
      else if(current->flags.compare_flag){
	cerr << program_name
	     << ": -C flag cannot be used with multiple objects" << endl;
	Usage();
      }
      else if(current->flags.exist_flag) {
	cerr << program_name
	     << ": -x flag cannot be used with multiple objects" << endl;
	Usage();
      }
      else if(current->flags.master_flag && current->flags.quiet_flag) {
	cerr << program_name
	     << ": -q -m flag cannot be used with multiple objects" << endl;
	Usage();
      }
    }

    // get current VestaSource pointer
    Text host, port;
    if (repos != "") {
      int colon = repos.FindCharR(':');
      if (colon == -1) {
	host = repos;
	port = lport;
	repos = repos + ":" + lport;
      } else {
	host = repos.Sub(0, colon);
	port = repos.Sub(colon+1);
      }
      hints = hints + " " + repos;
    }
    else {
      host = lhost;
      port = lport;
    }
    hints = hints + " " + defhints;
    
    bool got_next_source; 
    do {
      got_next_source = false;
      try {
	current->csource = ReposUI::canonicalize(source);
	if(at_master) {
	  current->sourceVS = 
	    ReposUI::filenameToMasterVS(current->csource, hints);
	} else {
	  current->sourceVS = 
	    ReposUI::filenameToVS(current->csource, host, port);
	}
      }
      catch (ReposUI::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(2);
      }
      list.addhi(current);

      // create new Action for next source
      if(new_action_ind && new_action_ind < argc) {
	Action* new_act = NEW(Action);
	// the same action for next source
	if(argv[new_action_ind][0] != '-') {
	  new_act->flags = current->flags;
	  new_act->timestamp = current->timestamp;
	  new_act->attrib = current->attrib;
	  new_act->value = current->value;
	  source = argv[new_action_ind];
	  new_action_ind++;
	  got_next_source = true;
	}
	current = new_act;
      }
    } while(got_next_source);


    if(new_action_ind == argc)
      new_action_ind = 0;
  } //while

}



int
main(int argc, char* argv[])
{
  program_name = argv[0];

  Sequence<Action*> list; 

  try {
    timefmt = VestaConfig::get_Text("UserInterface", "TimeFormat");
    Text lhost(VDirSurrogate::defaultHost());
    Text lport(VDirSurrogate::defaultPort());
    Text lrepos = lhost + ":" + lport;
    commandLineParsing(argc, argv, list, lhost, lport);
    int actions_num = list.size();
    
    for(int i = 0; i < actions_num; i++) {
      Action* current = list.remlo();
      
      Text source = current->csource;
      Text repos = current->sourceVS->host() + ":" +  
	current->sourceVS->port();

      if(multiple && !current->flags.quiet_flag && 
	 (current->flags.master_flag || current->flags.type_flag ||
	  current->flags.last_modified_flag || current->flags.get_flag ||
	  current->flags.list_flag || current->flags.listall_flag || 
	  current->flags.history_flag || current->flags.getone_flag)) {
	cout << source.cchars() << " ";
	if(repos != lrepos)
	  cout << "(" << repos << ") "; 
	if(!current->flags.master_flag && !current->flags.type_flag)
	  cout << endl;
      }
      if(current->flags.master_flag) {
	if(current->sourceVS->master)
	  if(current->flags.quiet_flag) exit(0); 
	  else cout << "master";
	else
	  if(current->flags.quiet_flag) exit(1);
	  else cout << "nonmaster";
	if (current->flags.type_flag)
	  cout << " ";
	else
	  cout << endl;
      }

      if(current->flags.type_flag) {
	cout << VestaSource::typeTagString(current->sourceVS->type) 
	     << endl;
      }
      
      if (current->flags.last_modified_flag){
	time_t time = current->sourceVS->timestamp();
	char timebuf[256];
	switch(int(time)) {
	case 2:
	case -1:
	  // 2 => ghost, stub, or device
	  // -1 => missing shortid or stat failure in server

	  // See VLeaf::timestamp in repository server code.

	  strcpy(timebuf, "<no timestamp>");
	  break;
	default:
	  strftime(timebuf, sizeof(timebuf), timefmt.cchars(), 
		   localtime(&time));
	  break;
	}
	if(multiple)
	  cout << '\t';
	cout << timebuf << endl;
      }
      
      if(current->flags.exist_flag) {
	if(current->sourceVS->hasAttribs()) {
	  if(!current->flags.quiet_flag)
	    cout << "true" << endl;
	  exit(0);
	} else {
	  if (!current->flags.quiet_flag)
	    cout << "false" << endl;
	  exit(1);
	}	
      }	    

      bool require_attribs_flags = current->flags.in_flag ||
	current->flags.get_flag || current->flags.getone_flag ||
	current->flags.list_flag || current->flags.listall_flag ||
	current->flags.history_flag || current->flags.set_flag ||
	current->flags.clear_flag || current->flags.add_flag ||
	current->flags.remove_flag;

      if(require_attribs_flags && !current->sourceVS->hasAttribs()) {
	if(!current->flags.quiet_flag) {
	  cerr << program_name << ": " << source.cchars()
	       << " does not have attributes" << endl;
	}
	exit(2);
      }
      
      if (current->flags.in_flag) {
	if(current->sourceVS->inAttribs(current->attrib.cchars(), 
					current->value.cchars())){
	  if(!current->flags.quiet_flag)
	    cout << "true" << endl;
	  exit(0);
	} else {
	  if(!current->flags.quiet_flag)
	    cout << "false" << endl;
	  exit(1);
	}
      } 
      else if(current->flags.compare_flag) {
	const char* typestr = current->value.cchars();
	if(strcmp(VestaSource::typeTagString(current->sourceVS->type),
		  typestr) == 0) {
	  if(!current->flags.quiet_flag) 
	    cout << "true" << endl;
	  exit(0);
	} 
	else {
	  if(strcmp(typestr, "immutableFile") !=0 &&
	     strcmp(typestr, "mutableFile") !=0 &&
	     strcmp(typestr, "immutableDirectory") !=0 &&
	     strcmp(typestr, "appendableDirectory") !=0 &&
	     strcmp(typestr, "mutableDirectory") !=0 &&
	     strcmp(typestr, "ghost") !=0 &&
	     strcmp(typestr, "stub")!=0) {
	    cerr << program_name << ": " << typestr  
		 << ": invalid type string" << endl;
	    exit (2);
	  }
	  else {
	    if(!current->flags.quiet_flag)
	      cout << "false" << endl;
	    exit(1);
	  }
	}
      }
      else if(current->flags.get_flag) {
	Text indent = multiple ? "\t" : "";
	current->sourceVS->getAttrib(current->attrib.cchars(), 
				     coutValueCallback, (void *) &indent);
      } else if(current->flags.getone_flag) {
	char* val = current->sourceVS->getAttrib(current->attrib.cchars());
	if(val == NULL) {
	  if(!current->flags.quiet_flag) {
	    cerr << program_name
		 << ": no value for attribute " << current->attrib << endl;
	  }
	  exit(1);
	}
	if(multiple)
	  cout << '\t';
	cout << val << endl;
	delete val;
      } else if(current->flags.list_flag) {
	Text indent = multiple ? "\t" : "";
	current->sourceVS->listAttribs(coutValueCallback, (void *) &indent);
      } else if(current->flags.listall_flag) {
	listAllClosure cl;
	cl.sourceVS = current->sourceVS;
	cl.indent = multiple ? "\t" : "";
	current->sourceVS->listAttribs(listAllCallback, &cl);
      } else if(current->flags.history_flag) {
	current->sourceVS->getAttribHistory(coutHistoryCallback, NULL);
      } else if (current->flags.set_flag || current->flags.clear_flag ||
		 current->flags.add_flag || current->flags.remove_flag){
	VestaSource::attribOp op;
	if(current->flags.set_flag) {
	  op = VestaSource::opSet;
	} else if(current->flags.clear_flag) {
	  op = VestaSource::opClear;
	  current->value = "";
	} else if(current->flags.add_flag) {
	  op = VestaSource::opAdd;
	} else {
	  assert(current->flags.remove_flag);
	  op = VestaSource::opRemove;
	}		
	VestaSource::errorCode err =
	  current->sourceVS->writeAttrib(op, current->attrib.cchars(),
					 current->value.cchars(), NULL, 
					 current->timestamp);
	if (err != VestaSource::ok) {
	  Text msg = program_name + ":";
	  if(multiple) {
	    msg = msg + " " + source; 
	    if(repos != lrepos)
	      msg = msg + " at " + repos; 
	  }
	  msg = msg + " on writeAttrib: " +  ReposUI::errorCodeText(err);
	  cerr << msg << endl;
	  exit(2);
	}
      }

      delete current;
    }
    
  } catch (VestaConfig::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  } catch (SRPC::failure f) {
    cerr << program_name
	 << ": SRPC failure; " << f.msg << " (" << f.r << ")" << endl;
    exit(2);
  } catch (ReposUI::failure f) {
    cerr << program_name << ": " << f.msg << endl;
    exit(2);
  }
  
  return 0;
}




