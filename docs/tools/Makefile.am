man_MANS =

if HAVE_LIBPNG
man_MANS += xpstopng.1
endif

if HAVE_LIBJPEG
man_MANS += xpstojpeg.1
endif

if HAVE_CAIRO_PDF
man_MANS += xpstopdf.1
endif

if HAVE_CAIRO_PS
man_MANS += xpstops.1
endif

if HAVE_CAIRO_SVG
man_MANS += xpstosvg.1
endif

.xml.1:
	$(AM_V_GEN)@XSLTPROC@ -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<


MAINTAINERCLEANFILES = $(man_MANS)
EXTRA_DIST = $(man_MANS)