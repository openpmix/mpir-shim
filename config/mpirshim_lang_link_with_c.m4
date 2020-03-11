dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2006      Los Alamos National Security, LLC.  All rights
dnl                         reserved.
dnl Copyright (c) 2010-2012 Cisco Systems, Inc.  All rights reserved.
dnl Copyright (c) 2020      Intel, Inc.  All rights reserved.
dnl Copyright (c) 2020      Research Organization for Information Science
dnl                         and Technology (RIST).  All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl

# MPIRSHIM_TEST_LINK_C_AND_CXX([action if happy], [action if unhappy])
# -------------------------------
# Try to link a small test program against a C object file to make
# sure the compiler for the given language is compatible with the C
# compiler.
AC_DEFUN([MPIRSHIM_TEST_LINK_C_AND_CXX], [
    MPIRSHIM_VAR_SCOPE_PUSH([result])
    AC_MSG_CHECKING([if C and C++ are link compatible])

    # Write out and compile C part
    AC_LANG_PUSH([C])
    rm -f conftest_c.$ac_ext
    cat > conftest_c.$ac_ext << EOF
int testfunc(int a);
int testfunc(int a) { return a; }
EOF
    MPIRSHIM_LOG_COMMAND([$CC -c $CFLAGS $CPPFLAGS conftest_c.$ac_ext])

    # Now try linking together with C++
    AC_LANG_PUSH([C++])
    mpirshim_lang_link_with_c_libs=$LIBS
    LIBS="conftest_c.o $LIBS"
    AC_LINK_IFELSE([AC_LANG_PROGRAM([
extern "C" int testfunc(int);
],
        [return testfunc(0);])],
        [result=yes],
        [result=no])

    LIBS=$mpirshim_lang_link_with_c_libs
    AC_LANG_POP([C++])
    rm -f conftest_c.$ac_ext
    AC_LANG_POP([C])

    AC_MSG_RESULT([$result])
    AS_IF([test "$result" = "yes"], [$1], [$2])

    MPIRSHIM_VAR_SCOPE_POP
])
