#!/usr/bin/perl -w

# Copyright (C) 2002-2004, Kenneth C. Schalk

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

# ----------------------------------------------------------------------
# pkg2vesta.pl

# Author:	Ken Schalk

# A script to extract the files from an OS component package (e.g. a
# Debian package archive or RPM) and create a Vesta package which
# provides these files for use in building an encapsulated filesystem
# for tool invocations.

# Based on the rpm2vesta shell script.

# Major to-do items:

# - Warn the user about scripts (giving them the option to display
# them, fork a shell to examine them, ignore them ...)

# - Put more in the pod documentation (examples)

# - More sanity checks (e.g. that we haven't been asked to import
# packages from multiple different architectures into one package, or
# multiple different versions of the same package, etc.)

# - Be able to copy from an installed Debian system as an alternative
# to .deb archives (already works for RPM systems)

# - Document how to add a new packaging type

# ----------------------------------------------------------------------

# Command-line option handling
use Getopt::Long;

# Facilitate printing our own embedded documentation.
use Pod::Usage;

# Human-readable names for Perl special variables
use English;

# strftime
use POSIX;

# Make it possible to use certain shell command like functions
use Shell qw{ find cp rm };

# Safely create temporary directories
BEGIN {
  # This used to be 'use File::Temp qw{ tempdir };', but some orlder
  # Perls don't have File::Temp.  this is sort of a conditional use.
  eval {
    require File::Temp;
    import File::Temp qw{ tempdir };
  };
  if($@)
    {
      # Use this simplistic tempdir subroutine in that case.
      eval ('sub tempdir {'.
	    'my $pattern = shift;'.
	    '$pattern =~ s/XXXXX$/.$$/;'.
	    'my $path = "/tmp/".$pattern;'.
	    'mkdir($path, 0700) or die "Could not mkdir $path: $!";'.
	    'return $path;'.
	    '}');
    }
};

# Make some configuration information available.
use Config;

# Simple text wrapping
use Text::Wrap;

# Ability to flush output
use IO::Handle;

#----------------------------------------------------------------------

=head1 NAME

pkg2vesta.pl - Import an OS component package (e.g. a Debian package
archive or RPM) into a Vesta package which provides its files for use
in building an encapsulated filesystem for tool invocations.

=head1 SYNOPSIS

pkg2vesta.pl [--test] [--verbose]
             [--vhost <hostname>] [--rsh <command>]
             [--alias <name>]
             [--preinstall <command string>]
             [--postinstall <command string>]
             [--package-root /vesta/example.com/...]
             [--package-pattern <pattern>]
             <package> [<package2> ...]

=head1 DESCRIPTION

This script is for populating a Vesta package with an I<OS component
package>.  It knows how to use several different OS packaging systems
(RedHat, Debian, Tru64).

=head1 OPTIONS

=over 4

=item B<--test>

Perform a dry run.  Don't attempt to create antything in the Vesta
repository, but create a temporary directory with the contents which
would be put into a package.

=item B<--verbose>

Produce additional messages about what the script is doing.  May be
useful for debugging this script.

=item B<--vhost> I<hostname>

Remote host where the Vesta repository tools (e.g. vcreate, vcheckout)
can be run.  Useful if this script is being run on a new platform
where the Vesta tools don't work (e.g. when constructing a Vesta-based
build environment targetting a new platform).

=item B<--rsh> I<command>

The command we should use for rsh if vhost is set (e.g. "ssh").
Defaults to "rsh".

=item B<--package-root> I</vesta/example.com>

The directory within the Vesta repository below which a directory
structure to hold OS component packages should be created.  Can be
given a default value with a setting in F<vesta.cfg>.  (See the
section on configuration below.)

=item B<--package-pattern>

Pattern used to construct the name of the package to hold the imported
rpm/deb.

This pattern has several replacements performed on it to get the
actual path:

=over 4

=item B<%a> is replaced with the CPU architecture (alpha, i386)

=item B<%p> is replaced with the name of the imported package (glibc, binutils, gcc)

=item B<%o> is replaced with the OS name (linux, tru64, hpux)

=item B<%v> is replaced with the vendor name (redhat, debian, suse, dec, hp, sun)

=back

Defaults to F<platforms/%o/%v/%a/components/%p> which is fine in most
cases.  Can be given a default value with a setting in F<vesta.cfg>.
(See the section on configuration below.)

=item B<--work-dir-pattern>

Pattern used to construct the name of the working directory when the
package is checked out.  This pattern has the same replacements
performed on it as B<--package-pattern> (B<%a>, B<%p>, B<%o>, and
B<%v>) and the pattern is appended after the mutable root
(/vesta-work) and a directory with the user's login name.  The pattern
defaults to F<%o-%v-%a/%p> (which could expand to
F<linux-debian-i386/binutils>).  Can be given a default value with a
setting in F<vesta.cfg>.  (See the section on configuration below.)

=item B<--from-installed>

Rather than expanding a package archive file, copy files installed on
the host system.  This queries the package system for information
about installed files and copies them for you.  Can be useful if you
don't have a package archive file.

=item B<--skip-unreadable>

When copying files from an installed package, skip any that the script
cannot read.  Has no effect unless --from-installed is also used.

=item B<--excludepath> I<path>

Apecify a path to be excluded when importing the OS component.  If
this is a directory, anything inside it will be excluded.  May be
repeated.

=item B<--excludedocs> | B<--no-excludedocs>

Determines whether documentation files (e.g. man pages) should be
excluded when copying into a Vesta package.  The default is
--excludedocs.

=item B<--preinstall>

Shell commands to run before extracting files from the package.  This
can be used to prepare the directory.

=item B<--postinstall>

Shell commands to run after extracting files from the package.  this
can be used to modify the files being imported (e.g. to perform some
action that might be done by the packaging system when installing
normally).

=item B<--alias> I<name>

Provide an alternate name by which this OS component will be known.
May be repeated.

=item B<--pkg-type> I<rpm|deb|tru64-subse>

Explicitly specify the type of OS component package to import.
Usually the script can figure this out on its own, but sometimes this
option is needed (e.g. if you have both rpm and dpkg on your system
and you use --from-installed)

=item B<--rpm> | B<--redhat>

Equivalent to "--pkg-type rpm"

=item B<--deb> | B<--debian>

Equivalent to "--pkg-type deb"

=item B<--tru64> | B<--tru64-subset> | B<--setld>

Equivalent to "--pkg-type tru64-subset"

=item B<--os> I<os-string> | B<--vendor> I<vendor-string> | B<--arch>
I<arch-string> | B<--name> I<name-string> | B<--version>
I<version-string>

Normally the strings used to construct the Vesta package path are
determined automatically by querying the OS component packaging
system.  These options can be used to override those values.

=item B<-h> | B<--help>

Print a brief help message and exits.

=item B<-m> | B<--man>

Prints the manual page and exits.

=back

=head1 CONFIGURATION

This script uses two settings from vesta.cfg, both in the
[pkg2vesta.pl] section:

=over 4

=item package-root

Sets the default top-level directory in the Vesta repository under
which OS component packages should be imported.  Can be overridden on
the command line with B<--package-root>.  Must start with
"F</vesta/>".

=item package-pattern

Sets the default pattern of directory structure for creating Vesta
packages to hold imported OS component packages.  Interpreted relative
to the package root.  Can be overridden on the command line with
B<--package-pattern>.  Defaults to F<platforms/%o/%v/%a/components/%p>
which is fine in most cases.

=item work-dir-pattern

Sets the default pattern of working directory names when checking out
packages to hold imported OS component packages.  Interpreted relative
to the the user's directory below the mutable root.  Can be overridden
on the command line with B<--work-dir-pattern>.  Defaults to
F<%o-%v-%a/%p> which is fine in most cases.

=back

=head1 AUTHOR

Kenneth C. Schalk E<lt>ken at xorian dot netE<gt>

Based on rpm2vesta written by:

Tim Mann E<lt>tim at tim dash mann dot orgE<gt>

=head1 SEE ALSO

vmkdir(1), vcreate(1), vbranch(1), vcheckout(1), vesta.cfg(5)

=cut

#----------------------------------------------------------------------

# ----------------------------------------------------------------------
# Command-line option handling
# ----------------------------------------------------------------------

