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
# library archive

noinst_LIBRARIES += \
	libbus1.a

libbus1_a_SOURCES = \
	src/peer.c \
	src/peer.h \
	src/message.c \
	src/message.h \
	src/node.c \
	src/node.h \
	src/interface.c \
	src/interface.h \
	src/bus1-client.c \
	src/bus1-client.h \
	src/libbus1.sym \
	src/linux/bus1.h \
	src/org.bus1/b1-peer.h

libbus1_a_CFLAGS = \
	$(AM_CFLAGS) \
	$(CRBTREE_CFLAGS) \
	$(CSUNDRY_CFLAGS) \
	$(CVARIANT_CFLAGS)

# ------------------------------------------------------------------------------
# shared library built from archive

all-local: libbus1.so.0

pkginclude_HEADERS += \
	src/org.bus1/b1-peer.h

libbus1.so.0: libbus1.a $(top_srcdir)/src/libbus1.sym
	$(AM_V_CCLD)$(LINK) -shared \
	-Wl,-soname=$@ \
	-Wl,--version-script=$(top_srcdir)/src/libbus1.sym \
	-Wl,--whole-archive libbus1.a -Wl,--no-whole-archive \
	$(CRBTREE_LIBS) \
	$(CVARIANT_LIBS)

CLEANFILES += \
	libbus1.so.0

install-exec-local:
	@echo " $(MKDIR_P) '$(DESTDIR)$(libdir)'"; \
		$(MKDIR_P) "$(DESTDIR)$(libdir)" || exit 1
	@echo " $(INSTALL) libbus1.so.0 '$(DESTDIR)$(libdir)'"; \
		$(INSTALL) libbus1.so.0 "$(DESTDIR)$(libdir)" || exit $$?
	@echo " $(LN_S) -f libbus1.so.0 '$(DESTDIR)$(libdir)/libbus1.so'"; \
	$(LN_S) -f libbus1.so.0 "$(DESTDIR)$(libdir)/libbus1.so" || exit $$?

uninstall-local:
	@test ! -d "$(DESTDIR)$(libdir)" || \
		{ echo " ( cd '$(DESTDIR)$(libdir)' && rm -f libbus1.so.0 )"; \
		  rm -f "$(DESTDIR)$(libdir)/libbus1.so.0"; }

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
# test-perf

default_tests += \
	test-perf

test_perf_SOURCES = \
	src/test-perf.c

test_perf_CFLAGS = \
	$(AM_CFLAGS) \
	$(CRBTREE_CFLAGS) \
	$(CSUNDRY_CFLAGS) \
	$(CVARIANT_CFLAGS)

test_perf_LDADD = \
	libbus1.a \
	$(CRBTREE_LIBS) \
	$(CVARIANT_LIBS)

# ------------------------------------------------------------------------------
# test-peer

default_tests += \
	test-peer

test_peer_SOURCES = \
	src/test-peer.c

test_peer_CFLAGS = \
	$(AM_CFLAGS) \
	$(CRBTREE_CFLAGS) \
	$(CSUNDRY_CFLAGS) \
	$(CVARIANT_CFLAGS)

test_peer_LDADD = \
	libbus1.a \
	$(CRBTREE_LIBS) \
	$(CVARIANT_LIBS)

# ------------------------------------------------------------------------------
# test suite

AM_TESTS_ENVIRONMENT = LD_LIBRARY_PATH=$(abs_builddir)
check_PROGRAMS += $(default_tests)
TESTS += $(default_tests)

# ------------------------------------------------------------------------------
# check "make install" directory tree

install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree
