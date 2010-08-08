noinst_PROGRAMS = test-gxps

test_gxps_SOURCES = test-gxps.c
test_gxps_CPPFLAGS = \
	-I$(top_srcdir)		\
	-I$(top_srcdir)/libgxps	\
	$(AM_CPPFLAGS)

test_gxps_CFLAGS = \
	$(GXPS_CFLAGS)	\
	$(GTK_CFLAGS)	\
	$(WARN_CFLAGS)	\
	$(AM_CFLAGS)

test_gxps_LDADD = \
	$(top_builddir)/libgxps/libgxps.la	\
	$(GTK_LIBS)
