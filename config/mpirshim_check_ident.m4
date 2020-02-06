dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2007      Sun Microsystems, Inc.  All rights reserved.
dnl Copyright (c) 2015-2020 Intel, Inc.  All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl
dnl defines:
dnl   MPIRSHIM_$1_USE_PRAGMA_IDENT
dnl   MPIRSHIM_$1_USE_IDENT
dnl   MPIRSHIM_$1_USE_CONST_CHAR_IDENT
dnl

# MPIRSHIM_CHECK_IDENT(compiler-env, compiler-flags,
# file-suffix, lang) Try to compile a source file containing
# a #pragma ident, and determine whether the ident was
# inserted into the resulting object file
# -----------------------------------------------------------
AC_DEFUN([MPIRSHIM_CHECK_IDENT], [
    AC_MSG_CHECKING([for $4 ident string support])

    mpirshim_pragma_ident_happy=0
    mpirshim_ident_happy=0
    mpirshim_static_const_char_happy=0
    _MPIRSHIM_CHECK_IDENT(
        [$1], [$2], [$3],
        [[#]pragma ident], [],
        [mpirshim_pragma_ident_happy=1
         mpirshim_message="[#]pragma ident"],
        _MPIRSHIM_CHECK_IDENT(
            [$1], [$2], [$3],
            [[#]ident], [],
            [mpirshim_ident_happy=1
             mpirshim_message="[#]ident"],
            _MPIRSHIM_CHECK_IDENT(
                [$1], [$2], [$3],
                [[#]pragma comment(exestr, ], [)],
                [mpirshim_pragma_comment_happy=1
                 mpirshim_message="[#]pragma comment"],
                [mpirshim_static_const_char_happy=1
                 mpirshim_message="static const char[[]]"])))

    AC_DEFINE_UNQUOTED([MPIRSHIM_$1_USE_PRAGMA_IDENT],
        [$mpirshim_pragma_ident_happy], [Use #pragma ident strings for $4 files])
    AC_DEFINE_UNQUOTED([MPIRSHIM_$1_USE_IDENT],
        [$mpirshim_ident_happy], [Use #ident strings for $4 files])
    AC_DEFINE_UNQUOTED([MPIRSHIM_$1_USE_PRAGMA_COMMENT],
        [$mpirshim_pragma_comment_happy], [Use #pragma comment for $4 files])
    AC_DEFINE_UNQUOTED([MPIRSHIM_$1_USE_CONST_CHAR_IDENT],
        [$mpirshim_static_const_char_happy], [Use static const char[] strings for $4 files])

    AC_MSG_RESULT([$mpirshim_message])

    unset mpirshim_pragma_ident_happy mpirshim_ident_happy mpirshim_static_const_char_happy mpirshim_message
])

# _MPIRSHIM_CHECK_IDENT(compiler-env, compiler-flags,
# file-suffix, header_prefix, header_suffix, action-if-success, action-if-fail)
# Try to compile a source file containing a #-style ident,
# and determine whether the ident was inserted into the
# resulting object file
# -----------------------------------------------------------
AC_DEFUN([_MPIRSHIM_CHECK_IDENT], [
    eval mpirshim_compiler="\$$1"
    eval mpirshim_flags="\$$2"

    mpirshim_ident="string_not_coincidentally_inserted_by_the_compiler"
    cat > conftest.$3 <<EOF
$4 "$mpirshim_ident" $5
int main(int argc, char** argv);
int main(int argc, char** argv) { return 0; }
EOF

    # "strings" won't always return the ident string.  objdump isn't
    # universal (e.g., OS X doesn't have it), and ...other
    # complications.  So just try to "grep" for the string in the
    # resulting object file.  If the ident is found in "strings" or
    # the grep succeeds, rule that we have this flavor of ident.

    echo "configure:__oline__: $1" >&5
    mpirshim_output=`$mpirshim_compiler $mpirshim_flags -c conftest.$3 -o conftest.${OBJEXT} 2>&1 1>/dev/null`
    mpirshim_status=$?
    AS_IF([test $mpirshim_status = 0],
          [test -z "$mpirshim_output"
           mpirshim_status=$?])
    MPIRSHIM_LOG_MSG([\$? = $mpirshim_status], 1)
    AS_IF([test $mpirshim_status = 0 && test -f conftest.${OBJEXT}],
          [mpirshim_output="`strings -a conftest.${OBJEXT} | grep $mpirshim_ident`"
           grep $mpirshim_ident conftest.${OBJEXT} 2>&1 1>/dev/null
           mpirshim_status=$?
           AS_IF([test "$mpirshim_output" != "" || test "$mpirshim_status" = "0"],
                 [$6],
                 [$7])],
          [MPIRSHIM_LOG_MSG([the failed program was:])
           MPIRSHIM_LOG_FILE([conftest.$3])
           $7])

    unset mpirshim_compiler mpirshim_flags mpirshim_output mpirshim_status
    rm -rf conftest.* conftest${EXEEXT}
])dnl