%Options = (
	    # Ask for help at two different levels.

	    'help' => 0,
	    'man'  => 0,

	    # Don't actually do anything in the Vesta repository, just
	    # create what we would copy into the .

	    'test' => 0,

	    # Turn on verbose output to help debugging this script

	    'verbose'  => 0,

	    # Shell commands to run in the temporary root directory
	    # before and after extracting files from the package
	    # archive(s).  These can be used to prepare or fix up the
	    # filesystem.

	    'preinstall' => "",
	    'postinstall' => "",

	    # 'vhost' will specify a host where the Vesta repos_ui
	    # tools can be run.  If not specified, they will be run
	    # locally.

	    # The command we should use for rsh if vhost is set.

	    'rsh' => "rsh",

	    # package-pattern: Pattern used to construct the name of
	    # the package to hold the imported rpm/deb.  %a is
	    # replaced with the CPU architecture (alpha, i386). %p is
	    # replaced with the name of the imported package (glibc,
	    # binutils, gcc).  %o is replaced with the OS name (linux,
	    # tru64, hpux).  %v is replaced with the vendor name
	    # (redhat, debian, suse, dec, hp, sun).

	    'package-pattern' => undef,

	    # work-dir-pattern: Pattern used to construct the name of
	    # the working directory.  The pattern substitutions are
	    # applied as with package-pattern.

	    'work-dir-pattern' => undef,

	    # Root within which package-pattern is used.

	    # 'package-root' => "/vesta/vestasys.org"
	    # 'package-root' => "/vesta/env1.vestasys.org"

	    'package-root' => undef,

	    # These options allow the user to specify the pieces
	    # substituted into the package pattern.  In some cases
	    # this may be neccessary is the script can't determine it
	    # or guesses wrong.

	    'os' => undef,
	    'vendor' => undef,
	    'arch' => undef,
	    'name' => undef,
	    'version' => undef,

	    # alias: alternate names by which this package will be
	    # known.

	    'alias' => [],

	    # excludepath: paths which shouldn't be included in the
	    # provided filesystem.

	    'excludepath' => [],

	    # Should we exclude documentation files?

	    'excludedocs' => 1,

	    # Should we copy the files from installed packages rather
	    # than extracting the contents of a package archive?

	    'from-installed' => 0,

	    # When copying files from installed packages, should we
	    # skip any that we can't read?

	    'skip-unreadable' => 0,

	    # The following options allow the user to specify which
	    # package type we'll be using.  In some cases, we need
	    # this hint.

	    'pkg-type' => undef,

	    'rpm'          => sub { $Options{'pkg-type'} = "rpm"; },
	    'redhat'       => sub { $Options{'pkg-type'} = "rpm"; },
	    'deb'          => sub { $Options{'pkg-type'} = "deb"; },
	    'debian'       => sub { $Options{'pkg-type'} = "deb"; },
	    'tru64'        => sub { $Options{'pkg-type'} = "tru64-subset"; },
	    'tru64-subset' => sub { $Options{'pkg-type'} = "tru64-subset"; },
	    'setld'        => sub { $Options{'pkg-type'} = "tru64-subset"; },

	    );

# Save the command-line arguments so we can record them later.
@Saved_ARGV = @ARGV;

GetOptions(\%Options,
	   'help', 'man', 'usage',
	   'verbose',
	   'test',
	   'preinstall=s',
	   'postinstall=s',
	   'vhost=s',
	   'rsh=s',
	   'alias=s',
	   'excludepath|exclude=s',
	   'from-installed',
	   'excludedocs!',
	   'skip-unreadable',
	   'pkg-type=s',
	   'rpm', 'redhat',
	   'deb', 'debian',
	   'tru64', 'tru64-subset', 'setld',
	   'package-pattern|pkg-pattern|pattern=s',
	   'package-root|pkg-root|root=s',
	   'work-dir-pattern|wd-pattern|wd-pat=s',
	   'os=s',
	   'vendor=s',
	   'arch|architecture=s',
	   'name=s',
	   'version=s') or pod2usage(2);
pod2usage(1) if $Options{'help'};
pod2usage(2) if $Options{'usage'};
pod2usage(-exitstatus => 0,
	  -verbose => 2,
	  -noperldoc => 1) if $Options{'man'};

# ----------------------------------------------------------------------
# Global variables
# ----------------------------------------------------------------------

# These are the known package types that this script can handle.
# Later we set the value to 1 if we think we have the tools needed to
# import that kind of package.

%Pkg_Types = (
	      "deb" => 0,
	      "rpm" => 0,
	      "tru64-subset" => 0
	     );


# Used to hold a parsed representation of "setld -i" (the inventory of
# installed/known Tru64 subsets).

$Subset_Inventory_Cache = undef;

# This will hold information gathered about the OS component packages
# we're importing.

%Pkg_Info = ();

# ----------------------------------------------------------------------
# Forward declarations
# ----------------------------------------------------------------------

sub resolve_symlinks ($ );

# ----------------------------------------------------------------------
# Functions
# ----------------------------------------------------------------------

# combine_path(<parent dir>, <path inside parent>)

# Combine a base directory with a path beneath it, ensuring that
# there's exactly one slash between the path components.

sub combine_path ($$)
  {
    my $p1 = shift;
    my $p2 = shift;
    my $result = $p1."/".$p2;
    $result =~ s|/+|/|g;
    return $result;
  }

# canonicalize_path(<relative path>)

# Eliminate '.' and '..' path components.

sub canonicalize_path ($ )
  {
    my $path = shift;

    # Eliminate '.' path components: '/./' -> '/'
    $path =~ s{/\./}{/}g;

    # Eliminate double-slashes.
    $path =~ s|//|/|g;

    # Eliminate '..' path components: '/{not ..}/../' -> '/'
    # Also eliminate leading './' components.
    1 while($path =~ s{/([^/]{3,}|[^./][^/]|[^/][^./]|[^/])/\.\./}{/}g ||
	    $path =~ s{^\./}{});

    return $path;
  }

# parent_dir(<path>)

# Return the parent directory of the given path.

sub parent_dir ($ )
  {
    my $path = shift;

    $path =~ s|/[^/]+/?$||;

    return $path;
  }

# wait_for(<closure>, <description>)

# Function used for waiting for some test to return a true value.

sub wait_for (&$ )
  {
    my $test = shift;
    my $msg = shift;
    my $count = 0;

    # Until the test returns true...
    while(!&$test())
      {
        if(($count > 0) && (($count % 10) == 0))
          {
            print("\tStill waiting for $msg after $count seconds...\n");
          }

        # Sleep and try again
        sleep(1);
        $count++;
      }
  }

# wait_for_nfs_directory

# wait for a directory to appear in the NFS representation of the
# repostiory.  THis is important after a vcheckin or other change
# made via SRPC.

sub wait_for_nfs_directory ($ )
  {
    my $path = shift;
    wait_for(sub { return -d $path },
             "NFS to catch up with creation of $path");
  }

# which(<command name>)

# Check to make sure a command is available

sub which ($ )
{
    my $command = shift;
    # Loop over the directories on the path
    foreach my $dir (split(':', $ENV{PATH}))
    {
        my $test = combine_path($dir, $command);
        if(-e $test && -x $test)
        {
            return $test
        }
    }
    return undef;
}

# pkg_file_kind(<package file name>)

# Determine the kind of package file this is.

sub pkg_file_kind ($ )
  {
    my $pkg_fname = shift;

    if($pkg_fname =~ /\.deb$/)
      {
	return "deb";
      }
    elsif($pkg_fname =~ /\.rpm$/)
      {
	return "rpm";
      }
    # @@@ Tru64 subsets

    return undef;
  }

# deb_file_info(<.deb file name>)

# Get information from a Debian package file.

sub deb_file_info ($ )
  {
    my $deb_fname = shift;

    my $info = `dpkg-deb --info $deb_fname`;

    if((!defined $info) || ($? != 0))
      {
	print STDERR ("error -- \"dpkg-deb --info\" failed for $deb_fname\n");
	return undef;
      }

    my %result;

    if($info =~ /^\s*Package:\s*(\S+)/m)
      {
	$result{"name"} = $1;
      }

    if($info =~ /^\s*Version:\s*(\S+)/m)
      {
	$result{"version"} = $1;
      }

    if($info =~ /^\s*Architecture:\s*(\S+)/m)
      {
	$result{"arch"} = $1;
      }

    # @@@ Is there any vendor information recorded in .deb archives?
    $result{"vendor"} = "debian";

    $result{"aliases"} = [];

    return \%result;
  }

# rpm_vendor_path_component(<vendor string>)

# Return a path component representing the vendor that packaged this
# RPM.

sub rpm_vendor_path_component($ )
  {
    my $vendor = shift;

    if($vendor eq "Red Hat, Inc.")
      {
	return "redhat";
      }

    if(($vendor eq "SUSE LINUX Products GmbH, Nuernberg, Germany") or
       ($vendor eq "SuSE LINUX Products GmbH, Nuernberg, Germany") or
       ($vendor eq "SuSE Linux AG, Nuernberg, Germany"))
      {
        return "suse";
      }

    if($vendor eq "(none)")
      {
	return "unknown_vendor"
      }

    # @@@ Add recognition of other common RPM-based distributions
    # (SusE, Mandrake, etc.) here.

    # No known vendor path elenent for this one, so translate it into
    # a reasonable path component ourselves.  (This doesn't have to be
    # perfect, as the real vendor string will still be checked into
    # the package.)

    $vendor =~ s/\W/_/g;
    $vendor =~ s/_*$//;
    $vendor =~ s/^_*//;
    $vendor =~ tr/A-Z/a-z/;

    if(length($vendor) < 1)
      {
	return undef;
      }

    return $vendor;
  }

# rpm_info(<.rpm file name, or installed rpm name>)

# Get information from a RedHat package.  Works on either a .rpm file
# or an installed package.

sub rpm_info ($ )
  {
    my $rpm_name = shift;

    my $info;
    if($Options{'from-installed'})
      {
	$info = `rpm -q --queryformat "%{NAME}\n%{ARCH}\n%{VERSION}-%{RELEASE}\n%{VENDOR}\n" $rpm_name`;
      }
    else
      {
	$info = `rpm -q --queryformat "%{NAME}\n%{ARCH}\n%{VERSION}-%{RELEASE}\n%{VENDOR}\n" -p $rpm_name`;
      }

    if((!defined $info) || ($? != 0))
      {
	print STDERR ("error -- \"rpm -q\" failed for $rpm_name\n");
	return undef;
      }

    # Split rpm's output by line
    my @info = split /\n/, $info;

    my %result;
    $result{"name"} = $info[0];
    $result{"arch"} = $info[1];
    $result{"version"} = $info[2];
    $result{"vendor"} = rpm_vendor_path_component($info[3]);
    $result{"aliases"} = [];

    return \%result;
  }

# tru64_subset_info(<subset name>)

# Get information about a Tru64 subset from the system inventory.

