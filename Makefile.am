SUBDIRS = libgxps

if ENABLE_TEST
SUBDIRS += test
endif

SUBDIRS += docs

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgxps.pc

EXTRA_DIST = \
	$(pkgconfig_DATA).in

DISTCLEANFILES = \
	$(pkgconfig_DATA)

DISTCHECK_CONFIGURE_FLAGS = 		\
	--enable-gtk-doc		\
	--disable-maintainer-mode	\
	--enable-introspection
