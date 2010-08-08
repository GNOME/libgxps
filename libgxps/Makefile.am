lib_LTLIBRARIES = libgxps.la

NOINST_H_FILES = \
	gxps-archive.h		\
	gxps-fonts.h		\
	gxps-images.h		\
	gxps-parse-utils.h	\
	gxps-private.h

INST_H_FILES = \
	gxps.h		\
	gxps-document.h	\
	gxps-error.h	\
	gxps-file.h	\
	gxps-page.h

libgxpsincludedir = $(includedir)/libgxps
libgxpsinclude_HEADERS = $(INST_H_FILES)

libgxps_la_SOURCES = \
	gxps-archive.c		\
	gxps-document.c		\
	gxps-error.c		\
	gxps-file.c		\
	gxps-fonts.c		\
	gxps-images.c		\
	gxps-page.c		\
	gxps-parse-utils.c	\
	$(NOINST_H_FILES)	\
	$(INST_H_FILES)

libgxps_la_CPPFLAGS = \
	-I$(top_builddir)	\
	-I$(srcdir)		\
	-I$(top_srcdir)		\
	-DGXPS_COMPILATION	\
	$(AM_CPPFLAGS)

libgxps_la_CFLAGS = \
	$(GXPS_CFLAGS)	\
	$(WARN_CFLAGS)	\
	$(AM_CFLAGS)

libgxps_la_LDFLAGS = \
	-version-info $(GXPS_LT_VERSION_INFO)   \
	-no-undefined				\
	-export-symbols-regex "^gxps_*"		\
	$(AM_LDFLAGS)

libgxps_la_LIBADD = \
	$(GXPS_LIBS)	\
	$(LIBJPEG)	\
	$(LIBTIFF)
