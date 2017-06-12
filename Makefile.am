SUBDIRS = libgxps tools

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
	--enable-man			\
	--disable-maintainer-mode	\
	--enable-introspection

MAINTAINERCLEANFILES = \
	ChangeLog \
	$(srcdir)/INSTALL \
	$(srcdir)/aclocal.m4 \
	$(srcdir)/config.guess \
	$(srcdir)/config.h.in \
	$(srcdir)/config.sub \
	$(srcdir)/configure \
	$(srcdir)/depcomp \
	$(srcdir)/install-sh \
	$(srcdir)/ltmain.sh \
	$(srcdir)/missing \
	$(srcdir)/gtk-doc.make \
	`find "$(srcdir)" -type f -name Makefile.in -print`

distclean-local:
	if test "$(srcdir)" = "."; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	$(AM_V_GEN) if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log -M -C --name-status --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog
