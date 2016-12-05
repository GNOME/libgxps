lib_LTLIBRARIES = libgxps.la

include Makefile.sources

libgxpsincludedir = $(includedir)/libgxps
libgxpsinclude_HEADERS = $(GXPS_BASE_INST_H_FILES)

libgxps_la_SOURCES = \
	$(GXPS_BASE_SOURCES)		\
	$(GXPS_BASE_NOINST_H_FILES)	\
	$(GXPS_BASE_INST_H_FILES)

libgxps_la_CPPFLAGS = \
	-I$(top_builddir)	\
	-I$(srcdir)		\
	-I$(top_srcdir)		\
	-DG_LOG_DOMAIN=\"GXPS\"	\
	-DGXPS_COMPILATION	\
	$(AM_CPPFLAGS)

libgxps_la_CFLAGS = \
	$(GXPS_CFLAGS)		\
	$(GXPS_DEBUG_CFLAGS)	\
	$(LCMS2_CFLAGS)		\
	$(LIBPNG_CFLAGS)	\
	$(WARN_CFLAGS)		\
	$(AM_CFLAGS)		\
	$(GXPS_HIDDEN_VISIBILITY_CFLAGS)

libgxps_la_LDFLAGS = \
	-version-info $(GXPS_LT_VERSION_INFO)   \
	-no-undefined				\
	-export-dynamic				\
	$(AM_LDFLAGS)

libgxps_la_LIBADD = \
	$(GXPS_LIBS)	\
	$(LCMS2_LIBS)	\
	$(LIBPNG_LIBS)	\
	$(LIBJPEG)	\
	$(LIBM)         \
	$(LIBTIFF)

EXTRA_DIST = \
	gxps-version.h.in

DISTCLEANFILES = \
	gxps-version.h

CLEANFILES =

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all --identifier-prefix=GXPS --symbol-prefix=gxps
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION
introspection_sources = $(filter-out $(NOINST_H_FILES) gxps-archive.c gxps-fonts.c gxps-images.c gxps-parse-utils.c gxps-version.h, $(libgxps_la_SOURCES))

GXPS-0.1.gir: libgxps.la
GXPS_0_1_gir_INCLUDES = GObject-2.0 Gio-2.0 cairo-1.0
GXPS_0_1_gir_CFLAGS = $(libgxps_la_CPPFLAGS) $(GXPS_CFLAGS)
GXPS_0_1_gir_LIBS = libgxps.la
GXPS_0_1_gir_FILES = $(addprefix $(srcdir)/, $(introspection_sources))

INTROSPECTION_GIRS += GXPS-0.1.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif
