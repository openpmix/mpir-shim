dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
dnl                         University Research and Technology
dnl                         Corporation.  All rights reserved.
dnl Copyright (c) 2004-2005 The University of Tennessee and The University
dnl                         of Tennessee Research Foundation.  All rights
dnl                         reserved.
dnl Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
dnl                         University of Stuttgart.  All rights reserved.
dnl Copyright (c) 2004-2005 The Regents of the University of California.
dnl                         All rights reserved.
dnl Copyright (c) 2008      Cisco Systems, Inc.  All rights reserved.
dnl Copyright (c) 2020      Intel, Inc.  All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl

AC_DEFUN([MPIRSHIM_CXX_FIND_EXCEPTION_FLAGS],[
#
# Arguments: none
#
# Dependencies: none
#
# Get the exception handling flags for the C++ compiler.  Leaves
# CXXFLAGS undisturbed.
# Provides --with-exflags command line argument for configure as well.
#
# Sets MPIRSHIM_CXX_EXCEPTION_CXXFLAGS and MPIRSHIM_CXX_EXCEPTION_LDFLAGS as
# appropriate.
# Must call AC_SUBST manually
#

# Command line flags

AC_ARG_WITH(exflags,
  AC_HELP_STRING([--with-exflags],
                 [Specify flags necessary to enable C++ exceptions]),
  mpirshim_force_exflags="$withval")

mpirshim_CXXFLAGS_SAVE="$CXXFLAGS"
AC_MSG_CHECKING([for compiler exception flags])

# See which flags to use

if test "$mpirshim_force_exflags" != ""; then

    # If the user supplied flags, use those

    mpirshim_exflags="$mpirshim_force_exflags"
elif test "$GXX" = "yes"; then

    # g++ has changed their flags a few times.  Sigh.

    CXXFLAGS="$CXXFLAGS -fexceptions"

    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[try { int i = 0; } catch(...) { int j = 2; }]])], mpirshim_happy=1, mpirshim_happy=0)

    if test "$mpirshim_happy" = "1"; then
	mpirshim_exflags="-fexceptions";
    else
	CXXFLAGS="$CXXFLAGS_SAVE -fhandle-exceptions"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[try { int i = 0; } catch(...) { int j = 2; }]])], mpirshim_happy=1, mpirshim_happy=0)
	if test "$mpirshim_happy" = "1"; then
	    mpirshim_exflags="-fhandle-exceptions";
	fi
    fi
    AC_LANG_RESTORE
elif test "`basename $CXX`" = "KCC"; then

    # KCC flags

    mpirshim_exflags="--exceptions"
fi
CXXFLAGS="$mpirshim_CXXFLAGS_SAVE"

# Save the result

MPIRSHIM_CXX_EXCEPTIONS_CXXFLAGS="$mpirshim_exflags"
MPIRSHIM_CXX_EXCEPTIONS_LDFLAGS="$mpirshim_exflags"
if test "$mpirshim_exflags" = ""; then
    AC_MSG_RESULT([none necessary])
else
    AC_MSG_RESULT([$mpirshim_exflags])
fi

# Clean up

unset mpirshim_force_exflags mpirshim_CXXFLAGS_SAVE mpirshim_exflags mpirshim_happy])dnl

