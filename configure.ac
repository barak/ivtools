#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# Header
AC_PREREQ([2.71])

AC_INIT([InterViews Application Framework and Drawing Tools],[2.1.1],[johnston@vectaport.com],[ivtools],[http://www.ivtools.org])

dnl The foreign option allows the follow files to not exist: NEWS AUTHORS ChangeLog
AM_INIT_AUTOMAKE([subdir-objects foreign -Wall])
AC_CONFIG_SRCDIR([src/Unidraw/path.cc])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Process options
AC_MSG_CHECKING([with_ace])
AC_ARG_WITH([ace],
       [AS_HELP_STRING([--with-ace],[use the ACE library (default: check)])],,
       [with_ace=check])
AC_MSG_RESULT([${with_ace}])


AC_MSG_CHECKING([with_clippoly])
AC_ARG_WITH([clippoly],
       [AS_HELP_STRING([--with-clippoly],[use the CLIPPOLY library (default: check)])],,
       [with_clippoly=check])
AC_MSG_RESULT([${with_clippoly}])

AC_MSG_CHECKING([with_backend])
AC_ARG_WITH([backend],
       [AS_HELP_STRING([--with-backend=BACKEND],[graphics backend: x11 or gtk4 (default: x11)])],,
       [with_backend=x11])
AC_MSG_RESULT([${with_backend}])

# Checks for programs.
AC_LANG([C++])
AC_PROG_CXX
### Work around autoconf 2.72 bug; remove for autoconf 2.73+
# autoconf 2.72 AC_PROG_CXX's C++11 detection test uses u8"..." string
# literals, which changed from const char* to const char8_t* in C++20,
# causing the test to fail with a C++20-defaulting compiler like
# GCC-16 and making autoconf fall back to -std=gnu++11. Strip whatever
# -std= flag it may have added and explicitly require C++17 or later
# instead.
CXX=`echo "$CXX" | sed 's/ -std=[[^ ]]*//g'`
CXXCPP=`echo "$CXXCPP" | sed 's/ -std=[[^ ]]*//g'`
###
dnl ask for -std=c++17
# Uses features introduced in C++17:
AX_CXX_COMPILE_STDCXX([17], [], [mandatory])
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AR
AC_PROG_INSTALL
ACLTX_PROG_PS2PDF([AC_MSG_WARN([Cannot find ps2pdf])])
AM_CONDITIONAL([PS2PDF], [test x$ps2pdf != xno])
LT_INIT

# Checks for libraries.
AC_PATH_XTRA
CFLAGS="${X_CFLAGS} ${CFLAGS}"
CXXFLAGS="${X_CFLAGS} ${CXXFLAGS}"
LDFLAGS="${X_LIBS} ${LDFLAGS}"

AC_CHECK_LIB([m], [pow], [MATH_LD=-lm], [AC_MSG_WARN([Cannot find math library])])

AS_IF([test "x${with_backend}" = xgtk4],
 [PKG_CHECK_MODULES([GTK4], [gtk4],
    [CFLAGS="${GTK4_CFLAGS} ${CFLAGS}"
     CXXFLAGS="${GTK4_CFLAGS} ${CXXFLAGS}"],
    [AC_MSG_ERROR([Cannot find GTK4 library; install libgtk-4-dev and try again])])])

AS_IF([test "x${with_backend}" != xgtk4],
 [AC_CHECK_LIB([X11], [XOpenDisplay], [X11_LD=-lX11], [AC_MSG_ERROR([Cannot find X11 library])])
  AC_CHECK_LIB([Xext], [XextFindDisplay], [XEXT_LD=-lXext], [AC_MSG_ERROR([Cannot find Xext library])])])

AS_IF([test "x${with_backend}" = xgtk4],
 [AC_SUBST(GTK4_CFLAGS)
  AC_SUBST(GTK4_LIBS)])

AS_IF([test ${with_ace} != no],
 [AC_CHECK_LIB([ACE], [ace_cleanup_destroyer],
  [with_ace=yes
   ACE_LD=-lACE],
  [AS_IF([test ${with_ace} = check],
    [with_ace=no
     AC_MSG_WARN([Cannot find ACE library])],
    [AC_MSG_ERROR([Cannot find ACE library])])])])

AC_CHECK_LIB([tiff], [TIFFOpen],
  [TIFF_LD=-ltiff],
  [AC_MSG_ERROR([Cannot find tiff library])])

PKG_CHECK_MODULES([FREETYPE], [freetype2],
  [],
  [AC_MSG_ERROR([Cannot find FreeType2 library])])

PKG_CHECK_MODULES([FONTCONFIG], [fontconfig],
  [],
  [AC_MSG_ERROR([Cannot find Fontconfig library])])

AS_IF([test ${with_clippoly} != no],
 [AC_CHECK_LIB([clippoly], [clippoly_version],
   [with_clippoly=yes
    CLIPPOLY_LD=-lclippoly],
   [AS_IF([test ${with_clippoly} = check],
     [with_clippoly=no
      AC_MSG_WARN([Cannot find clippoly library])],
     [AC_MSG_ERROR([Cannot find clippoly library])])])])

AM_CONDITIONAL([HAVE_LIBACE], [test ${with_ace} = yes])
AM_CONDITIONAL([HAVE_LIBCLIPPOLY], [test ${with_clippoly} = yes])
AM_CONDITIONAL([BACKEND_GTK4], [test "x${with_backend}" = xgtk4])
AM_CONDITIONAL([BACKEND_X11], [test "x${with_backend}" != xgtk4])

AC_SUBST(X11_LD)
AC_SUBST(XEXT_LD)
AC_SUBST(ACE_LD)
AC_SUBST(TIFF_LD)
AC_SUBST(CLIPPOLY_LD)

# Generated dynamic library version; in general, LIB_IV_VER=a:b:c
# generates (a-c).c.b

LIB_IV_VER=2:0:0
AC_SUBST(LIB_IV_VER)

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h float.h inttypes.h malloc.h \
memory.h netdb.h netinet/in.h stddef.h stdlib.h string.h strings.h \
sys/file.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h unistd.h])
AS_IF([test ${with_ace} = yes],
 [AC_CHECK_HEADERS([ace/Acceptor.h ace/ACE.h])])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UID_T

