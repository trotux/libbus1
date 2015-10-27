# ------------------------------------------------------------------------------
# main

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

SUBDIRS = .

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# Keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES =
EXTRA_DIST =
BUILT_SOURCES =
lib_LIBRARIES =
noinst_LIBRARIES =
bin_PROGRAMS =
noinst_PROGRAMS =
check_PROGRAMS =
TESTS =
default_tests =

AM_CPPFLAGS = \
	-include $(top_builddir)/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# libbus1
# XXX: for now static, should be made dynamic later on

noinst_LIBRARIES += \
	libbus1.a

libbus1_a_SOURCES = \
	src/bus1-macro.h \
	src/bus1-public.h \
	src/bus1-sys.c \
	src/bus1-sys.h

libbus1_a_CFLAGS = \
	$(AM_CFLAGS)

# ------------------------------------------------------------------------------
# bus1ctl

bin_PROGRAMS += \
	bus1ctl

bus1ctl_SOURCES = \
	src/bus1ctl.c

bus1ctl_CFLAGS = \
	$(AM_CFLAGS)

bus1ctl_LDADD = \
	libbus1.a

# ------------------------------------------------------------------------------
# test-macro

default_tests += \
	test-macro

test_macro_SOURCES = \
	src/test-macro.c

test_macro_CFLAGS = \
	$(AM_CFLAGS)

test_macro_LDADD = \
	libbus1.a

# ------------------------------------------------------------------------------
# test-perf

default_tests += \
	test-perf

test_perf_SOURCES = \
	src/test-perf.c

test_perf_CFLAGS = \
	$(AM_CFLAGS)

test_perf_LDADD = \
	libbus1.a

# ------------------------------------------------------------------------------
# test-public

default_tests += \
	test-public

test_public_SOURCES = \
	src/test-public.c

test_public_CFLAGS = \
	$(AM_CFLAGS)

test_public_LDADD = \
	libbus1.a

# ------------------------------------------------------------------------------
# test suite

check_PROGRAMS += $(default_tests)
TESTS += $(default_tests)
