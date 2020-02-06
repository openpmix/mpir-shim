dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2010      Cisco Systems, Inc.  All rights reserved.
dnl Copyright (c) 2014-2020 Intel, Inc.  All rights reserved.
dnl Copyright (c) 2014      Research Organization for Information Science
dnl                         and Technology (RIST). All rights reserved.
dnl
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl

# MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC()
# ----------------------------------------------------
# Helper macro from MPIRSHIM-CHECK-OS-FLAVORS(), below.
# $1 = macro to look for
# $2 = suffix of env variable to set with results
AC_DEFUN([MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC],
[
    AC_MSG_CHECKING([$1])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
     [[#ifndef $1
      error: this is not $1
      #endif
     ]])],
                      [mpirshim_found_$2=yes],
                      [mpirshim_found_$2=no])
    AC_MSG_RESULT([$mpirshim_found_$2])
])dnl

# MPIRSHIM_CHECK_OS_FLAVORS()
# ----------------------------------------------------
# Try to figure out the various OS flavors out there.
#
AC_DEFUN([MPIRSHIM_CHECK_OS_FLAVORS],
[
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__NetBSD__], [netbsd])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__FreeBSD__], [freebsd])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__OpenBSD__], [openbsd])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__DragonFly__], [dragonfly])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__386BSD__], [386bsd])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__bsdi__], [bsdi])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__APPLE__], [apple])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__linux__], [linux])
    MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__sun__], [sun])
    AS_IF([test "$mpirshim_found_sun" = "no"],
          MPIRSHIM_CHECK_OS_FLAVOR_SPECIFIC([__sun], [sun]))

    AS_IF([test "$mpirshim_found_sun" = "yes"],
          [mpirshim_have_solaris=1
           CFLAGS="$CFLAGS -D_REENTRANT"
           CPPFLAGS="$CPPFLAGS -D_REENTRANT"],
          [mpirshim_have_solaris=0])
    AC_DEFINE_UNQUOTED([MPIRSHIM_HAVE_SOLARIS],
                       [$mpirshim_have_solaris],
                       [Whether or not we have solaris])

    # check for sockaddr_in (a good sign we have TCP)
    AC_CHECK_HEADERS([netdb.h netinet/in.h netinet/tcp.h])
    AC_CHECK_TYPES([struct sockaddr_in],
                   [mpirshim_found_sockaddr=yes],
                   [mpirshim_found_sockaddr=no],
                   [AC_INCLUDES_DEFAULT
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif])
])dnl