AH_TEMPLATE([sig_atomic_t], [Define to int if <signal.h> does not define.])
AC_CHECK_TYPES([sig_atomic_t], [], [AC_DEFINE([sig_atomic_t], [int])], [
#include <signal.h>
])

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MMAP
AC_FUNC_STRTOD

AC_CHECK_FUNCS([bzero floor getcwd gethostbyname gethostname \
gettimeofday isascii memchr memmove memset munmap pow realpath \
regcomp select socket sqrt strcasecmp strchr strcspn strdup strerror \
strncasecmp strrchr strstr strtol uname hypot hypotf])

# Random noise-control options.
AX_CFLAGS_WARN_ALL
AX_CXXFLAGS_WARN_ALL
AX_CHECK_COMPILE_FLAG([-Wno-narrowing], [CXXFLAGS="${CXXFLAGS} -Wno-narrowing"])
AX_CHECK_COMPILE_FLAG([-Wno-unused-variable], [CXXFLAGS="${CXXFLAGS} -Wno-unused-variable"])
AX_CHECK_COMPILE_FLAG([-Wno-char-subscripts], [CXXFLAGS="${CXXFLAGS} -Wno-char-subscripts"])
AX_CHECK_COMPILE_FLAG([-Wno-maybe-uninitialized], [CXXFLAGS="${CXXFLAGS} -Wno-maybe-uninitialized"])
AX_CHECK_COMPILE_FLAG([-Wno-sign-compare], [CXXFLAGS="${CXXFLAGS} -Wno-sign-compare"])


# Trailer
AC_CONFIG_FILES([Makefile src/Makefile src/include/Makefile
                 pkgconfig/IV.pc
                 pkgconfig/Unidraw.pc
                 pkgconfig/ComUtil.pc
                 pkgconfig/IVGlyph.pc
                 pkgconfig/Attribute.pc
                 pkgconfig/Time.pc
                 pkgconfig/ComTerp.pc
                 pkgconfig/AttrGlyph.pc
                 pkgconfig/ComGlyph.pc
                 pkgconfig/TopoFace.pc
                 pkgconfig/UniIdraw.pc
                 pkgconfig/OverlayUnidraw.pc
                 pkgconfig/ComUnidraw.pc
                 pkgconfig/FrameUnidraw.pc
                 pkgconfig/GraphUnidraw.pc
                 pkgconfig/Unidraw-common.pc
                 pkgconfig/IV-common.pc
                 pkgconfig/DrawServ.pc
                 pkgconfig/AceDispatch.pc])

AC_OUTPUT
