SUBDIRS = libgxps

if ENABLE_TEST
SUBDIRS += test
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgxps.pc

EXTRA_DIST = \
	$(pkgconfig_DATA).in

DISTCLEANFILES = \
	$(pkgconfig_DATA)
