Instructions for building GXPS on Visual Studio
===================================================
Building the GXPS DLL on Windows is now also supported using Visual Studio
versions 2008 through 2015, in both 32-bit and 64-bit (x64) flavors, via NMake
Makefiles.

The following are instructions for performing such a build, as there is a
number of build configurations supported for the build.  Note that for all
build configurations, the OpenType and Simple TrueType layout (fallback)
backends are enabled, as well as the Uniscribe platform shaper, and this
is the base configuration that is built if no options (see below) are
specified.  A 'clean' target is provided-it is recommended that one cleans
the build and redo the build if any configuration option changed.  An
'install' target is also provided to copy the built items in their appropriate
locations under $(PREFIX), which is described below.

Invoke the build by issuing the command:
nmake /f Makefile.vc CFG=[release|debug] [PREFIX=...] <option1=1 option2=1 ...>
where:

CFG: Required.  Choose from a release or debug build.  Note that 
     all builds generate a .pdb file for each .dll and .exe built--this refers
     to the C/C++ runtime that the build uses.

PREFIX: Optional.  Base directory of where the third-party headers, libraries
        and needed tools can be found, i.e. headers in $(PREFIX)\include,
        libraries in $(PREFIX)\lib and tools in $(PREFIX)\bin.  If not
        specified, $(PREFIX) is set as $(srcroot)\..\vs$(X)\$(platform), where
        $(platform) is win32 for 32-bit builds or x64 for 64-bit builds, and
        $(X) is the short version of the Visual Studio used, as follows:
        2008: 9
        2010: 10
        2012: 11
        2013: 12
        2015: 14

Explanation of options, set by <option>=1:
------------------------------------------
PYTHON: Full path to the Python interpretor to be used, if it is not in %PATH%.

PERL: Full path to the PERL interpretor to be used, if it is not in %PATH%.

LIBTOOL_DLL_NAME: Enable libtool-style DLL names.