sub tru64_subset_info ($ )
  {
    my $subset = shift;

    # If we haven't previously run "setld -i", do so now.
    if(!defined $Subset_Inventory_Cache)
      {
	print ("Getting system inventory...\n");
	unless(open SETLD, "setld -i|")
	  {
	    print STDERR ("Fatal error: Couldn't run \"setld -i\"\n");
	    exit(1);
	  }

	my $inventory = {};

	# The architecture of all subsets is that of the host.  (Could
	# just hardocde it as "alpha".)
	my $arch = `uname -m`;
	chomp $arch;

	# Hard code the vendor to HP, since they merged with Compaq
	# that bought Digital.
	my $vendor = "hp";

	while(<SETLD>)
	  {
	    # Skip uninteresting lines
	    if(/^\s*$/ ||
	       /^Subset\s+Status\s+Description$/ ||
	       /^-+\s+-+\s+-+$/)
	      {
		next;
	      }
	    # Line for an installed or partially installed subset
	    elsif(/^([A-Z]+)(\d+)\s+(installed|incomplete|corrupt)\s+(.*)$/)
	      {
		my $name = $1;
		my $version = $2;
		my $subset = $name.$version;
		my $status = $3;
		my $description = $4;

		my $lname = $name;
		$lname =~tr/A-Z/a-z/;

		my $info = { 'name' => $lname,
			     'aliases' => [$subset, $name],
			     'version' => $version,
			     'status' => $status,
			     'description' => $description,
			     'arch' => $arch,
			     'vendor' => $vendor };

		# Remember the information for this subset.
		$inventory->{$subset} = $info;

		# If this isn't a patch subset and it's the first or
		# highest installed version we've seen with this name,
		# also save it under just the name.
		if($name ne "OSFPAT" &&
		   ((!exists $inventory->{$name}) ||
		    (($status eq "installed") && ($version > $inventory->{$name}->{'version'}))))
		  {
		    $inventory->{$name} = $info;
		  }
	      }
	    # Line for a known but not installed subset
	    elsif(/^([A-Z]+)(\d+)\s+(.*)$/)
	      {
		my $name = $1;
		my $version = $2;
		my $subset = $name.$version;
		my $description = $3;

		my $lname = $name;
		$lname =~tr/A-Z/a-z/;

		# Remember the information for this subset.
		$inventory->{$subset} = { 'name' => $lname,
					  'aliases' => [$subset, $name],
					  'version' => $version,
					  'status' => "not installed",
					  'description' => $description,
					  'arch' => $arch,
					  'vendor' => $vendor };
	      }
	  }

	close SETLD;

	# Save the subset inventory for later calls to this function
	$Subset_Inventory_Cache = $inventory;

	print ("... done getting system inventory.\n");
      }

    # If this is a known subset, return information about it.
    if(exists $Subset_Inventory_Cache->{$subset})
      {
	return $Subset_Inventory_Cache->{$subset};
      }

    print STDERR ("Error: unknown subset \"$subset\"\n");

    return undef;
  }

# pkg_info(<package name or filename>)

# Get information about a package.

sub pkg_info ($ )
  {
    my $pkg = shift;
    my $type = $Options{'pkg-type'};

    if(($type eq "deb") && (-f $pkg))
      {
	return deb_file_info($pkg);
      }
    elsif($type eq "rpm")
      {
	if($Options{'from-installed'} || (-f $pkg))
	  {
	    return rpm_info($pkg);
	  }
	else
	  {
	    print STDERR ("Error: rpm $pkg isn't a file, and --from-installed not given\n");

	    return undef;
	  }
      }
    elsif(($type eq "tru64-subset") && (!-e $pkg))
      {
	return tru64_subset_info($pkg);
      }

    print STDERR ("Error: Don't know how to get information about $pkg\n");

    return undef;
  }

sub save_deb_file_info ($$@ )
  {
    my $work = shift;
    my $target = shift;
    my @pkgs = @_;

    # Extract the Debian control information.  As we go, gather the
    # control information together into a single file.

    my $control_temp = combine_path($work, "control");
    mkdir $control_temp;

    my $collected_info = combine_path($target, "control");
    open CONTROL_OUT, ">$collected_info";
    foreach my $pkg (@pkgs)
      {
	my $name = $Pkg_Info{$pkg}->{"name"};

	# This directory will contain all the control data for this
	# package.
	my $control_dir = combine_path($control_temp, $name);
	mkdir $control_dir;

	# Extract thye control data.
	if(system("dpkg-deb", "--control", $pkg, $control_dir) != 0)
	  {
	    print STDERR
	      ("error -- couldn't extract control information from ",
	       $pkg, "\n");
	    exit(1);
	  }

	# Look for the main information file.  If it's present,
	# collect it into the single file where we're placing all the
	# control information.
	my $control_info = combine_path($control_dir, "control");
	if(-f $control_info)
	  {
	    print CONTROL_OUT (("-"x70), "\n",
			       "Control information for ", $name, "\n",
			       ("-"x70), "\n");
	    open CONTROL_IN, $control_info;
	    while(<CONTROL_IN>)
	      {
		print CONTROL_OUT;
	      }
	    close CONTROL_IN;
	  }
	else
	  {
	    print CONTROL_OUT (("-"x70), "\n",
			       "No control information for ", $name, "\n",
			       ("-"x70), "\n");
	  }
      }
    close CONTROL_OUT;

    return "control";
  }

sub save_rpm_info ($$@ )
  {
    my $work = shift;
    my $target = shift;
    my @pkgs = @_;

    # Extract information about the RedHat packages.  As we go, gather
    # the information together into three files: info, provides,
    # requires.

    my $collected_info = combine_path($target, "info");
    my $collected_requires = combine_path($target, "requires");
    my $collected_provides = combine_path($target, "provides");

    foreach my $pkg (@pkgs)
      {
	if($Options{'from-installed'})
	  {
	    unless((system("rpm -q -i $pkg >> $collected_info") == 0) &&
		   (system("rpm -q --requires $pkg >> $collected_requires")
		    == 0) &&
		   (system("rpm -q --provides $pkg >> $collected_provides")
		    == 0))
	      {
		print STDERR
		  ("error -- couldn't save information about ",
		   $pkg, "\n");
		exit(1);
	      }
	  }
	elsif(-f $pkg)
	  {
	    unless((system("rpm -q -i -p $pkg >> $collected_info") == 0) &&
		   (system("rpm -q --requires -p $pkg >> $collected_requires")
		    == 0) &&
		   (system("rpm -q --provides -p $pkg >> $collected_provides")
		    == 0))
	      {
		print STDERR
		  ("error -- couldn't save information about ",
		   $pkg, "\n");
		exit(1);
	      }
	  }
	else
	  {
	    print STDERR
	      ("error -- ", $pkg,
	       " is not a file and --from-installed wasn't specified\n");
	    exit(1);
	  }
      }

    return ("/* Output of 'rpm -q -i' */ info, ".
	    "/* Output of 'rpm -q --requires' */ requires, ".
	    "/* Output of 'rpm -q --provides' */ provides");
  }

sub save_tru64_subset_info ($$@ )
  {
    my $work = shift;
    my $target = shift;
    my @subsets = @_;

    my $descr_file = combine_path($target, "descriptions");
    open DESCR_FILE, ">$descr_file";
    print DESCR_FILE ("Subset\t\tDescription\n",
		      "------\t\t-----------\n");
    foreach my $subset (@subsets)
      {
	print DESCR_FILE ($subset, "\t",
			  $Pkg_Info{$subset}->{'description'}, "\n");
      }

    print DESCR_FILE ("\n",
		      "(Unless otherwise noted, the vendor is HP.)\n");

    close DESCR_FILE;

    return "descriptions";
  }

# save_pkg_info(<work dir>, <target dir>, <package>, ...)

# Extarct information about an OS component package for inclusion in
# the Vesta package.

sub save_pkg_info ($$@ )
  {
    my $work = shift;
    my $target = shift;
    my @pkgs = @_;

    if(($Options{'pkg-type'} eq "deb") &&
       !$Options{'from-installed'})
      {
	return save_deb_file_info($work, $target, @pkgs);
      }
    if($Options{'pkg-type'} eq "rpm")
      {
	return save_rpm_info($work, $target, @pkgs);
      }
    elsif(($Options{'pkg-type'} eq "tru64-subset") &&
	  $Options{'from-installed'})
      {
	return save_tru64_subset_info($work, $target, @pkgs);
      }

    return undef;
  }

sub extract_deb ($$)
  {
    my $root = shift;
    my $deb = shift;

    if(system("dpkg-deb", "--extract", $deb, $root) != 0)
      {
	print STDERR ("error -- couldn't extract contents of ",
		      $deb, "\n");
	exit(1);
      }

    return ();
  }

sub extract_rpm ($$)
  {
    my $root = shift;
    my $rpm = shift;

    if(system("rpm2cpio $rpm | (cd $root; cpio --extract --make-directories)") != 0)
      {
	print STDERR ("error -- couldn't extract contents of ",
		      $rpm, "\n");
	exit(1);
      }

    return ();
  }

