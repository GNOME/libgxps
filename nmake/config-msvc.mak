# NMake Makefile portion for enabling features for Windows builds

# GLib is required for all utility programs and tests
GXPS_MAIN_LIBS = gio-2.0.lib gobject-2.0.lib glib-2.0.lib cairo.lib archive.lib freetype.lib

# libpng library
LIBPNG_LIB = libpng16.lib

# libjpeg library
LIBJPEG_LIB = jpeg.lib

# Please do not change anything beneath this line unless maintaining the NMake Makefiles
# Bare minimum features and sources built into GXPS on Windows
GXPS_DEFINES = /DGXPS_COMPILATION

GXPS_CFLAGS = \
	/FImsvc_recommended_pragmas.h \
	/I$(PREFIX)\include\glib-2.0 \
	/I$(PREFIX)\include\gio-win32-2.0 \
	/I$(PREFIX)\include\cairo \
	/I$(PREFIX)\lib\glib-2.0\include

GXPS_SOURCES = \
	$(GXPS_BASE_SOURCES) \
	$(GXPS_BASE_NOINST_H_FILES)

GXPS_HEADERS = \
	$(GXPS_BASE_INST_H_FILES)

# Minimal set of (system) libraries needed for the GXPS DLL
GXPS_DEP_LIBS = user32.lib Advapi32.lib $(GXPS_MAIN_LIBS)

# We build the GXPS DLL/LIB at least
GXPS_LIBS = $(CFG)\$(PLAT)\gxps.lib $(CFG)\$(PLAT)\gxpstools.lib

# Note: All the utility and test programs require GLib support to be present!
GXPS_TESTS =
GXPS_TESTS_DEP_LIBS = $(GXPS_MAIN_LIBS)

GXPS_TOOLS =

# Use libtool-style DLL names, if desired
!if "$(LIBTOOL_DLL_NAME)" == "1"
GXPS_DLL_FILENAME = $(CFG)\$(PLAT)\libgxps-0
!else
GXPS_DLL_FILENAME = $(CFG)\$(PLAT)\gxps-vs$(VSVER)
!endif

# Enable libpng if desired
!if "$(LIBPNG)" == "1"
GXPS_DEFINES = $(GXPS_DEFINES) /DHAVE_LIBPNG=1
GXPS_DEP_LIBS = $(GXPS_DEP_LIBS) $(LIBPNG_LIB)
GXPS_CFLAGS = \
	$(GXPS_CFLAGS) \
	/I$(PREFIX)\include\libpng
!endif

!if "$(LIBJPEG)" == "1"
GXPS_DEFINES = $(GXPS_DEFINES) /DHAVE_LIBJPEG=1
GXPS_DEP_LIBS = $(GXPS_DEP_LIBS) $(LIBJPEG_LIB)
!endif

# Enable cairo-pdf if desired
!if "$(CAIRO_PDF)" == "1"
GXPS_DEFINES = $(GXPS_DEFINES) /DHAVE_CAIRO_PDF=1
GXPS_TOOLS = $(GXPS_TOOLS) $(CFG)\$(PLAT)\xpstopdf.exe
!endif

# Enable cairo-ps if desired
!if "$(CAIRO_PS)" == "1"
GXPS_DEFINES = $(GXPS_DEFINES) /DHAVE_CAIRO_PS=1
!endif

# Enable cairo-svg if desired
!if "$(CAIRO_SVG)" == "1"
GXPS_DEFINES = $(GXPS_DEFINES) /DHAVE_CAIRO_SVG=1
!endif

EXTRA_TARGETS =

GXPS_TESTS =

GXPS_LIB_CFLAGS = $(GXPS_CFLAGS) /D_GXPS_EXTERN="__declspec (dllexport) extern"
