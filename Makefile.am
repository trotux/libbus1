# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

pkgincludedir=$(includedir)/org.bus1
pkgconfiglibdir=$(libdir)/pkgconfig

BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST =
pkginclude_HEADERS =
pkgconfiglib_DATA =
noinst_LIBRARIES =
lib_LIBRARIES =
bin_PROGRAMS =
check_PROGRAMS =
TESTS =
default_tests =

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# libbus1.a

noinst_LIBRARIES += \
	libbus1.a

pkginclude_HEADERS += \
	src/org.bus1/b1.h \
	src/org.bus1/b1-peer.h \
	src/org.bus1/c-macro.h \
	src/org.bus1/c-shared.h \
	src/org.bus1/c-sys.h

libbus1_a_SOURCES = \
	src/b1.c \
	src/b1-peer.c \
	src/bus1-client.c \
	src/bus1-client.h \
	src/c-sys.c \
	src/libbus1.sym \
	src/linux/bus1.h

libbus1_a_CFLAGS = \
	$(AM_CFLAGS) \
	$(CVARIANT_CFLAGS) \
	$(CRBTREE_CFLAGS)

# ------------------------------------------------------------------------------
# libbus1.so.0

libbus1.so.0: libbus1.a $(top_srcdir)/src/libbus1.sym
	$(AM_V_CCLD)$(CCLD) $(AM_CFLAGS) $(AM_LDFLAGS) \
	-shared \
	-Wl,-soname=$@ \
	-Wl,--version-script=$(top_srcdir)/src/libbus1.sym \
	-Wl,--whole-archive libbus1.a -Wl,--no-whole-archive \
	-o $@

CLEANFILES += \
	libbus1.so.0

all-local: libbus1.so.0

install-exec-hook:
	$(MKDIR_P) $(DESTDIR)$(libdir)
	cp libbus1.so.0 $(DESTDIR)$(libdir)
	$(AM_V_LN)ln -sf libbus1.so.0 $(DESTDIR)$(libdir)/libbus1.so

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/libbus1.so.0

%.pc: %.pc.in
	$(AM_V_GEN)$(SED) \
		-e 's,@includedir\@,$(includedir),g' \
		-e 's,@libdir\@,$(libdir),g' \
		-e 's,@VERSION\@,$(VERSION),g' \
		$< > $@

pkgconfiglib_DATA += \
	src/libbus1.pc

EXTRA_DIST += \
	src/libbus1.pc.in

CLEANFILES += \
	src/libbus1.pc

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.bus1.busctl

org_bus1_busctl_SOURCES = \
	src/busctl.c

org_bus1_busctl_CFLAGS = \
	$(AM_CFLAGS)

org_bus1_busctl_LDADD = \
	libbus1.so.0 \
	$(CVARIANT_LIBS) \
	$(CRBTREEE_LIBS)

# ------------------------------------------------------------------------------
# test-macro

default_tests += \
	test-macro

test_macro_SOURCES = \
	src/test-macro.c

test_macro_CFLAGS = \
	$(AM_CFLAGS)

test_macro_LDADD = \
	libbus1.a \
	$(CVARIANT_LIBS) \
	$(CRBTREEE_LIBS)

# ------------------------------------------------------------------------------
# test-perf

default_tests += \
	test-perf

test_perf_SOURCES = \
	src/test-perf.c

test_perf_CFLAGS = \
	$(AM_CFLAGS)

test_perf_LDADD = \
	libbus1.a \
	$(CVARIANT_LIBS) \
	$(CRBTREEE_LIBS)

# ------------------------------------------------------------------------------
# test-shared

default_tests += \
	test-shared

test_shared_SOURCES = \
	src/test-shared.c

test_shared_CFLAGS = \
	$(AM_CFLAGS)

test_shared_LDADD = \
	libbus1.a \
	$(CVARIANT_LIBS) \
	$(CRBTREEE_LIBS)

# ------------------------------------------------------------------------------
# test suite

AM_TESTS_ENVIRONMENT = LD_LIBRARY_PATH=$(abs_builddir)
check_PROGRAMS += $(default_tests)
TESTS += $(default_tests)

# ------------------------------------------------------------------------------
# check "make install" directory tree

install-tree: all
	rm -rf $(abs_srcdir)/install-tree
	$(MAKE) install DESTDIR=$(abs_srcdir)/install-tree
	tree $(abs_srcdir)/install-tree
.PHONY: install-tree
