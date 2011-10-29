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

bin_PROGRAMS =

tools_cppflags = \
	-I$(top_srcdir)	\
	$(AM_CPPFLAGS)

tools_cflags = \
	$(GXPS_CFLAGS)	\
	$(WARN_CFLAGS)
	$(AM_CFLAGS)

if HAVE_LIBPNG
bin_PROGRAMS += xpstopng

xpstopng_SOURCES = \
	gxps-converter-main.c	\
	gxps-png-converter.c	\
	gxps-png-converter.h	\
	gxps-png-writer.c	\
	gxps-png-writer.h

xpstopng_CPPFLAGS = \
	$(tools_cppflags)				\
	-DCONVERTER_TYPE=GXPS_TYPE_PNG_CONVERTER	\
	-DCONVERTER_HEADER=gxps-png-converter.h

xpstopng_CFLAGS = \
	$(tools_cflags) 	\
	$(LIBPNG_CFLAGS)

xpstopng_LDADD = \
	libgxpstools.la		\
	$(LIBPNG_LIBS)
endif # HAVE_LIBPNG