dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
dnl                         University Research and Technology
dnl                         Corporation.  All rights reserved.
dnl Copyright (c) 2004-2018 The University of Tennessee and The University
dnl                         of Tennessee Research Foundation.  All rights
dnl                         reserved.
dnl Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
dnl                         University of Stuttgart.  All rights reserved.
dnl Copyright (c) 2004-2005 The Regents of the University of California.
dnl                         All rights reserved.
dnl Copyright (c) 2007      Sun Microsystems, Inc.  All rights reserved.
dnl Copyright (c) 2009      Oak Ridge National Labs.  All rights reserved.
dnl Copyright (c) 2009-2015 Cisco Systems, Inc.  All rights reserved.
dnl Copyright (c) 2014-2020 Intel, Inc.  All rights reserved.
dnl Copyright (c) 2015-2017 Research Organization for Information Science
dnl                         and Technology (RIST). All rights reserved.
dnl
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl
dnl Portions of this file derived from GASNet v1.12 (see "GASNet"
dnl comments, below)
dnl Copyright 2004,  Dan Bonachea <bonachea@cs.berkeley.edu>
dnl
dnl IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
dnl DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
dnl OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
dnl CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
dnl
dnl THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
dnl INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
dnl AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
dnl ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
dnl PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
dnl

AC_DEFUN([MPIRSHIM_CONFIGURE_SETUP],[

# Some helper script functions.  Unfortunately, we cannot use $1 kinds
# of arguments here because of the m4 substitution.  So we have to set
# special variable names before invoking the function.  :-\

mpirshim_show_title() {
  cat <<EOF

============================================================================
== ${1}
============================================================================
EOF
  MPIRSHIM_LOG_MSG([=== ${1}], 1)
}


mpirshim_show_subtitle() {
  cat <<EOF

*** ${1}
EOF
  MPIRSHIM_LOG_MSG([*** ${1}], 1)
}


mpirshim_show_subsubtitle() {
  cat <<EOF

+++ ${1}
EOF
  MPIRSHIM_LOG_MSG([+++ ${1}], 1)
}

mpirshim_show_subsubsubtitle() {
  cat <<EOF

--- ${1}
EOF
  MPIRSHIM_LOG_MSG([--- ${1}], 1)
}

mpirshim_show_verbose() {
  if test "$V" = "1"; then
      cat <<EOF
+++ VERBOSE: ${1}
EOF
      MPIRSHIM_LOG_MSG([--- ${1}], 1)
  fi
}

#
# Save some stats about this build
#

MPIRSHIM_CONFIGURE_USER="${USER:-`whoami`}"
MPIRSHIM_CONFIGURE_HOST="${HOSTNAME:-`(hostname || uname -n) 2> /dev/null | sed 1q`}"
MPIRSHIM_CONFIGURE_DATE="`date`"

#
# Save these details so that they can be used in mpirshim_info later
#
AC_SUBST(MPIRSHIM_CONFIGURE_USER)
AC_SUBST(MPIRSHIM_CONFIGURE_HOST)
AC_SUBST(MPIRSHIM_CONFIGURE_DATE)])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

