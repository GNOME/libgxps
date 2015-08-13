# *****************************************************************************
# Versioning
# *****************************************************************************

m4_define([gxps_major_version],[0])
m4_define([gxps_minor_version],[2])
m4_define([gxps_micro_version],[3])
m4_define([gxps_extra_version],[])
m4_define([gxps_version],[gxps_major_version.gxps_minor_version.gxps_micro_version()gxps_extra_version])

# Libtool versioning. The backend and view libraries have separate versions.
# Before making a release, the libtool version should be modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A

# Libtool version
m4_define([gxps_lt_current],[4])
m4_define([gxps_lt_revision],[0])
m4_define([gxps_lt_age],[2])
m4_define([gxps_lt_version_info],[gxps_lt_current:gxps_lt_revision:gxps_lt_age])
m4_define([gxps_lt_current_minus_age],[m4_eval(gxps_lt_current - gxps_lt_age)])

# *****************************************************************************

AC_PREREQ([2.57])
AC_INIT([libgxps],[gxps_version],[http://bugzilla.gnome.org/enter_bug.cgi?product=libgxps],[libgxps])
AM_INIT_AUTOMAKE([1.10 foreign dist-bzip2 no-dist-gzip])

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])

if test -z "$enable_maintainer_mode"; then
  enable_maintainer_mode=yes
fi
AM_MAINTAINER_MODE([enable])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_LIBTOOL
AC_PROG_CC
AC_ISC_POSIX
AC_PROG_CC_STDC
AC_STDC_HEADERS
AC_C_BIGENDIAN
LT_LIB_M

GNOME_MAINTAINER_MODE_DEFINES
GNOME_COMPILE_WARNINGS
GNOME_CXX_WARNINGS

AC_ARG_ENABLE(debug,
              [AS_HELP_STRING([--enable-debug],
                              [Turn on debugging])],,
              [enable_debug=no])

if test "x$enable_debug" = "xyes"; then
   GXPS_DEBUG_CFLAGS="-DGXPS_ENABLE_DEBUG"
fi
AC_SUBST(GXPS_DEBUG_CFLAGS)

dnl Specify required versions of dependencies
GLIB_REQUIRED=2.24
CAIRO_REQUIRED=1.10.0
LIBARCHIVE_REQUIRED=2.8.0

PKG_CHECK_MODULES(GXPS,
                  glib-2.0 >= $GLIB_REQUIRED
		  gobject-2.0 >= $GLIB_REQUIRED
		  gio-2.0 >= $GLIB_REQUIRED
		  cairo >= $CAIRO_REQUIRED
		  libarchive >= $LIBARCHIVE_REQUIRED
		  freetype2)

AC_SUBST(GXPS_CFLAGS)
AC_SUBST(GXPS_LIBS)
AC_SUBST(GLIB_REQUIRED)
AC_SUBST(CAIRO_REQUIRED)
AC_SUBST(LIBARCHIVE_REQUIRED)

AC_CHECK_FUNC(localtime_r, AC_DEFINE(HAVE_LOCALTIME_R, 1, [Defines if localtime_r is available on your system]))

dnl libjpeg
AC_ARG_WITH(libjpeg,
            [AC_HELP_STRING([--without-libjpeg],
                            [disable JPEG])])
