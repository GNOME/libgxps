noinst_LTLIBRARIES = libgxpstools.la

libgxpstools_la_SOURCES = \
	gxps-converter.c	\
	gxps-converter.h	\
	gxps-image-converter.c	\
	gxps-image-converter.h	\
	gxps-image-writer.c	\
	gxps-image-writer.h

libgxpstools_la_CPPFLAGS = \
	-I$(top_srcdir)		\
	$(AM_CPPFLAGS)

libgxpstools_la_CFLAGS = \
	$(GXPS_CFLAGS)		\
	$(WARN_CFLAGS)		\
	$(AM_CFLAGS)

libgxpstools_la_LIBADD = \
	$(top_builddir)/libgxps/libgxps.la