sub copy_rpm ($$)
  {
    my $root = shift;
    my $rpm = shift;

    if($Options{'verbose'})
      {
	print ("-- Before copying, verifying installed rpm: ", $rpm, "\n");
      }
    unless(system("rpm", "-V", $rpm) == 0)
      {
	STDOUT->printflush("Failed to verify installed rpm: ", $rpm, "\n",
			   "Continue? (y/n) ");
	my $answer = <STDIN>;
	chomp $answer;
	if($answer !~ /^[yY]$/)
	  {
	    print STDERR ("Error: Verification of installed rpm ", $rpm,
			  " failed\n");
	    exit(1);
	  }
      }

    unless(open(RPM, "rpm -q -l $rpm |"))
      {
	print STDERR ("Fatal error: Couldn't run rpm to get contents ",
		      "of $rpm\n");
	exit(1);
      }

    my @skipped = ();

    while(<RPM>)
      {
	chomp;

	my $source = $_;
	my $target = combine_path($root, $source);

	# Skip any paths that we're supposed to exclude.
	if(path_excluded($source))
	  {
	    if($Options{'verbose'})
	      {
		print ("Skipping excluded path: ", $source, "\n");
	      }

	    push @skipped, $source;
	    next;
	  }

	# If we're supposed to skip anything we can't read, and we
	# can't read this one, skip it.
	if($Options{'skip-unreadable'} && (!-r $source))
	  {
	    if($Options{'verbose'})
	      {
		print ("Skipping unreadable path: ", $source, "\n");
	      }

	    push @skipped, $source;
	    next;
	  }

	# Get the parent directory and create it if necessary.
	my $parent = parent_dir($target);
	if((!-e $parent) && (system("mkdir", "-p", $parent) != 0))
	  {
	    print STDERR ("Fatal error: couldn't create directory (",
			  $parent, ")\n");
	    exit(1);
	  }

	# If this is a directory, just create it.  RPMs often claim
	# ownership of directories, but that doesn't mean we should
	# copy everything in them.
	if(-d $source)
	  {
	    mkdir($target);
	    next;
	  }

	# If it's not a ditrectory, copy it, preserving file type.
	if(system("cp", "-R", $source, $target) != 0)
	  {
	    print STDERR ("Fatal error: couldn't copy $source to $target\n");
	    exit(1);
	  }
      }

    close RPM;

    return @skipped;
  }

sub copy_tru64_subset ($$)
  {
    my $root = shift;
    my $subset = shift;

    my @skipped = ();

    unless(open(SETLD, "setld -i $subset|"))
      {
	print STDERR ("Fatal error: Couldn't run setld to get contents ",
		      "of subset $subset\n");
	exit(1);
      }

    while(<SETLD>)
      {
	chomp;

	# Paths from setld usually start with "./", so we canonicalize
	# each one first.
	my $path = canonicalize_path($_);

	# Construct the paths we'll be copying from and to.
	my $source = ("/".$path);
	my $target = combine_path($root, $path);

	# Skip directories.  We create the hierarchy above any file to
	# be copied, and there's no point in creating empty
	# directories.
	next if(-d $source);

	# Skip any paths that we're supposed to exclude.
	if(path_excluded($source))
	  {
	    if($Options{'verbose'})
	      {
		print ("Skipping excluded path: ", $source, "\n");
	      }

	    push @skipped, $source;
	    next;
	  }

	# If we're supposed to skip anything we can't read, and we
	# can't read this one, skip it.
	if($Options{'skip-unreadable'} && (!-r $source))
	  {
	    if($Options{'verbose'})
	      {
		print ("Skipping unreadable path: ", $source, "\n");
	      }

	    push @skipped, $source;
	    next;
	  }

	# Get the parent directory and create it if necessary.
	my $parent = parent_dir($target);
	if((!-e $parent) && (system("mkdir", "-p", $parent) != 0))
	  {
	    print STDERR ("Fatal error: couldn't create directory (",
			  $parent, ")\n");
	    exit(1);
	  }

	# If it's not a ditrectory, copy it, preserving file type.
	if(system("cp", "-R", $source, $target) != 0)
	  {
	    print STDERR ("Fatal error: couldn't copy /$path to $target\n");
	    exit(1);
	  }
      }

    close SETLD;

    return @skipped;
  }

sub copy_pkg ($$)
  {
    my $root = shift;
    my $pkg = shift;

    if(($Options{'pkg-type'} eq "deb") &&
       !$Options{'from-installed'})
      {
	return extract_deb($root, $pkg);
      }
    elsif($Options{'pkg-type'} eq "rpm")
      {
	if($Options{'from-installed'})
	  {
	    return copy_rpm($root, $pkg);
	  }
	else
	  {
	    return extract_rpm($root, $pkg);
	  }
      }
    elsif(($Options{'pkg-type'} eq "tru64-subset") &&
	  $Options{'from-installed'})
      {
	return copy_tru64_subset($root, $pkg);
      }
    else
      {
	print STDERR ("Fatal Error: Don't know how to extarct/copy files ",
		      "making up $pkg\n");
	exit(1);
      }
  }

# vesta_cmd(<command> [, <arg>, ...])

# Run a command that needs to run on a host where we can execute the
# Vesta repos_ui commands.  In the early stages of a new port, these
# may need to be run on a remote host by using rsh or ssh.

sub vesta_cmd_str (@)
  {
    if(exists($Options{'vhost'}))
      {
	return ($Options{'rsh'}." ".$Options{'vhost'}.join(" ",@_));
      }
    else
      {
	return join(" ",@_);
      }
  }

sub vesta_cmd (@)
  {
    my $cmd_str = vesta_cmd_str(@_);
    if($Options{'verbose'})
      {
	print ("-- running: $cmd_str\n");
      }

    return system($cmd_str);
  }

sub owning_rpm ($ )
  {
    my $fname = shift;

    my $search_result = `rpm -q --queryformat "%{NAME}" -f $fname 2>/dev/null`;

    if((!defined $search_result) ||
       ($? != 0))
      {
	return undef;
      }

    return $search_result;
  }

sub owning_deb ($ )
  {
    my $fname = shift;

    my $search_result = `dpkg --search $fname 2>/dev/null`;

    if((!defined $search_result) ||
       ($? != 0))
      {
	return undef;
      }

    if($search_result =~ /^(\S+):/)
      {
	return $1;
      }

    # @@@ There's a problem here: more than one package can own a
    # path.  This is particularly common for directories.  (Try "dpkg
    # -S /lib" or "dpkg -S /usr" for an example.)  Currently we're
    # returning udef, meaning "no OS package owns this".  We could be
    # smarter and return a list, but we'd have to re-work all the code
    # that expects owning_package to return a single text string.
    return undef;
  }

sub owning_tru64_subset ($ )
  {
    my $fname = shift;

    # This is a little cheap, but it seems like a lot of work to read
    # every inventory file.
    my @candidates = `grep -l $fname /usr/.smdb./*.inv`;

    my $result = undef;

    foreach my $candidate (@candidates)
      {
	chomp $candidate;
	if($candidate =~ m|^/usr/\.smdb\./(\w+)\.inv$|)
	  {
	    my $subset = $1;

	    # Skip patch subsets, as anything they might own another
	    # subset should own too.
	    next if($subset =~ /^OSFPAT\d+$/);

	    open INVENTORY, $candidate;
	    while(<INVENTORY>)
	      {
		# The 10th field is the filename.
		if(/^(\S+\s+){9}(\S+)\s+/)
		  {
		    if(($2 eq ".".$fname) ||
		       ($2 eq "./".$fname) ||
		       ($2 eq $fname))
		      {
			$result = $subset;
			last;
		      }
		  }
	      }
	    close INVENTORY;

	    last if(defined $result);
	  }
      }

    return $result;
  }

# owning_package(<file>)

# Figure out what installed package, if any, owns a file

sub owning_package ($ )
  {
    my $fname = shift;

    if($Options{'pkg-type'} eq "deb")
      {
	return owning_deb($fname);
      }
    elsif($Options{'pkg-type'} eq "rpm")
      {
	return owning_rpm($fname);
      }
    elsif($Options{'pkg-type'} eq "tru64-subset")
      {
	return owning_tru64_subset($fname);
      }

    print STDERR ("Fatal Error: Don't know how to find owning package ",
		  "of $fname\n");
    exit(1);
  }

sub date_and_time ()
  {
    return POSIX::strftime("%a %b %d %H:%M:%S %Z %Y", localtime(time));
  }

sub path_tail ($ )
  {
    my $path = shift;

    if($path =~ m|/([^/]+)/?$|)
      {
	return $1;
      }

    return $path;
  }

# glob_to_regex(<glob pattern>)

# Returns a compiled regex which is the equivalent of the glob
# pattern.

# Derived from Text::Glob by Richard Clamp (which is GPL licensed, so
# I believe it's fine to include it here).

sub glob_to_regex ($ )
  {
    my $glob = shift;
    my ($regex, $in_curlies, $escaping);
    local $_;
    my $first_byte = 1;
    for ($glob =~ m/(.)/gs) {
        if ($first_byte) {
	  $regex .= '(?=[^\.])' unless $_ eq '.';
	  $first_byte = 0;
        }
        if ($_ eq '/') {
            $first_byte = 1;
        }
        if ($_ eq '.' || $_ eq '(' || $_ eq ')' || $_ eq '|' ||
            $_ eq '+' || $_ eq '^' || $_ eq '\$' ) {
            $regex .= "\\$_";
        }
        elsif ($_ eq '*') {
            $regex .= $escaping ? "\\*" :
	      "[^/]*";
        }
        elsif ($_ eq '?') {
            $regex .= $escaping ? "\\?" :
              "[^/]";
        }
        elsif ($_ eq '{') {
            $regex .= $escaping ? "\\{" : "(";
            ++$in_curlies unless $escaping;
        }
        elsif ($_ eq '}' && $in_curlies) {
            $regex .= $escaping ? "}" : ")";
            --$in_curlies unless $escaping;
        }
        elsif ($_ eq ',' && $in_curlies) {
            $regex .= $escaping ? "," : "|";
        }
        elsif ($_ eq "\\") {
            if ($escaping) {
                $regex .= "\\\\";
                $escaping = 0;
            }
            else {
                $escaping = 1;
            }
            next;
        }
        else {
            $regex .= $_;
            $escaping = 0;
        }
        $escaping = 0;
    }
    qr/$regex/;
}

# path_excluded(<path>)

# Return a true value if the specified path is with a path to be
# excluded.