if test x$with_libjpeg != xno && test -z "$LIBJPEG"; then
  AC_CHECK_LIB(jpeg, jpeg_destroy_decompress,
               jpeg_ok=yes,
               jpeg_ok=no
               AC_MSG_WARN(*** JPEG will not be built (JPEG library not found) ***))
  if test "$jpeg_ok" = yes; then
    AC_MSG_CHECKING([for jpeglib.h])
    AC_TRY_CPP(
[#include <stdio.h>
#undef PACKAGE
#undef VERSION
#undef HAVE_STDLIB_H
#include <jpeglib.h>],
        jpeg_ok=yes,
        jpeg_ok=no)
    AC_MSG_RESULT($jpeg_ok)
    if test "$jpeg_ok" = yes; then
      LIBJPEG='-ljpeg'
    else
        AC_MSG_WARN(*** JPEG loader will not be built (JPEG header file not found) ***)
    fi
  fi

  if test x$with_libjpeg != xno && test -z "$LIBJPEG"; then
     AC_MSG_ERROR([
*** Checks for JPEG failed. You can build without it by passing
*** --without-libjpeg to configure])
  else
    AC_DEFINE(HAVE_LIBJPEG, [1], [Have JPEG library])
  fi
fi

AM_CONDITIONAL(HAVE_LIBJPEG, test ! -z "$LIBJPEG")
AC_SUBST(LIBJPEG)

dnl libtiff
AC_ARG_WITH(libtiff,
            [AC_HELP_STRING([--without-libtiff],
	                    [disable Tiff])])
if test x$with_libtiff != xno && test -z "$LIBTIFF"; then
   AC_CHECK_HEADERS([tiffio.h],enable_tiff=yes,enable_tiff=no,)
   if test "x$enable_tiff" = "xyes"; then
      AC_CHECK_LIB([tiff],TIFFClientOpen,enable_tiff=yes,enable_tiff=no,"-lz")
      AC_CHECK_LIB([tiff],TIFFReadRGBAImageOriented,enable_tiff=yes,enable_tiff=no,"-lz")
   fi
   if test "x$enable_tiff" = "xyes"; then
     LIBTIFF='-ltiff'
     AC_DEFINE(HAVE_LIBTIFF, [1], [Have Tiff library])
   else
     AC_MSG_ERROR([
*** Checks for Tiff failed. You can build without it by passing
*** --without-libtiff to configure])
   fi
fi

AC_SUBST(LIBTIFF)

dnl libcms2
AC_ARG_WITH(liblcms2,
            [AC_HELP_STRING([--without-liblcms2],
	                    [disable Little CMS 2])])
if test x$with_liblcms2 != xno; then
   PKG_CHECK_MODULES(LCMS2, lcms2, [enable_lcms2="yes"], [enable_lcms2="no"])
   if test "x$enable_lcms2" = "xyes"; then
     AC_DEFINE(HAVE_LIBLCMS2, [1], [Have Little CMS 2 library])
   else
     AC_MSG_ERROR([
*** Checks for Little CMS 2 failed. You can build without it by passing
*** --without-liblcms2 to configure])
   fi
fi

AC_SUBST(LCMS2_CFLAGS)
AC_SUBST(LCMS2_LIBS)

AC_DEFINE(_FILE_OFFSET_BITS, 64, [Enable LFS])

dnl Test
AC_ARG_ENABLE(test,
              [AC_HELP_STRING([--enable-test], [Compile tests])],enable_test="$enableval",enable_test=yes)

if test "x$enable_test" = "xyes"; then
   PKG_CHECK_MODULES(GTK, gtk+-3.0, enable_test=yes,enable_test=no)

  if test "x$enable_test" != "xyes"; then
    AC_MSG_WARN("Tests disabled since required GTK+ library was not found")
  fi
fi

AM_CONDITIONAL(ENABLE_TEST, test x$enable_test = xyes)

dnl Tools

dnl libpng
PKG_CHECK_MODULES(LIBPNG, libpng, [have_libpng="yes"], [have_libpng="no"])

AM_CONDITIONAL(HAVE_LIBPNG, test x$have_libpng = xyes)
if test x$have_libpng = xyes; then
   AC_DEFINE(HAVE_LIBPNG, [1], [Have PNG library])
fi

AC_SUBST(LIBPNG_CFLAGS)
AC_SUBST(LIBPNG_LIBS)

dnl Cairo PDF
PKG_CHECK_MODULES(CAIRO_PDF, cairo-pdf, [have_cairo_pdf="yes"], [have_cairo_pdf="no"])

AM_CONDITIONAL(HAVE_CAIRO_PDF, test x$have_cairo_pdf = xyes)

AC_SUBST(CAIRO_PDF_CFLAGS)
AC_SUBST(CAIRO_PDF_LIBS)

dnl Cairo PostScript
PKG_CHECK_MODULES(CAIRO_PS, cairo-ps, [have_cairo_ps="yes"], [have_cairo_ps="no"])

AM_CONDITIONAL(HAVE_CAIRO_PS, test x$have_cairo_ps = xyes)

AC_SUBST(CAIRO_PS_CFLAGS)
AC_SUBST(CAIRO_PS_LIBS)

dnl Cairo SVG
PKG_CHECK_MODULES(CAIRO_SVG, cairo-svg, [have_cairo_svg="yes"], [have_cairo_svg="no"])

AM_CONDITIONAL(HAVE_CAIRO_SVG, test x$have_cairo_svg = xyes)

AC_SUBST(CAIRO_SVG_CFLAGS)
AC_SUBST(CAIRO_SVG_LIBS)

dnl gtk-doc
GTK_DOC_CHECK([1.14],[--flavour no-tmpl])

AC_ARG_ENABLE(man,
              [AC_HELP_STRING([--enable-man],
                              [generate tools man pages [default=no]])],
                              enable_man=yes, enable_man=no)

AC_PATH_PROG([XSLTPROC], [xsltproc])
AM_CONDITIONAL(ENABLE_MAN, test x$enable_man != xno)

dnl GObject instrospection
GOBJECT_INTROSPECTION_CHECK([0.10.1])

# **********
# Versioning
# **********

AC_SUBST([GXPS_MAJOR_VERSION],[gxps_major_version])
AC_SUBST([GXPS_MINOR_VERSION],[gxps_minor_version])
AC_SUBST([GXPS_MICRO_VERSION],[gxps_micro_version])

AC_SUBST([GXPS_LT_VERSION_INFO],[gxps_lt_version_info])
AC_SUBST([GXPS_LT_CURRENT_MINUS_AGE],[gxps_lt_current_minus_age])

# *****************************************************************************
# *****************************************************************************

AC_CONFIG_FILES([
Makefile
libgxps.pc
libgxps/Makefile
libgxps/gxps-version.h
tools/Makefile
test/Makefile
docs/Makefile
docs/reference/Makefile
docs/reference/version.xml
docs/tools/Makefile
])

AC_OUTPUT
