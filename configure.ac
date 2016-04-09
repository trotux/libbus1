# ------------------------------------------------------------------------------
# versions

AC_PREREQ([2.64])
AC_INIT([libbus1],
        [1],
        [http://www.github.com/bus1/libbus1],
        [libbus1],
        [http://www.github.com/bus1/libbus1])

# ------------------------------------------------------------------------------
# autotools

AC_CONFIG_SRCDIR([src/org.bus1/c-macro.h])
AC_CONFIG_MACRO_DIR([build/m4])
AC_CONFIG_HEADERS([build/config.h])
AC_CONFIG_AUX_DIR([build/build-aux])

# suppress conflicting defaults
CFLAGS=${CFLAGS:-""}
CXXFLAGS=${CXXFLAGS:-""}

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AM_MAINTAINER_MODE([enable])
AM_INIT_AUTOMAKE([foreign 1.11 -Wall -Wno-portability silent-rules tar-pax no-dist-gzip dist-xz subdir-objects parallel-tests])
AM_SILENT_RULES([yes])
AC_CANONICAL_HOST
AC_DEFINE_UNQUOTED([CANONICAL_HOST], "$host", [Canonical host string.])
AC_PROG_CC_C99
AC_PROG_RANLIB
AC_PROG_SED
AC_PROG_LN_S

m4_pattern_forbid([^_?PKG_[A-Z_]+$],[*** pkg.m4 missing, please install pkg-config])

# ------------------------------------------------------------------------------
# toolchain

OUR_CFLAGS=${OUR_CFLAGS:-$(echo \
        -g \
        -Og \
        -pipe \
        -ftrapv \
        -ffast-math \
        -fno-common \
        -fdiagnostics-show-option \
        -fno-strict-aliasing \
        -fvisibility=hidden \
        -ffunction-sections \
        -fdata-sections \
        -fstack-protector \
        -fstack-protector-strong \
        -fPIC \
        --param=ssp-buffer-size=4 \
        -Wall \
        -Wextra \
        -Wno-inline \
        -Wundef \
        -Wformat=2 \
        -Wformat-security \
        -Wformat-nonliteral \
        -Wsign-compare \
        -Wmissing-include-dirs \
        -Wold-style-definition \
        -Wpointer-arith \
        -Winit-self \
        -Wdeclaration-after-statement \
        -Wfloat-equal \
        -Wmissing-prototypes \
        -Wstrict-prototypes \
        -Wredundant-decls \
        -Wmissing-declarations \
        -Wmissing-noreturn \
        -Wshadow \
        -Wendif-labels \
        -Wstrict-aliasing=2 \
        -Wwrite-strings \
        -Wno-long-long \
        -Wno-overlength-strings \
        -Wno-unused-parameter \
        -Wno-missing-field-initializers \
        -Wno-unused-result \
        -Werror=overflow \
        -Wdate-time \
        -Wnested-externs)}

OUR_CPPFLAGS=${OUR_CPPFLAGS:-$(echo \
        -Wp,-D_FORTIFY_SOURCE=2 \
        -Wno-typedef-redefinition \
        -Wno-gnu-variable-sized-type-not-at-end)}

OUR_LDFLAGS=${OUR_LDFLAGS:-$(echo \
        -Wl,--as-needed \
        -Wl,--no-undefined \
        -Wl,--gc-sections \
        -Wl,-z,relro \
        -Wl,-z,now \
        -pie)}

AC_SUBST(OUR_CFLAGS)
AC_SUBST(OUR_CPPFLAGS)
AC_SUBST(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# dependencies

AC_ARG_ENABLE(static, AS_HELP_STRING([--enable-static], [enable temporary static library linking]))
AS_IF([test "$enable_static" = "yes"],
        [
                # temporary hack until we do proper shared library linking
                CVARIANT_CFLAGS="-I../cvariant/src"
                CVARIANT_LIBS="-L../cvariant -lcvariant"
                AC_SUBST(CVARIANT_CFLAGS)
                AC_SUBST(CVARIANT_LIBS)

                CRBTREE_CFLAGS="-I../crbtree/src"
                        CRBTREE_LIBS="-L../crbtree -lcrbtree"
                AC_SUBST(CRBTREE_CFLAGS)
                AC_SUBST(CRBTREE_LIBS)
        ], [
                PKG_CHECK_MODULES(CRBTREE, [crbtree],
                        [AC_DEFINE(HAVE_CRBTREE, 1, [Define if crbtree library is available])],
                        AC_MSG_ERROR([*** crbtree library not found]))

                PKG_CHECK_MODULES(CVARIANT, [cvariant],
                        [AC_DEFINE(HAVE_CRBTREE, 1, [Define if cvariant library is available])],
                        AC_MSG_ERROR([*** cvariant library not found]))
        ]
)

# ------------------------------------------------------------------------------
# report

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION
        prefix:                 ${prefix}
        exec_prefix:            ${exec_prefix}
        includedir:             ${includedir}
        libdir:                 ${libdir}

        CFLAGS:                 ${OUR_CFLAGS} ${CFLAGS}
        CPPFLAGS:               ${OUR_CPPFLAGS} ${CPPFLAGS}
        LDFLAGS:                ${OUR_LDFLAGS} ${LDFLAGS}
])
