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
// vgetconfig.C
//

// Read from the vesta.cfg file; for use in scripts.  See VestaConfig.H.

// Usage:
//  vgetconfig [-l location] [-L] [-i] [-b] [-f] [section name]

// If section and name are provided, and -i is not specified, print
// VestaConfig::get_Text(section, name).  If section and name are
// provided, and -i is specified, print VestaConfig::get_int(section,
// name).

// Other options:
// -l location 
//   Use the given location for the config file, overriding the default.
// -L 
//   Print the config file location.

// If there is nothing to do, a usage message is printed.

#include <Basics.H>
#include <Text.H>
#include <VestaConfig.H>

#if defined(HAVE_GETOPT_H)
extern "C" {
#include <getopt.h>
}
#endif

using std::cerr;
using std::cout;
using std::endl;

Text program_name;
extern const char *Version;

void
Usage()
{
    cerr << "Usage: " << program_name << endl <<
      "        [-l location] [-L] [-V] [-I]" << endl <<
      "        [-i] [-b] [-f]" << endl <<
      "        [-s [section]]" << endl <<
      "        [[-w|-W] section name [section name ...]]" << endl << endl;
    exit(1);
}

int
main(int argc, char* argv[])
{
  typedef enum { get_text, get_int, get_bool, get_float, get_one_location, get_all_location } get_t;
    try {
	Text location, section, name;
	bool default_location = true, print_location = false, print_value = true;
	bool print_version = false, print_sect = false;
	bool print_include_hierarchy = false;
	int c;
	get_t get_type = get_text;
	// 
	// Parse command line
	//
	program_name = argv[0];
	opterr = 0;
	for (;;) {
	    c = getopt(argc, argv, "l:ibfLVswWI");
	    if (c == EOF) break;
	    switch (c) {
	      case 'l':
		location = optarg;
		default_location = false;
		break;
	      case 'i':
		get_type = get_int;
		break;
	      case 'b':
		get_type = get_bool;
		break;
	      case 'f':
		get_type = get_float;
		break;
	      case 'L':
		print_location = true;
		break;
	      case 'V':
		print_version = true;
		break;
	      case 's':
		print_sect = true;
		break;
	      case 'w':
		get_type = get_one_location;
		break;
	      case 'W':
		get_type = get_all_location;
		break;
	      case 'I':
		print_include_hierarchy = true;
		print_value = false;
		break;
	      case '?':
	      default:
		Usage();
	    }
	}
	
	if (optind == argc) {
	    if (!print_location && !print_version && !print_sect &&
		!print_include_hierarchy) {
		cerr << program_name << ": nothing to do\n" << endl;
		Usage();
	    }
	    print_value = false;
	} else if ((argc - optind) == 1) {
	  if(!print_sect)
	    Usage();
	  print_value = false;
	} else if ((argc - optind) & 0x1) {
	    Usage();
	}

	if((get_type == get_one_location) ||
	   (get_type == get_all_location))
	  {
	    VestaConfig::record_setting_locations();
	  }
	if(print_include_hierarchy)
	  {
	    VestaConfig::record_include_hierarchy();
	  }

	if (!default_location) {
	    VestaConfig::set_location(location);
	}	    

	if (print_version) {
	    cout << program_name << ": version " << Version << endl;
	}

	if (print_location) {
	    cout << VestaConfig::get_location() << endl;
	}

	if(print_sect)
	  {
	    TextSeq result;
	    if(argc > optind)
	      result = VestaConfig::section_vars(argv[optind]);
	    else
	      result = VestaConfig::sections();
	    while(result.size())
	      {
		cout << result.remlo() << endl;
	      }
	  }

	if(print_value)
	  {
	    for(;optind < argc;optind += 2) {
		section = argv[optind];
		name = argv[optind + 1];
		switch(get_type)
		  {
		  case get_text:
		    cout << VestaConfig::get_Text(section, name) << endl;
		    break;
		  case get_int:
		    cout << VestaConfig::get_int(section, name) << endl;
		    break;
		  case get_bool:
		    cout << (VestaConfig::get_bool(section, name)
			 ? "true" : "false")
		         << endl;
		    break;
		  case get_float:
		    cout << VestaConfig::get_float(section, name) << endl;
		    break;
		  case get_one_location:
		  case get_all_location:
		    {
		      VestaConfig::SettingLocationSeq location_seq =
			VestaConfig::get_setting_locations(section, name);
		      if((get_type == get_one_location) && (location_seq.size() > 0))
			{
			  VestaConfig::SettingLocation location = location_seq.gethi();
			  cout << location.file << ":" << location.line
			       << ":\t" << location.val << endl;
			}
		      else
			{
			  while(location_seq.size() > 0)
			    {
			      VestaConfig::SettingLocation location = location_seq.remlo();
			      cout << location.file << ":" << location.line
				   << ":\t" << location.val << endl;
			    }
			}
		    }
		  }
	     }
	  }

	if(print_include_hierarchy)
	  {
	    VestaConfig::IncludeTree *inc_tree =
	      VestaConfig::get_include_hierarchy();

	    inc_tree->Print(cout);

	    delete inc_tree;
	  }

    } catch (VestaConfig::failure f) {
	cerr << program_name << ": " << f.msg << endl;
	exit(2);
    }
    
    return 0;
}


