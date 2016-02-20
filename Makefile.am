# ------------------------------------------------------------------------------
# autotools

SHELL=bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

SUBDIRS = .

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

pkgincludedir=$(includedir)/org.bus1
pkgconfiglibdir=$(libdir)/pkgconfig

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES =
EXTRA_DIST =
BUILT_SOURCES =
pkginclude_HEADERS=
pkgconfiglib_DATA =
noinst_LIBRARIES =
lib_LTLIBRARIES =
bin_PROGRAMS =
noinst_PROGRAMS =
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
# libbus1.so

%.pc: %.pc.in
	$(AM_V_GEN)$(SED) \
		-e 's,@includedir\@,$(includedir),g' \
		-e 's,@libdir\@,$(libdir),g' \
		-e 's,@VERSION\@,$(VERSION),g' \
		$< > $@

lib_LTLIBRARIES += \
	libbus1.la

pkginclude_HEADERS += \
	src/org.bus1/b1.h \
	src/org.bus1/c-macro.h \
	src/org.bus1/c-rbtree.h \
	src/org.bus1/c-shared.h \
	src/org.bus1/c-sys.h

pkgconfiglib_DATA += \
	src/libbus1.pc

libbus1_la_SOURCES = \
	src/libbus1.sym \
	src/b1.c \
	src/c-rbtree.c \
	src/c-sys.c

libbus1_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-info $(LIBRARY_CURRENT):$(LIBRARY_REVISION):$(LIBRARY_AGE) \
	-Wl,--version-script=$(top_srcdir)/src/libbus1.sym

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
	libbus1.la

# ------------------------------------------------------------------------------
# test-macro

default_tests += \
	test-macro

test_macro_SOURCES = \
	src/test-macro.c

test_macro_CFLAGS = \
	$(AM_CFLAGS)

test_macro_LDADD = \
	libbus1.la

# ------------------------------------------------------------------------------
# test-perf

default_tests += \
	test-perf

test_perf_SOURCES = \
	src/test-perf.c

test_perf_CFLAGS = \
	$(AM_CFLAGS)

test_perf_LDADD = \
	libbus1.la

# ------------------------------------------------------------------------------
# test-rbtree

default_tests += \
	test-rbtree

test_rbtree_SOURCES = \
	src/test-rbtree.c

test_rbtree_CFLAGS = \
	$(AM_CFLAGS)

test_rbtree_LDADD = \
	libbus1.la

# ------------------------------------------------------------------------------
# test-shared

default_tests += \
	test-shared

test_shared_SOURCES = \
	src/test-shared.c

test_shared_CFLAGS = \
	$(AM_CFLAGS)

test_shared_LDADD = \
	libbus1.la

# ------------------------------------------------------------------------------
# test suite

check_PROGRAMS += $(default_tests)
TESTS += $(default_tests)
noinst_PROGRAMS += $(default_tests) # build as part of 'make all'

# ------------------------------------------------------------------------------
# 'make install-tree'

install-tree: all
	rm -rf $(abs_srcdir)/install-tree
	$(MAKE) install DESTDIR=$(abs_srcdir)/install-tree
	tree $(abs_srcdir)/install-tree
.PHONY: install-tree
