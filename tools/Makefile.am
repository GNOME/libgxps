include Makefile.sources

noinst_LTLIBRARIES = libgxpstools.la

libgxpstools_la_SOURCES = $(LIBGXPS_TOOLS_SOURCES)

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
	$(WARN_CFLAGS) \
	$(AM_CFLAGS)

if HAVE_LIBPNG
bin_PROGRAMS += xpstopng

xpstopng_SOURCES = $(XPS_TO_PNG_SOURCES)

xpstopng_CPPFLAGS = \
	$(tools_cppflags)				\
	-DCONVERTER_TYPE=GXPS_TYPE_PNG_CONVERTER	\
	-DCONVERTER_HEADER=gxps-png-converter.h

xpstopng_CFLAGS = \
	$(tools_cflags) 	\
	$(LIBPNG_CFLAGS)

xpstopng_LDADD = \
	libgxpstools.la		\
	$(LIBM)                 \
	$(LIBPNG_LIBS)
endif # HAVE_LIBPNG

if HAVE_LIBJPEG
bin_PROGRAMS += xpstojpeg

xpstojpeg_SOURCES = $(XPS_TO_JPEG_SOURCES)

xpstojpeg_CPPFLAGS = \
	$(tools_cppflags)				\
	-DCONVERTER_TYPE=GXPS_TYPE_JPEG_CONVERTER	\
	-DCONVERTER_HEADER=gxps-jpeg-converter.h

xpstojpeg_CFLAGS = \
	$(tools_cflags)

xpstojpeg_LDADD = \
	libgxpstools.la		\
	$(LIBM)                 \
	$(LIBJPEG)
endif # HAVE_LIBJPEG

if HAVE_CAIRO_PDF
bin_PROGRAMS += xpstopdf

xpstopdf_SOURCES = $(XPS_TO_PDF_SOURCES)

xpstopdf_CPPFLAGS = \
	$(tools_cppflags)				\
	-DCONVERTER_TYPE=GXPS_TYPE_PDF_CONVERTER	\
	-DCONVERTER_HEADER=gxps-pdf-converter.h

xpstopdf_CFLAGS = \
	$(tools_cflags)		\
	$(CAIRO_PDF_CFLAGS)

xpstopdf_LDADD = \
	libgxpstools.la		\
	$(LIBM)                 \
	$(CAIRO_PDF_LIBS)
endif # HAVE_CAIRO_PDF

if HAVE_CAIRO_PS
bin_PROGRAMS += xpstops

xpstops_SOURCES = $(XPS_TO_PS_SOURCES)

xpstops_CPPFLAGS = \
	$(tools_cppflags)				\
	-DCONVERTER_TYPE=GXPS_TYPE_PS_CONVERTER	\
	-DCONVERTER_HEADER=gxps-ps-converter.h

xpstops_CFLAGS = \
	$(tools_cflags)		\
	$(CAIRO_PS_CFLAGS)

xpstops_LDADD = \
	libgxpstools.la		\
	$(LIBM)                 \
	$(CAIRO_PS_LIBS)
endif # HAVE_CAIRO_PS

if HAVE_CAIRO_SVG
bin_PROGRAMS += xpstosvg

xpstosvg_SOURCES = $(XPS_TO_SVG_SOURCES)

xpstosvg_CPPFLAGS = \
	$(tools_cppflags)				\
	-DCONVERTER_TYPE=GXPS_TYPE_SVG_CONVERTER	\
	-DCONVERTER_HEADER=gxps-svg-converter.h

xpstosvg_CFLAGS = \
	$(tools_cflags)		\
	$(CAIRO_SVG_CFLAGS)

xpstosvg_LDADD = \
	libgxpstools.la		\
	$(LIBM)                 \
	$(CAIRO_SVG_LIBS)
endif # HAVE_CAIRO_SVG