AC_DEFUN([MPIRSHIM_BASIC_SETUP],[
#
# Save some stats about this build
#

MPIRSHIM_CONFIGURE_USER="${USER:-`whoami`}"
MPIRSHIM_CONFIGURE_HOST="${HOSTNAME:-`(hostname || uname -n) 2> /dev/null | sed 1q`}"
MPIRSHIM_CONFIGURE_DATE="`date`"

#
# Make automake clean emacs ~ files for "make clean"
#

CLEANFILES="*~ .\#*"
AC_SUBST(CLEANFILES)

#
# See if we can find an old installation of MPIRSHIM to overwrite
#

# Stupid autoconf 2.54 has a bug in AC_PREFIX_PROGRAM -- if mpirshim_clean
# is not found in the path and the user did not specify --prefix,
# we'll get a $prefix of "."

mpirshim_prefix_save="$prefix"
AC_PREFIX_PROGRAM(mpirshim_clean)
if test "$prefix" = "."; then
    prefix="$mpirshim_prefix_save"
fi
unset mpirshim_prefix_save

#
# Basic sanity checking; we can't install to a relative path
#

case "$prefix" in
  /*/bin)
    prefix="`dirname $prefix`"
    echo installing to directory \"$prefix\"
    ;;
  /*)
    echo installing to directory \"$prefix\"
    ;;
  NONE)
    echo installing to directory \"$ac_default_prefix\"
    ;;
  @<:@a-zA-Z@:>@:*)
    echo installing to directory \"$prefix\"
    ;;
  *)
    AC_MSG_ERROR(prefix "$prefix" must be an absolute directory path)
    ;;
esac

# BEGIN: Derived from GASNet

# Suggestion from Paul Hargrove to disable --program-prefix and
# friends.  Heavily influenced by GASNet 1.12 acinclude.m4
# functionality to do the same thing (copyright listed at top of this
# file).

# echo program_prefix=$program_prefix  program_suffix=$program_suffix program_transform_name=$program_transform_name
# undo prefix autoconf automatically adds during cross-compilation
if test "$cross_compiling" = yes && test "$program_prefix" = "${target_alias}-" ; then
    program_prefix=NONE
fi
# normalize empty prefix/suffix
if test -z "$program_prefix" ; then
    program_prefix=NONE
fi
if test -z "$program_suffix" ; then
    program_suffix=NONE
fi
# undo transforms caused by empty prefix/suffix
if expr "$program_transform_name" : 's.^..$' >/dev/null || \
   expr "$program_transform_name" : 's.$$..$' >/dev/null || \
   expr "$program_transform_name" : 's.$$..;s.^..$' >/dev/null ; then
    program_transform_name="s,x,x,"
fi
if test "$program_prefix$program_suffix$program_transform_name" != "NONENONEs,x,x," ; then
    AC_MSG_WARN([*** The MPIR-SHIM configure script does not support --program-prefix, --program-suffix or --program-transform-name. Users are recommended to instead use --prefix with a unique directory and make symbolic links as desired for renaming.])
    AC_MSG_ERROR([*** Cannot continue])
fi

# END: Derived from GASNet
])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

AC_DEFUN([MPIRSHIM_LOG_MSG],[
# 1 is the message
# 2 is whether to put a prefix or not
if test -n "$2"; then
    echo "configure:__oline__: $1" >&5
else
    echo $1 >&5
fi])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

AC_DEFUN([MPIRSHIM_LOG_FILE],[
# 1 is the filename
if test -n "$1" && test -f "$1"; then
    cat $1 >&5
fi])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

AC_DEFUN([MPIRSHIM_LOG_COMMAND],[
# 1 is the command
# 2 is actions to do if success
# 3 is actions to do if fail
echo "configure:__oline__: $1" >&5
$1 1>&5 2>&1
mpirshim_status=$?
MPIRSHIM_LOG_MSG([\$? = $mpirshim_status], 1)
if test "$mpirshim_status" = "0"; then
    unset mpirshim_status
    $2
else
    unset mpirshim_status
    $3
fi])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

AC_DEFUN([MPIRSHIM_UNIQ],[
# 1 is the variable name to be uniq-ized
mpirshim_name=$1

# Go through each item in the variable and only keep the unique ones

mpirshim_count=0
for val in ${$1}; do
    mpirshim_done=0
    mpirshim_i=1
    mpirshim_found=0

    # Loop over every token we've seen so far

    mpirshim_done="`expr $mpirshim_i \> $mpirshim_count`"
    while test "$mpirshim_found" = "0" && test "$mpirshim_done" = "0"; do

	# Have we seen this token already?  Prefix the comparison with
	# "x" so that "-Lfoo" values won't be cause an error.

	mpirshim_eval="expr x$val = x\$mpirshim_array_$mpirshim_i"
	mpirshim_found=`eval $mpirshim_eval`

	# Check the ending condition

	mpirshim_done="`expr $mpirshim_i \>= $mpirshim_count`"

	# Increment the counter

	mpirshim_i="`expr $mpirshim_i + 1`"
    done

    # If we didn't find the token, add it to the "array"

    if test "$mpirshim_found" = "0"; then
	mpirshim_eval="mpirshim_array_$mpirshim_i=$val"
	eval $mpirshim_eval
	mpirshim_count="`expr $mpirshim_count + 1`"
    else
	mpirshim_i="`expr $mpirshim_i - 1`"
    fi
done

# Take all the items in the "array" and assemble them back into a
# single variable

mpirshim_i=1
mpirshim_done="`expr $mpirshim_i \> $mpirshim_count`"
mpirshim_newval=
while test "$mpirshim_done" = "0"; do
    mpirshim_eval="mpirshim_newval=\"$mpirshim_newval \$mpirshim_array_$mpirshim_i\""
    eval $mpirshim_eval

    mpirshim_eval="unset mpirshim_array_$mpirshim_i"
    eval $mpirshim_eval

    mpirshim_done="`expr $mpirshim_i \>= $mpirshim_count`"
    mpirshim_i="`expr $mpirshim_i + 1`"
done

# Done; do the assignment

mpirshim_newval="`echo $mpirshim_newval`"
mpirshim_eval="$mpirshim_name=\"$mpirshim_newval\""
eval $mpirshim_eval

# Clean up

unset mpirshim_name mpirshim_i mpirshim_done mpirshim_newval mpirshim_eval mpirshim_count])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

# MPIRSHIM_APPEND_UNIQ(variable, new_argument)
# ----------------------------------------
# Append new_argument to variable if not already in variable.  This assumes a
# space separated list.
#
# This could probably be made more efficient :(.
AC_DEFUN([MPIRSHIM_APPEND_UNIQ], [
for arg in $2; do
    mpirshim_found=0;
    for val in ${$1}; do
        if test "x$val" = "x$arg" ; then
            mpirshim_found=1
            break
        fi
    done
    if test "$mpirshim_found" = "0" ; then
        if test -z "$$1"; then
            $1="$arg"
        else
            $1="$$1 $arg"
        fi
    fi
done
unset mpirshim_found
])

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

# Remove all duplicate -I, -L, and -l flags from the variable named $1
AC_DEFUN([MPIRSHIM_FLAGS_UNIQ],[
    # 1 is the variable name to be uniq-ized
    mpirshim_name=$1

    # Go through each item in the variable and only keep the unique ones

    mpirshim_count=0
    for val in ${$1}; do
        mpirshim_done=0
        mpirshim_i=1
        mpirshim_found=0

        # Loop over every token we've seen so far

        mpirshim_done="`expr $mpirshim_i \> $mpirshim_count`"
        while test "$mpirshim_found" = "0" && test "$mpirshim_done" = "0"; do

            # Have we seen this token already?  Prefix the comparison
            # with "x" so that "-Lfoo" values won't be cause an error.

	    mpirshim_eval="expr x$val = x\$mpirshim_array_$mpirshim_i"
	    mpirshim_found=`eval $mpirshim_eval`

            # Check the ending condition

	    mpirshim_done="`expr $mpirshim_i \>= $mpirshim_count`"

            # Increment the counter

	    mpirshim_i="`expr $mpirshim_i + 1`"
        done

        # Check for special cases where we do want to allow repeated
        # arguments (per
        # http://www.open-mpi.org/community/lists/devel/2012/08/11362.php
        # and
        # https://github.com/open-mpi/ompi/issues/324).

        case $val in
        -Xclang)
                mpirshim_found=0
                mpirshim_i=`expr $mpirshim_count + 1`
                ;;
        -framework)
                mpirshim_found=0
                mpirshim_i=`expr $mpirshim_count + 1`
                ;;
        --param)
                mpirshim_found=0
                mpirshim_i=`expr $mpirshim_count + 1`
                ;;
        esac

        # If we didn't find the token, add it to the "array"

        if test "$mpirshim_found" = "0"; then
	    mpirshim_eval="mpirshim_array_$mpirshim_i=$val"
	    eval $mpirshim_eval
	    mpirshim_count="`expr $mpirshim_count + 1`"
        else
	    mpirshim_i="`expr $mpirshim_i - 1`"
        fi
    done

    # Take all the items in the "array" and assemble them back into a
    # single variable

    mpirshim_i=1
    mpirshim_done="`expr $mpirshim_i \> $mpirshim_count`"
    mpirshim_newval=
    while test "$mpirshim_done" = "0"; do
        mpirshim_eval="mpirshim_newval=\"$mpirshim_newval \$mpirshim_array_$mpirshim_i\""
        eval $mpirshim_eval

        mpirshim_eval="unset mpirshim_array_$mpirshim_i"
        eval $mpirshim_eval

        mpirshim_done="`expr $mpirshim_i \>= $mpirshim_count`"
        mpirshim_i="`expr $mpirshim_i + 1`"
    done

    # Done; do the assignment

    mpirshim_newval="`echo $mpirshim_newval`"
    mpirshim_eval="$mpirshim_name=\"$mpirshim_newval\""
    eval $mpirshim_eval

    # Clean up

    unset mpirshim_name mpirshim_i mpirshim_done mpirshim_newval mpirshim_eval mpirshim_count
])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

# MPIRSHIM_FLAGS_APPEND_UNIQ(variable, new_argument)
# ----------------------------------------------
# Append new_argument to variable if:
#
# - the argument does not begin with -I, -L, or -l, or
# - the argument begins with -I, -L, or -l, and it's not already in variable
#
# This macro assumes a space separated list.
AC_DEFUN([MPIRSHIM_FLAGS_APPEND_UNIQ], [
    MPIRSHIM_VAR_SCOPE_PUSH([mpirshim_tmp mpirshim_append])

    for arg in $2; do
        mpirshim_tmp=`echo $arg | cut -c1-2`
        mpirshim_append=1
        AS_IF([test "$mpirshim_tmp" = "-I" || test "$mpirshim_tmp" = "-L" || test "$mpirshim_tmp" = "-l"],
              [for val in ${$1}; do
                   AS_IF([test "x$val" = "x$arg"], [mpirshim_append=0])
               done])
        AS_IF([test "$mpirshim_append" = "1"],
              [AS_IF([test -z "$$1"], [$1=$arg], [$1="$$1 $arg"])])
    done

    MPIRSHIM_VAR_SCOPE_POP
])

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

# Macro that serves as an alternative to using `which <prog>`. It is
# preferable to simply using `which <prog>` because backticks (`) (aka
# backquotes) invoke a sub-shell which may source a "noisy"
# ~/.whatever file (and we do not want the error messages to be part
# of the assignment in foo=`which <prog>`). This macro ensures that we
# get a sane executable value.
AC_DEFUN([MPIRSHIM_WHICH],[
# 1 is the variable name to do "which" on
# 2 is the variable name to assign the return value to

MPIRSHIM_VAR_SCOPE_PUSH([mpirshim_prog mpirshim_file mpirshim_dir mpirshim_sentinel])

mpirshim_prog=$1

IFS_SAVE=$IFS
IFS="$PATH_SEPARATOR"
for mpirshim_dir in $PATH; do
    if test -x "$mpirshim_dir/$mpirshim_prog"; then
        $2="$mpirshim_dir/$mpirshim_prog"
        break
    fi
done
IFS=$IFS_SAVE

MPIRSHIM_VAR_SCOPE_POP
])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

# Declare some variables; use MPIRSHIM_VAR_SCOPE_POP to ensure that they
# are cleaned up / undefined.
AC_DEFUN([MPIRSHIM_VAR_SCOPE_PUSH],[

    # Is the private index set?  If not, set it.
    if test "x$mpirshim_scope_index" = "x"; then
        mpirshim_scope_index=1
    fi

    # First, check to see if any of these variables are already set.
    # This is a simple sanity check to ensure we're not already
    # overwriting pre-existing variables (that have a non-empty
    # value).  It's not a perfect check, but at least it's something.
    for mpirshim_var in $1; do
        mpirshim_str="mpirshim_str=\"\$$mpirshim_var\""
        eval $mpirshim_str

        if test "x$mpirshim_str" != "x"; then
            AC_MSG_WARN([Found configure shell variable clash at line $LINENO!])
            AC_MSG_WARN([[MPIRSHIM_VAR_SCOPE_PUSH] called on "$mpirshim_var",])
            AC_MSG_WARN([but it is already defined with value "$mpirshim_str"])
            AC_MSG_WARN([This usually indicates an error in configure.])
            AC_MSG_ERROR([Cannot continue])
        fi
    done

    # Ok, we passed the simple sanity check.  Save all these names so
    # that we can unset them at the end of the scope.
    mpirshim_str="mpirshim_scope_$mpirshim_scope_index=\"$1\""
    eval $mpirshim_str
    unset mpirshim_str

    env | grep mpirshim_scope
    mpirshim_scope_index=`expr $mpirshim_scope_index + 1`
])dnl

# Unset a bunch of variables that were previously set
AC_DEFUN([MPIRSHIM_VAR_SCOPE_POP],[
    # Unwind the index
    mpirshim_scope_index=`expr $mpirshim_scope_index - 1`
    mpirshim_scope_test=`expr $mpirshim_scope_index \> 0`
    if test "$mpirshim_scope_test" = "0"; then
        AC_MSG_WARN([[MPIRSHIM_VAR_SCOPE_POP] popped too many MPIRSHIM configure scopes.])
        AC_MSG_WARN([This usually indicates an error in configure.])
        AC_MSG_ERROR([Cannot continue])
    fi

    # Get the variable names from that index
    mpirshim_str="mpirshim_str=\"\$mpirshim_scope_$mpirshim_scope_index\""
    eval $mpirshim_str

    # Iterate over all the variables and unset them all
    for mpirshim_var in $mpirshim_str; do
        unset $mpirshim_var
    done
])dnl


dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

#
# MPIRSHIM_WITH_OPTION_MIN_MAX_VALUE(NAME,DEFAULT_VALUE,LOWER_BOUND,UPPER_BOUND)
# Defines a variable MPIRSHIM_MAX_xxx, with "xxx" being specified as parameter $1 as "variable_name".
# If not set at configure-time using --with-max-xxx, the default-value ($2) is assumed.
# If set, value is checked against lower (value >= $3) and upper bound (value <= $4)
#
AC_DEFUN([MPIRSHIM_WITH_OPTION_MIN_MAX_VALUE], [
    max_value=[$2]
    AC_MSG_CHECKING([maximum length of ]m4_translit($1, [_], [ ]))
    AC_ARG_WITH([max-]m4_translit($1, [_], [-]),
        AC_HELP_STRING([--with-max-]m4_translit($1, [_], [-])[=VALUE],
                       [maximum length of ]m4_translit($1, [_], [ ])[s.  VALUE argument has to be specified (default: [$2]).]))
    if test ! -z "$with_max_[$1]" && test "$with_max_[$1]" != "no" ; then
        # Ensure it's a number (hopefully an integer!), and >0
        expr $with_max_[$1] + 1 > /dev/null 2> /dev/null
        AS_IF([test "$?" != "0"], [happy=0],
              [AS_IF([test $with_max_[$1] -ge $3 && test $with_max_[$1] -le $4],
                     [happy=1], [happy=0])])

        # If badness in the above tests, bail
        AS_IF([test "$happy" = "0"],
              [AC_MSG_RESULT([bad value ($with_max_[$1])])
               AC_MSG_WARN([--with-max-]m4_translit($1, [_], [-])[s value must be >= $3 and <= $4])
               AC_MSG_ERROR([Cannot continue])])
        max_value=$with_max_[$1]
    fi
    AC_MSG_RESULT([$max_value])
    AC_DEFINE_UNQUOTED([MPIRSHIM_MAX_]m4_toupper($1), $max_value,
                       [Maximum length of ]m4_translit($1, [_], [ ])[s (default is $2)])
    [MPIRSHIM_MAX_]m4_toupper($1)=$max_value
    AC_SUBST([MPIRSHIM_MAX_]m4_toupper($1))
])dnl

dnl #######################################################################
dnl #######################################################################
dnl #######################################################################

# Usage: MPIRSHIM_COMPUTE_MAX_VALUE(number_bytes, variable_to_set, action if overflow)
# Compute maximum value of datatype of
# number_bytes, setting the result in the second argument.  Assumes a
# signed datatype.
AC_DEFUN([MPIRSHIM_COMPUTE_MAX_VALUE], [
    # This is more complicated than it really should be.  But some
    # expr implementations (OpenBSD) have an expr with a max value of
    # 2^31 - 1, and we sometimes want to compute the max value of a
    # type as big or bigger than that...
    mpirshim_num_bits=`expr $1 \* 8 - 1`
    newval=1
    value=1
    overflow=0

    while test $mpirshim_num_bits -ne 0 ; do
        newval=`expr $value \* 2`
        if test 0 -eq `expr $newval \< 0` ; then
            # if the new value is not negative, next iteration...
            value=$newval
            mpirshim_num_bits=`expr $mpirshim_num_bits - 1`
            # if this was the last iteration, subtract 1 (as signed
            # max positive is 2^num_bits - 1).  Do this here instead
            # of outside of the while loop because we might have
            # already subtracted 1 by then if we're trying to find the
            # max value of the same datatype expr uses as it's
            # internal representation (ie, if we hit the else
            # below...)
            if test 0 -eq $mpirshim_num_bits ; then
                value=`expr $value - 1`
            fi
        else
            # if the new value is negative, we've over flowed.  First,
            # try adding value - 1 instead of value (see if we can get
            # to positive max of expr)
            newval=`expr $value - 1 + $value`
            if test 0 -eq `expr $newval \< 0` ; then
                value=$newval
                # Still positive, this is as high as we can go.  If
                # mpirshim_num_bits is 1, we didn't actually overflow.
                # Otherwise, we overflowed.
                if test 1 -ne $mpirshim_num_bits ; then
                    overflow=1
                fi
            else
                # still negative.  Time to give up.
                overflow=1
            fi
            mpirshim_num_bits=0
        fi
    done

    AS_VAR_SET([$2], [$value])
    AS_IF([test $overflow -ne 0], [$3])
])dnl