sub path_excluded ($ )
  {
    my $path = shift;

    foreach my $excluded (@{$Options{'excludepath'}})
      {
	# Ensure that $excluded starts with a slash.
	$excluded = "/".$excluded;
	$excluded =~ s|//|/|g;

	# Does this look like a glob pattern?
	if($excluded =~ /[\*\?\{\[]/)
	  {
	    # If this path matches this glob pattern exactly or is
	    # within a directory that matches it, it's excluded.
	    my $re = glob_to_regex($excluded);
	    if($path =~ m|^$re(/.*)?$|)
	      {
		return 1;
	      }
	  }
	else
	  {

	    # If the given path

	    if($path eq $excluded)
	      {
		return 1;
	      }

	    # If the given path has $excluded as a prefix, then it's
	    # excluded.
	    my $ex_len = length($excluded);
	    if((length($path) > $ex_len) &&
		  (substr($path, 0, $ex_len) eq $excluded))
	      {
		return 1;
	      }
	  }
      }

    return 0;
  }

# remove_prefix(<prefix>, <whole>)

# If the first parameter is a prefix of the second, return the portion
# after the prefix.  Otherwise just retunr the second parameter.
# (Useful for stripping off leading directories.)

sub remove_prefix ($$)
  {
    my $prefix = shift;
    my $whole = shift;

    my $plen = length $prefix;

    if((length($whole) > $plen) &&
       (substr($whole, 0, $plen) eq $prefix))
      {
	return substr($whole, $plen);
      }
    else
      {
	return $whole;
      }
  }

# absolute_to_relative <temporary root> <link location> <link target>

# Given a symbolic link's location and the absolute pathname it links
# to, generate a relative symbolic link target which can be used to
# replace it relative to the root.

sub absolute_to_relative ($$$ )
{
  my $root = shift;
  my $link = shift;
  my $target = shift;

  # Convert the directory that the link is in to an appropriate number
  # of '..'s
  my $local_name = remove_prefix($root, $link);
  my $updir = parent_dir($local_name);
  if($updir eq "")
    {
      # Special case: the symlink is at the root level.
      $updir = "./";
    }
  else
    {
      $updir =~ s|/[^/]*|../|g;
    }

  # Combine the string of '..'s with the link target.
  my $result = $updir.$target;

  # Eliminate any double-slashes.
  $result =~ s|//|/|g;

  return $result;
}

# root_var_name(<package name>,
#               <root vars of packages>, <packages of root vars>)

# Convert the name of a package to a variable which can be used to
# hold its root filesystem.

sub root_var_name ($$$ )
{
  my $pkg_name = shift;
  my $rvs_of_pkgs = shift;
  my $pkgs_of_rvs = shift;

  my $var_name = $pkg_name;
  $var_name =~ s/[^a-zA-Z0-9_.]/_/g;
  $var_name .= "_root";
  if(!exists($pkgs_of_rvs->{$var_name}))
    {
      $rvs_of_pkgs->{$pkg_name} = $var_name;
      $pkgs_of_rvs->{$var_name} = $pkg_name;
      return $var_name;
    }

  my $suffix = 1;
  while(exists($pkgs_of_rvs->{$var_name.$suffix}))
    {
      $suffix++;
    }
  $var_name .= $suffix;

  $rvs_of_pkgs->{$pkg_name} = $var_name;
  $pkgs_of_rvs->{$var_name} = $pkg_name;
  return $var_name;
}

# root_func_line(<temporary root>, <link position within root>,
#                <owning package>, <file linked to>,
#                <root vars of packages>, <packages of root vars>)

# Generate a portion of a Vesta SDL model for building a filesystem
# which incorporates files belonging to other OS packages.

# <root vars of packages> is a reference to a hash from package name
# to variable name containing its partial root filesystem.

# <packages of root vars> is a reference to a hash from variable name
# to the package name of which the variable contains the partial root
# filesystem.

sub root_func_line ($$$$$$)
  {
    my $root = shift;
    my $link = shift;
    my $owner = shift;
    my $target = shift;
    my $rvs_of_pkgs = shift;
    my $pkgs_of_rvs = shift;

    # Fetch the partial root filesystem of the owning package into a
    # variable, unless it is already in a variable.
    my $owner_root_assignment = "";
    my $owner_root_var;
    if(exists($rvs_of_pkgs->{$owner}))
      {
	$owner_root_var = $rvs_of_pkgs->{$owner}
      }
    else
      {
	$owner_root_var = root_var_name($owner, $rvs_of_pkgs, $pkgs_of_rvs);
	$owner_root_assignment = ("  ".$owner_root_var." = if .!\"".$owner.
				  "\" then ./\"".$owner.
				  "\"/root() else { _=_print(\"WARNING (from ".
				  path_tail($PROGRAM_NAME)."): component ".
				  $owner." not present\"); value []; };\n");
      }

    # Note that we add double-quotes around every path component, as
    # some of them may contain characters not in the set
    # [a-zA-Z0-9_.].

    my $target_path = remove_prefix(($root."/"), $link);
    $target_path =~ s|([^"])/([^"])|$1\"/\"$2|g;

    my $orig_path = $target;
    $orig_path =~ s|([^"])/([^"])|$1\"/\"$2|g;
    $orig_path =~ s|^/([^"])|/"$1|;

    # Here we generate a test expression which will only be true if
    # the target actually exists in the other component.  This allows
    # the root.ves to not fail if it doesn't.

    my $orig_expr = $orig_path."\"";
    $orig_expr =~ s|/([^/]+)$|!$1|;
    my $test_expr = $owner_root_var.$orig_expr;
    while($orig_expr =~ m|/|)
      {
	$orig_expr =~ s|/([^/!]+)!([^/!]+)$|!$1|;
	$test_expr = $owner_root_var.$orig_expr." && ".$test_expr;
      }

    return ($owner_root_assignment.
	    "  root ++= if (".$test_expr.
	    ") then [ \"".$target_path."\" = ".$owner_root_var.
            $orig_path."\" ] else { _=_print(\"WARNING (from ".
	    path_tail($PROGRAM_NAME)."): ".$target.
	    " not present in component ".$owner."\"); value[]; };\n");
  }

# fix_links(<temporary root>)

sub fix_links ($@)
  {
    my $root = shift;
    my %aliases;
    foreach my $alias (@_)
      {
	$aliases{$alias} = 1;
      }

    my $rvs_of_pkgs = {};
    my $pkgs_of_rvs = {};
    my $root_func_lines = "";

    # Find all symbolic links in the temporary root and loop over them
    my @links = find($root, "-type", "l");
    foreach my $link (@links)
      {
	chomp $link;

	# Get the target of this symbolic link
	my $target = readlink($link);

	# Is this an absolute link?
	if($target =~ m|^/|)
	  {
	    # Resolve any symlinks on the target path to give us
	    # alternative candidate paths.
	    my @alternates = resolve_symlinks($target);

	    my $fixed = 0;
	    foreach my $target_alt (@alternates)
	      {
		# Generate the equivalent filename within the
		# temporary root and see if it exists.
		my $local = $root.$target_alt;
		if(-e $local)
		  {
		    # Convert the absolute link to a relative one
		    my $new_target = absolute_to_relative($root, $link,
							  $target_alt);
		    if($Options{'verbose'})
		      {
			print("Fixing $link, old target:\n",
			      "    $target\n",
			      "New target:\n",
			      "    $new_target\n");
		      }
		    unlink $link;
		    symlink $new_target, $link;

		    # We've fixed it, we can stop now.
		    $fixed = 1;
		    last;
		  }
	      }

	    # If we haven't fixed it yet and the file exists in the OS...
	    if(!$fixed && -e $target)
	      {
		foreach my $target_alt (@alternates)
		  {
		    if(-e $target_alt)
		      {
			# It's absolute, but not in the temporary
			# root.  Figure out which OS package owns the
			# linked file (if any).
			my $owned_by = owning_package($target_alt);

			if(defined $owned_by)
			  {
			    if(exists $aliases{$owned_by})
			      {
				if ($Options{'verbose'})
				  {
				    print("Target of link $link seems to be owned by one of the aliases for this package ($owned_by)\n",
					  "(Maybe it was excluded?)\n");
				  }
			      }
			    else
			      {
				# Add a line to the root-generating
				# function to get this file from the
				# package it comes from.
				$root_func_lines .=
				  root_func_line($root, $link,
						 $owned_by, $target_alt,
						 $rvs_of_pkgs, $pkgs_of_rvs);

				if ($Options{'verbose'})
				  {
				    print("Fixing $link, old target:\n",
					  "    $target\n",
					  "Now taken from $owned_by\n");
				  }

				# Get rid of the link.
				unlink $link;

				# We've fixed it.
				$fixed = 1;
				last;
			      }
			  }
		      }
		  }

		# If we still haven't fixed it, no installed OS
		# component package owns it.
		if(!$fixed)
		  {
		    print STDERR ("Warning: bad symbolic link: $link\n",
				  "                         -> $target\n",
				  "    (absolute, non-local exists, not owned by any OS package)\n");


		    # For now we'll delete these.  @@@ Should we let
		    # this kind of file through?
		    unlink $link;
		  }
	      }
	    elsif(!$fixed)
	      {
		print STDERR ("Warning: bad symbolic link: $link\n",
			      "                         -> $target\n",
			      "    (absolute, missing)\n");
		unlink $link;
	      }
	  }
	else
	  {
	    # It's a relative symbolic link.  We only have to do
	    # anything if the linked file doesn't exist.
	    my $local = parent_dir($link)."/".$target;
	    if(! -e $local)
	      {
		# Eliminate '.' and '..' path components.
		my $canonical=canonicalize_path($local);

		# Generate the corresponding pathname outside the
		# temporary root.
		my $absolute=remove_prefix($root, $canonical);

		# If the absolute version exists...
		if(-e $absolute)
		  {
		    # Resolve any symlinks on the target path to give
		    # us alternative candidate paths.

		    my @alternates = resolve_symlinks($absolute);

		    my $fixed = 0;
		    foreach my $absolute_alt (@alternates)
		      {
			# Generate the equivalent filename within the
			# temporary root and see if it exists.
			my $local2 = $root.$absolute_alt;
			if(-e $local2)
			  {
			    # Convert the absolute link to a relative one
			    my $new_target =
			      absolute_to_relative($root, $link,
						   $absolute_alt);
			    if($Options{'verbose'})
			      {
				print("Fixing $link, old target:\n",
				      "    $target\n",
				      "New target:\n",
				      "    $new_target\n");
			      }
			    unlink $link;
			    symlink $new_target, $link;

			    # We've fixed it, we can stop now.
			    $fixed = 1;
			    last;
			  }
		      }
			
		    # If we haven't fixed it yet...
		    if(!$fixed)
		      {
			foreach my $absolute_alt (@alternates)
			  {
			    # Figure out which OS package owns the linked file
			    my $owned_by = owning_package($absolute_alt);

			    if(defined $owned_by)
			      {
				if(exists $aliases{$owned_by})
				  {
				    if ($Options{'verbose'})
				      {
					print("Target of link $link seems to be owned by one of the aliases for this package ($owned_by)\n",
					      "(Maybe it was excluded?)\n");
				      }
				  }
				else
				  {
				    # Add a line to the root-generating
				    # function to get this file from the
				    # package it comes from.
				    $root_func_lines .=
				      root_func_line($root, $link,
						     $owned_by,
						     $absolute_alt,
						     $rvs_of_pkgs,
						     $pkgs_of_rvs);

				    if($Options{'verbose'})
				      {
					print("Fixing $link, old target:\n",
					      "    $target\n",
					      "Now taken from $owned_by\n");
				      }

				    # Get rid of the link.
				    unlink $link;

				    # We've fixed it.
				    $fixed = 1;
				    last;
				}
			      }
			  }
		      }

		    if(!$fixed)
		      {
			print STDERR ("Warning: bad symbolic link: $link\n", 
				      "                         -> $target\n", 
				      "    (relative, non-local exists, not owned by any OS package)\n");

			# For now we'll delete these.  @@@ Should we
			# instead adjust the link let this kind of
			# file through?
			unlink $link;
		      }
		  }
		else
		  {
		    print STDERR ("Warning: bad symbolic link: $link\n",  
				  "                         -> $target\n",
				  "    (relative, missing)\n");

		    # Get rid of the link.  nothing we can do about this one.
		    unlink $link;
		  }
	      }
	  }
      }

    return $root_func_lines;
  }

# os_path_component

# Generate one or more path components representing the OS we're
# importing packages for.

sub os_path_component ()
  {
    if(defined $Options{'os'})
      {
	return $Options{'os'};
      }

    # Note that there's an assumption that, unless otherwise
    # instructed, the OS we're running on os the OS we're importing
    # for.
    my $result = $Config{'osname'};

    if($result eq "dec_osf")
      {
	$result = "tru64";
      }

    return $result;
  }

# resolve_symlinks(<path>)

# Given a path that may contain some synmlinks, return a list of
# alternate paths to the same file found by resolving symlinks.

sub resolve_symlinks ($ )
  {
    my $path = shift;

    my $sub = undef;

    my @result = ($path);

    while($path ne "/")
      {
	# If this parent is a symlink....
	if(-l $path)
	  {
	    # Get the target of this symbolic link
	    my $target = readlink($path);

	    # The resolved full path.
	    my $resolved;

	    # Is this an absolute link?
	    if($target =~ m|^/|)
	      {
	        $resolved = $target;
	      }
	    else
	      {
		# Form the target of the relative link.
		my $parent = parent_dir($path);
		$resolved = combine_path($parent, $target);

		# Resolve '.' and '..' path components.
		$resolved = canonicalize_path($resolved);
	      }

	    if(defined $sub)
	      {
		# Combine the resolved link with the path components
		# below the link that we've removed so far.
		$resolved = combine_path($resolved, $sub);
	      }

	    # Resurse on the path we just generated, adding all
	    # resolved paths from it that aren't already in our result
	    # to our result.
	    foreach my $resolved2 (resolve_symlinks($resolved))
	      {
		if(scalar(grep {$_ eq $resolved2} @result) == 0)
		  {
		    push @result, $resolved2;
		  }
	      }
	  }

	# Shift the last path component from $path to $sub, or exit
	# the loop if we can't find a path tail.
	if($path =~ m|/([^/]+)$|)
	  {
	    my $tail = $1;
	    if(defined $sub)
	      {
		$sub = combine_path($tail, $sub);
	      }
	    else
	      {
		$sub = $tail;
	      }
	    $path = $PREMATCH;
	  }
	else
	  {
	    last;
	  }
      }

    return @result;
  }

sub rpm_doc_paths (@ )
  {
    my @rpms = @_;

    my $all_rpms = join(" ", @rpms);

    my @result = ();

    if($Options{'from-installed'})
      {
	open RPM_DOC_LIST, "rpm -q -d $all_rpms|";
      }
    else
      {
	open RPM_DOC_LIST, "rpm -q -d -p $all_rpms|";
      }
    while(<RPM_DOC_LIST>)
      {
	chomp;
	push @result, $_;
      }
    close RPM_DOC_LIST;

    return @result;
  }

sub deb_doc_paths (@ )
  {
    print ("Note: With Debian, --excludedocs assumes /usr/share/man and /usr/share/doc\n");

    return ("/usr/share/man", "/usr/share/doc");
  }

sub tru64_doc_paths (@ )
  {
    print ("Note: With Tru64, --excludedocs assumes: /usr/share/man /usr/man /usr/doc /usr/examples\n");

    return ("/usr/share/man", "/usr/man", "/usr/doc", "/usr/examples");
  }

sub pkg_doc_paths (@ )
  {
    if($Options{'pkg-type'} eq "deb")
      {
	return deb_doc_paths(@_);
      }
    elsif($Options{'pkg-type'} eq "rpm")
      {
	return rpm_doc_paths(@_);
      }
    elsif($Options{'pkg-type'} eq "tru64-subset")
      {
	return tru64_doc_paths(@_);
      }

    print STDERR ("Fatal Error: Don't know how to exclude docs with packages ",
		  "of type ", $Options{'pkg-type'}, "\n");
    exit(1);
  }

# dir_is_empty(<directory>)

# Check whether a directory is empty.

sub dir_is_empty ($ )
  {
    my $dir = shift;

    # Not a directory => not an empty directory.
    return 0 if(!-d $dir);

    # Get the contents of the directory
    opendir MAYBE_EMPTY, $dir;
    my @dir_contents = readdir MAYBE_EMPTY;
    closedir MAYBE_EMPTY;

    # More than two entries in the directory => not an empty
    # directory.
    return 0 if(scalar(@dir_contents) > 2);

    # Any directory entries other than . and .. => not an empty
    # directory.
    foreach $kid (@dir_contents)
      {
	return 0 if(($kid ne ".") && ($kid ne ".."));
      }

    # Must be an empty directory.
    return 1;
  }

sub package_root ()
  {
    # If the user specified one explicitly on the command-line:
    if(defined $Options{'package-root'})
      {
	return $Options{'package-root'};
      }

    my $pattern = `vgetconfig pkg2vesta.pl package-root 2>/dev/null`;
    chomp $pattern;
    if(($? == 0) && ($pattern ne ""))
      {
	return $pattern;
      }

    print STDERR ("ERROR: Need a package root.  ".
		  "Use --package-root or set ".
		  "[pkg2vesta.pl]package-root in vesta.cfg\n");
    exit(1);
  }

sub package_pattern ()
  {
    my $root = package_root();

    # If the user specified one explicitly on the command-line:
    if(defined $Options{'package-pattern'})
      {
	return combine_path($root, $Options{'package-pattern'});
      }

    # If a pattern has been specified via vesta.cfg:
    my $pattern = `vgetconfig pkg2vesta.pl package-pattern 2>/dev/null`;
    chomp $pattern;
    if(($? == 0) && ($pattern ne ""))
      {
	return combine_path($root, $pattern);
      }

    # Neither configured nor explicitly specified: use the default
    return combine_path($root, "platforms/\%o/\%v/\%a/components/\%p");
  }

sub work_dir_pattern ()
  {
    my $root = `vgetconfig UserInterface DefaultWorkParent 2>/dev/null`;
    chomp $root;

    my $user_name = getlogin || getpwuid($UID);
    my $user_root = combine_path($root, $user_name);

    # If the user specified one explicitly on the command-line:
    if(defined $Options{'work-dir-pattern'})
      {
	return combine_path($user_root, $Options{'work-dir-pattern'});
      }

    # If a pattern has been specified via vesta.cfg:
    my $pattern = `vgetconfig pkg2vesta.pl work-dir-pattern 2>/dev/null`;
    chomp $pattern;
    if(($? == 0) && ($pattern ne ""))
      {
	return combine_path($user_root, $pattern);
      }

    # Neither configured nor explicitly specified: use the default
    return combine_path($user_root, "\%o-\%v-\%a/\%p");
  }

sub pattern_subst ($$$$$)
  {
    my $path = shift;
    my $arch = shift;
    my $name = shift;
    my $vendor = shift;
    my $os = shift;

    $path =~ s/\%a/$arch/;
    $path =~ s/\%p/$name/;
    $path =~ s/\%v/$vendor/;
    $path =~ s/\%o/$os/;

    return $path;
  }

sub mk_wd_parent ($ )
  {
    my $path = shift;
    my $parent = parent_dir($path);
    if(system("mkdir", "-p", $parent) != 0)
      {
	print STDERR ("error -- couldn't create working directory parent (",
		      $parent, ")\n");
	exit(1);
      }
  }

sub find_wd_of_co($ )
  {
    my $pkg_path = shift;

    my $wd_path;
    my $others_checkout;
    my $user_name = getlogin || getpwuid($UID);
    my $whohas_cmd = vesta_cmd_str("vwhohas", $pkg_path);
    open VWHOHAS, "$whohas_cmd|";
    while(<VWHOHAS>)
      {
	# Split the line into path and name
	my @line = split;

	if($line[1] =~ m|$user_name@|)
	  {
	    my $vattrib_cmd = vesta_cmd_str("vattrib", "-g", "work-dir",
					    $line[0]);
	    $wd_path = `$vattrib_cmd`;
	    chomp $wd_path;
	    if($wd_path eq "")
	      {
		$wd_path = undef;
	      }
	    else
	      {
		my $vattrib_cmd2 = vesta_cmd_str("vattrib", "-g", "new-version",
						 $wd_path);
		$new_ver_path = `$vattrib_cmd2`;
		chomp $new_ver_path;
		if($new_ver_path ne $line[0])
		  {
		    $wd_path = undef;
		  }
		else
		  {
		    wait_for_nfs_directory($wd_path);
		  }
	      }
	  }
	else
	  {
	    $others_checkout = $line[1];
	  }
      }
    close VWHOHAS;

    return wantarray ? ($wd_path, $others_checkout) : $wd_path;
  }

# ----------------------------------------------------------------------
# Main body starts here
# ----------------------------------------------------------------------

# Make sure that some directories we may need are on the PATH.
$ENV{'PATH'} .= ":/bin:/usr/bin:/usr/sbin";

# Determine which types of packages we can handle
if(which("dpkg") && which("dpkg-deb"))
  {
    $Pkg_Types{'deb'} = 1;
  }
if(which("rpm") && which("rpm2cpio") && which("cpio"))
  {
    $Pkg_Types{'rpm'} = 1;
  }
if(which("setld"))
  {
    $Pkg_Types{'tru64-subset'} = 1;
  }

# Packages to convert to Vesta packages providing OS components:
my @pkgs = @ARGV;

if(scalar(@pkgs) == 0)
  {
    print STDERR ("Fatal error: no packages given\n");
    exit(1);
  }

# Check that we're not trying to import multiple different kinds of
# packages (e.g. RPMs and Debian packages).  Also, determine the
# package type from the package files given.

foreach my $pkg (grep { -f $_ } @pkgs)
  {
    my $kind = pkg_file_kind($pkg);
    if(defined $kind)
      {
	if(!defined $Options{'pkg-type'})
	  {
	    $Options{'pkg-type'} = $kind;
	  }
	elsif($kind ne $Options{'pkg-type'})
	  {
	    print STDERR ("Fatal error: $pkg isn't a ", $Options{'pkg-type'},
			  "\n");
	    exit(1);
	  }
      }
  }

# If the user hasn't told us which package type we're using, and we
# couldn't figure it out from the package files given on the command
# line, make a guess.
if(!defined $Options{'pkg-type'})
  {
    if($Config{'osname'} eq "dec_osf")
      {
	print ("Note: Assuming package type is Tru64 subset\n");
	$Options{'pkg-type'} = "tru64-subset";
      }
    elsif($Config{'osname'} eq "linux")
      {
	if(-f "/etc/debian_version")
	  {
	    print ("Note: Assuming package type is Debian\n");
	    $Options{'pkg-type'} = "deb";
	  }
	elsif(-f "/etc/redhat-release")
	  {
	    print ("Note: Assuming package type is RedHat\n");
	    $Options{'pkg-type'} = "rpm";
	  }
      }
  }

# If we haven't determined the package type, or the user specified an
# invalid package type, or we don't think we can handle the specified
# package type, exit with error status.
if(!defined $Options{'pkg-type'})
  {
    print STDERR ("Fatal error: No package type specified, and couldn't come up with a guess\n");
    exit(1);
  }
else
  {
    my $type = $Options{'pkg-type'};
    if(!exists $Pkg_Types{$type})
      {
	print STDERR ("Fatal error: Unkown package type \"$type\"\n");
	exit(1);
      }
    elsif(!$Pkg_Types{$type})
      {
	print STDERR ("Fatal error: Some commands needed for package type \"$type\" are missing\n");
	exit(1);
      }
  }

# Extract information from all the packages we are to import.
my @name_list;
my $arch = $Options{'arch'};
my $name = $Options{'name'};
my $version = $Options{'version'};
my $vendor = $Options{'vendor'};
foreach my $pkg (@pkgs)
  {
    # Get information about this package.
    my $info = pkg_info($pkg);

    # If we couldn't, die.
    if(!defined $info)
      {
	exit(1);
      }

    # Remeber the information about this package.
    $Pkg_Info{$pkg} = $info;

    # Set the architecture or ensure that this matches the
    # architecture of previous packages.
    if(exists $info->{"arch"})
      {
	if(!defined $arch)
	  {
	    $arch = $info->{"arch"};
	  }
	elsif($info->{"arch"} ne $arch)
	  {
	    print STDERR ("Fatal error: attempt to import packages for ",
			  "multiple architectures (",\
			  $info->{"arch"}, " and ", $arch, ")\n");
	    exit(1);
	  }
      }

    # Remember the name of this package.
    if(exists $info->{"name"})
      {
	push @name_list, $info->{"name"};
      }

    # Pick a name and version for the crated Vesta package from the
    # first package that has both.
    if((exists $info->{"name"}) && (exists $info->{"version"}) &&
       (!defined $name))
      {
	$name = $info->{"name"};
	$version = $info->{"version"};
      }

    # Set the vendor or ensure that this matches the vendor of
    # previous packages.
    if(exists $info->{"vendor"} && !defined $Options{'vendor'})
      {
	if(!defined $vendor)
	  {
	    $vendor = $info->{"vendor"};
	  }
	elsif($info->{"vendor"} ne $vendor)
	  {
	    print STDERR ("Fatal error: attempt to import packages for ",
			  "multiple vendors (",\
			  $info->{"vendor"}, " and ", $vendor, ")\n");
	    exit(1);
	  }
      }
  }

my $os = os_path_component();

print("Architecture : ", $arch, "\n");
print("OS           : ", $os, "\n");
print("Vendor       : ", $vendor, "\n");
print("Name         : ", $name, "\n");
print("Version      : ", $version, "\n");

# Figure out where the package will be

my $pkg_path = pattern_subst(package_pattern(),
			     $arch, $name, $vendor, $os);

print("Package path : ", $pkg_path, "\n");

my $branch_path = combine_path($pkg_path, $version);

print("Branch path : ", $branch_path, "\n");

my $new_wd_path = pattern_subst(work_dir_pattern(),
				$arch, $name, $vendor, $os);

print("Working path : ", $new_wd_path, "\n");

# Create a temporary directory into which we will extract contents of
# the packages before we copy them into the Vesta working directory.
my $tempdir = tempdir("pkg2vesta_XXXXX", TMPDIR => 1);

print("Temp area    : ", $tempdir, "\n");

if(!$Options{'test'})
  {
    # Create a file with a message for vcreate/vbranch/vcheckout
    my $msg_file = combine_path($tempdir, "message");
    open MESSAGE, ">$msg_file";
    print MESSAGE (path_tail($PROGRAM_NAME), ", command line arguments:\n",
		   join(" ", @Saved_ARGV));
    close MESSAGE;

    # If the enclosing directory doesn't exist, create it.
    $pkg_parent = parent_dir($pkg_path);
    if(-d $pkg_parent)
      {
	print("parent directory exists -- ok\n");
      }
    else
      {
	print("creating parent directory\n");
	if(system("mkdir", "-p", $pkg_parent) != 0)
	  {
	    print STDERR ("error -- couldn't create package parent directory (",
			  $pkg_parent, ")\n");
	    exit(1);
	  }
	if(vesta_cmd("vattrib", "-a", "type", "package-parent", $pkg_parent) != 0)
	  {
	    print STDERR ("error -- couldn't set type package-parent on parent directory (",
			  $pkg_parent, ")\n");
	    exit(1);
	  }
      }

    # We need to un-set the EDITOR environment variable to force
    # vcreate/vbranch/vcheckout to take a message from standard input.
    delete $ENV{'EDITOR'} if(exists $ENV{'EDITOR'});

    # Create package if needed
    if(-d $pkg_path)
      {
	print("package exists -- ok\n");
      }
    else
      {
	print("creating package\n");
	if(vesta_cmd("vcreate $pkg_path < $msg_file") != 0)
	  {
	    print STDERR ("error -- couldn't create package (",
			  $pkg_path, ")\n");
	    exit(1);
	  }
      }

    my $new_branch = !(-d $branch_path);
    if($new_branch)
      {
	print("creating branch\n");
	if(vesta_cmd("vbranch -O $branch_path < $msg_file") != 0)
	  {
	    print STDERR ("error -- couldn't create branch (",
			  $branch_path, ")\n");
	    exit(1);
	  }
      }
    else
      {
	print("branch exists -- ok\n");
      }

    # Look to see if the branch is already checked out.
    my ($wd_path, $others_checkout) = find_wd_of_co($branch_path);

    if(defined $wd_path)
      {
	# This case is permitted for debugging and for retrying after
	# errors
	print "package already checked out -- ok\n";
      }
    elsif(defined $others_checkout)
      {
	# Someone else has the branch checked out: fail
	print STDERR ("error -- another user (", $others_checkout,
		      ") has the branch checked out");
	exit(1);
      }
    else
      {
	# Proceed with the checkout
	print("checking out branch (to ", $new_wd_path, ")\n");
	mk_wd_parent($new_wd_path);
	if(vesta_cmd("vcheckout -w $new_wd_path $branch_path < $msg_file") != 0)
	  {
	    print STDERR ("error -- couldn't check out branch\n");
	    exit(1);
	  }
      }
  }

# This will be a copy of what we're going to put into the package.
my $tmp_target = combine_path($tempdir, "target");
mkdir $tmp_target;

# Save information about the packages imported.  Exactly what gets
# saved depends on the kind of packages we're importing and what
# information the packages include.

my $pkg_info_files = save_pkg_info($tempdir, $tmp_target, @pkgs);

# This will be the root directory representing a the components of the
# OS filesystem provided by this package.
my $tmp_root = combine_path($tmp_target, "root");
mkdir $tmp_root;

# Execute and record any preinstall shell command(s)
if($Options{'preinstall'} ne "")
  {
    print("running preinstall command(s): ", $Options{'preinstall'}, "\n");
    system("cd $tmp_root; ".$Options{'preinstall'});
    open PRESINT_RECORD, ">$tmp_target/preinstall";
    print PRESINT_RECORD ("# Commands run before extracting the package archive(s)\n");
    print PRESINT_RECORD ($Options{'preinstall'}, "\n");
    close PRESINT_RECORD;
  }

# Extract/copy all the packages into the target root directory.
my @excluded = ();
foreach my $pkg (@pkgs)
  {
    push @excluded, copy_pkg($tmp_root, $pkg);
  }

# If we're to exclude documentation, add the documentation paths to
# the excluded paths.

if($Options{'excludedocs'})
  {
    push @{$Options{'excludepath'}}, pkg_doc_paths(@pkgs);
  }

# Remove any paths to be excluded.  (@@@ If we were importing RPMs,
# this should be unneccassry, as we could simply pass on this on to
# rpm with its --excludepath option.)
my @expanded_exclude = ();
foreach my $path (@{$Options{'excludepath'}})
  {
    my $rpath = combine_path($tmp_root, $path);

    # Does this look like a glob pattern?
    if($path =~ /[\*\?\{\[]/)
      {
	push @expanded_exclude, glob $rpath;
      }
    else
      {
	push @expanded_exclude, $rpath;
      }
  }
foreach my $rpath (@expanded_exclude)
  {
    my $path = remove_prefix($tmp_root, $rpath);

    if(-e $rpath)
      {
	if($Options{'verbose'})
	  {
	    print("Removing ", $path, "\n");
	  }
	push @excluded, $path;
	rm("-rf", $rpath);
      }
  }

# Remove any directories that have been emptied by exclusions.

foreach my $path (@excluded)
  {
    my $parent = parent_dir($path);
    until($parent eq "/")
      {
	my $rparent = combine_path($tmp_root, $parent);
	if(dir_is_empty($rparent))
	  {
	    rmdir $rparent;
	    push @excluded, $parent;
	  }
	else
	  {
	    last;
	  }
	$parent = parent_dir($path);
      }
  }

# Save a record of the paths we excluded.
if(scalar(@excluded) > 0)
  {
    my $exclude_record = combine_path($tmp_target, "excluded");
    open EXCLUDE_RECORD, ">$exclude_record";
    print EXCLUDE_RECORD ("# Paths excluded from Vesta package\n");
    foreach my $path (@excluded)
      {
	print EXCLUDE_RECORD ($path, "\n");
      }
    close EXCLUDE_RECORD;
  }

# Execute any postinstall shell command(s)
if($Options{'postinstall'} ne "")
  {
    print("running postinstall command(s): ", $Options{'postinstall'}, "\n");
    system("cd $tmp_root; ".$Options{'postinstall'});
    open POSTSINT_RECORD, ">$tmp_target/postinstall";
    print POSTSINT_RECORD ("# Commands run after extracting the package archive(s)\n");
    print POSTSINT_RECORD ($Options{'postinstall'}, "\n");
    close POSTSINT_RECORD;
  }

# Find all the names/aliases this will be known as
my @all_aliases = ();
foreach my $pkg (@pkgs) 
  {
    push @all_aliases, $Pkg_Info{$pkg}->{"name"};
    push @all_aliases, @{$Pkg_Info{$pkg}->{"aliases"}};
  }
push @all_aliases, @{$Options{'alias'}};

# Correct symbolic links in the target.
my $root_func_lines = fix_links($tmp_root, @all_aliases);

my $tmp_model = $tmp_target."/build.ves";
open MODEL, ">$tmp_model";
print MODEL ("// ", ("-"x70), "\n",
	     "// ",  "Model generated by ", path_tail($PROGRAM_NAME),
	     ", ", date_and_time(), "\n",
	     "// ", ("-"x70), "\n");

my $pkg_list_text = "";
my $name_list_text = "";
my $name_binding_text = "";
my $readme_list_text = "";
foreach my $pkg (@pkgs)
  {
    $pkg_list_text .= ("\"".path_tail($pkg)."\", ");
    $name_list_text .= ("\"".$Pkg_Info{$pkg}->{"name"}."\", ");
    $name_binding_text .= ("\"".$Pkg_Info{$pkg}->{"name"}."\" = result, ");
    foreach my $alias (@{$Pkg_Info{$pkg}->{"aliases"}})
      {
	$name_list_text .= ("\"".$alias."\", ");
	$name_binding_text .= ("\"".$alias."\" = result, ");
      }
    $readme_list_text .= (path_tail($pkg)."\n");
  }

# Add alias names to the all_names list and the result of the model.

foreach my $alias (@{$Options{'alias'}})
  {
    $name_list_text .= ("\"".$alias."\", ");
    $name_binding_text .= ("\"".$alias."\" = result, ");
  }

print MODEL
  (
   "files\n",
   "  component_info = [ ", $pkg_info_files, " ];\n",
   "import\n",
   "  root = root.ves;\n",
   "{\n",
   "  result = [\n",
   "    // The kind of OS component in this package\n",
   "    kind = \"", $Options{'pkg-type'}, "\",\n",
   "    // Main OS component installed into this package\n",
   "    name = \"", $name, "\",\n",
   "    version = \"", $version, "\",\n",
   "    arch = \"", $arch, "\",\n",
   "    // All OS components installed into this package\n",
   "    pkgs = < ", $pkg_list_text, ">,\n",
   "    // Alternate names for this, based on the other package archives\n",
   "    all_names = < ", $name_list_text, ">,\n",
   "    // Collected information on the imported packages\n",
   "    component_info,\n",
   "    // Function returning filesystem slice\n",
   "    root\n",
   "  ];\n",
   "  return [ ", $name_binding_text, "];\n",
   "}\n"
  );

close MODEL;

my $tmp_root_model = $tmp_target."/root.ves";
open MODEL, ">$tmp_root_model";

print MODEL ("// ", ("-"x70), "\n",
	     "// ",  "Model generated by ", path_tail($PROGRAM_NAME),
	     ", ", date_and_time(), "\n",
	     "// ", ("-"x70), "\n");
print MODEL
  (
   "files\n",
   "  root;\n",
   "{\n",
   # Lines grabbing symlinked files from other packages
   $root_func_lines,
   "  return root;\n",
   "}\n"
  );

close MODEL;

my $tmp_readme = $tmp_target."/README";

open README, ">$tmp_readme";

my $readme_txt =
  "The contents of the root sub-directory were automatically imported".
  "from the following ";

if(($Options{'pkg-type'} eq "deb") &&
   !$Options{'from-installed'})
  {
    $readme_txt .= "Debian package archive(s)";
  }
elsif($Options{'pkg-type'} eq "rpm")
  {
    if($Options{'from-installed'})
      {
	$readme_txt .= "installed RedHat package(s)";
      }
    else
      {
	$readme_txt .= "RedHat package archive(s)";
      }
  }
elsif(($Options{'pkg-type'} eq "tru64-subset") &&
      $Options{'from-installed'})
  {
    $readme_txt .= "Tru64 subset(s)";
  }

print README (wrap("", "", $readme_txt), "\n\n",
	      $readme_list_text, "\n");

$readme_txt =
  "Please consult the following file(s) for information on ".
  "the package(s) and the vendor/packager:";

print README (wrap("", "", $readme_txt), "\n\n",
	      $pkg_info_files, "\n\n");

$readme_txt =
  "If you need support, access to the source code, ".
  "information about licensing terms, or have a bug to report, please ".
  "contact them, *not* whoever imported the package(s) into this Vesta ".
  "package.";

print README (wrap("", "", $readme_txt), "\n");

print README ("\n", ("-"x70), "\n",
	      "Command-line arguments used when importing:\n\n",
	      join(" ", @Saved_ARGV), "\n\n");

close README;

# If we're not doing a dry run, copy everything to the working
# directory and remove the temporary directory.
if(!$Options{'test'})
  {
    # Use vwhohas to find the version reservation stub and vattrib to
    # find the working directory from that.
    my $wd_path = find_wd_of_co($branch_path);

    if(defined $wd_path)
      {
	if($Options{'verbose'})
	  {
	    print("Copying into working directory ($wd_path)\n");
	  }
	# If we think this is GNU cp, use -rL.  Otherwise use -r.
	my $cp_option = "-r";
	if(system("cp --version > /dev/null 2>&1") == 0)
	  {
	    $cp_option = "-rL";
	  }
	unless(system("cp $cp_option $tmp_target/* $wd_path") == 0)
	  {
	    print STDERR ("error -- copying to working directory failed\n",
			  "*Not* deleting $tempdir\n");
	    exit(1);
	  }
	if($Options{'verbose'})
	  {
	    print("Deleting $tempdir\n");
	  }
	rm("-rf", $tempdir);
      }
    else
      {
	print STDERR ("error -- couldn't find working directory\n",
		      "*Not* deleting $tempdir\n");
	exit(1);
      }
  }
