#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT(radiance, 1.1.4)
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([autoconf.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
# FIXME: Replace `main' with a function in `-ldl':
AC_CHECK_LIB([dl], [main])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h sys/file.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_CHECK_FUNCS([ftruncate memset socket strerror])

AM_SILENT_RULES([yes])
AM_INIT_AUTOMAKE([1.11 no-define foreign])
AC_PROG_CXX
AC_CONFIG_FILES([Makefile src/Makefile])
AC_LANG([C++])
AX_CXX_COMPILE_STDCXX(11, noext, mandatory)
AX_BOOST_BASE([1.55], [], [AC_MSG_ERROR(Need boost >= 1.55)])
AX_BOOST_IOSTREAMS
AX_BOOST_SYSTEM
AX_BOOST_THREAD
AX_BOOST_LOG
AX_BOOST_LOG_SETUP
AX_BOOST_DATE_TIME
AX_PTHREAD([], AC_MSG_FAILURE([pthread library is required]))
EV_DEVEL
MYSQL_C_API_LOCATION
MYSQLPP_DEVEL

# -----------------------------------------------------------------------------
# memory allocation library
AC_MSG_CHECKING([for memory allocator])
CHECK_JEMALLOC
if test "$has_jemalloc" = "1"; then
    AC_DEFINE([ENABLE_JEMALLOC], [1], [compile and link with jemalloc])
    AC_MSG_RESULT([jemalloc])
else
    CHECK_TCMALLOC
    if test "$has_tcmalloc" = "1"; then
        AC_DEFINE([ENABLE_TCMALLOC], [1], [compile and link with tcmalloc])
        AC_MSG_RESULT([tcmalloc])
    else
        AC_MSG_RESULT([system])
    fi
fi

AC_ARG_ENABLE([debug],
  [  --enable-debug           build with additional debugging code],
  [
    CFLAGS='-g -pg -ggdb';
    LDFLAGS="${LDFLAGS} -ldl -lprofiler";
    LIBS="$LIBS -lprofiler";
    AC_DEFINE([__DEBUG_BUILD__], [true], [enable debug build])
    AC_MSG_NOTICE($(tput setaf 1)"DEBUG BUILD ENABLED!!!"$(tput sgr0))
  ]
)

AC_OUTPUT
