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
dnl Copyright (c) 2006-2010 Oracle and/or its affiliates.  All rights reserved.
dnl Copyright (c) 2009-2016 Cisco Systems, Inc.  All rights reserved.
dnl Copyright (c) 2015-2019 Research Organization for Information Science
dnl                         and Technology (RIST).  All rights reserved.
dnl Copyright (c) 2016      IBM Corporation.  All rights reserved.
dnl Copyright (c) 2017-2020 Intel, Inc.  All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl

# MPIRSHIM_LIBTOOL_CONFIG(libtool-variable, result-variable,
#                         libtool-tag, extra-code)
# Retrieve information from the generated libtool
AC_DEFUN([MPIRSHIM_LIBTOOL_CONFIG],[
    MPIRSHIM_VAR_SCOPE_PUSH([rpath_script rpath_outfile])
    # Output goes into globally-visible variable.  Run this in a
    # sub-process so that we don't pollute the current process
    # environment.
    rpath_script=conftest.$$.sh
    rpath_outfile=conftest.$$.out
    rm -f $rpath_script $rpath_outfile
    cat > $rpath_script <<EOF
#!/bin/sh

# Slurp in the libtool config into my environment

# Apparently, "libtoool --config" calls "exit", so we can't source it
# (because if script A sources script B, and B calls "exit", then both
# B and A will exit).  Instead, we have to send the output to a file
# and then source that.
$MPIRSHIM_TOP_BUILDDIR/libtool $3 --config > $rpath_outfile

chmod +x $rpath_outfile
. ./$rpath_outfile
rm -f $rpath_outfile

# Evaluate \$$1, and substitute in LIBDIR for \$libdir
$4
flags="\`eval echo \$$1\`"
echo \$flags

# Done
exit 0
EOF
    chmod +x $rpath_script
    $2=`./$rpath_script`
    rm -f $rpath_script
    MPIRSHIM_VAR_SCOPE_POP
])

# Check to see whether the linker supports DT_RPATH.  We'll need to
# use config.rpath to find the flags that it needs, if it does (see
# comments in config.rpath for an explanation of where it came from).
AC_DEFUN([MPIRSHIM_SETUP_RPATH],[
    MPIRSHIM_VAR_SCOPE_PUSH([rpath_libdir_save])
    AC_MSG_CHECKING([if linker supports RPATH])
    MPIRSHIM_LIBTOOL_CONFIG([hardcode_libdir_flag_spec],[rpath_args],[],[libdir=LIBDIR])

    AS_IF([test -n "$rpath_args"],
          [MPIRSHIM_RPATH_SUPPORT=rpath
           MPIRSHIM_LIBTOOL_CONFIG([hardcode_libdir_flag_spec],[rpath_fc_args],[--tag=FC],[libdir=LIBDIR])
           AC_MSG_RESULT([yes ($rpath_args + $rpath_fc_args)])],
          [WRAPPER_RPATH_SUPPORT=unnecessary
           AC_MSG_RESULT([yes (no extra flags needed)])])

    MPIRSHIM_VAR_SCOPE_POP

    # If we found RPATH support, check for RUNPATH support, too
    AS_IF([test "$MPIRSHIM_RPATH_SUPPORT" = "rpath"],
          [MPIRSHIM_SETUP_RUNPATH])
])

# Check to see if the linker supports the DT_RUNPATH flags via
# --enable-new-dtags (a GNU ld-specific option).  These flags are more
# social than DT_RPATH -- they can be overridden by LD_LIBRARY_PATH
# (where a regular DT_RPATH cannot).
#
# If DT_RUNPATH is supported, then we'll use *both* the RPATH and
# RUNPATH flags in the LDFLAGS.
AC_DEFUN([MPIRSHIM_SETUP_RUNPATH],[
    MPIRSHIM_VAR_SCOPE_PUSH([LDFLAGS_save wl_fc])

    # Set the output in $runpath_args
    runpath_args=
    LDFLAGS_save=$LDFLAGS
    LDFLAGS="$LDFLAGS -Wl,--enable-new-dtags"
    AS_IF([test x"$enable_wrapper_runpath" = x"yes"],
           [AC_LANG_PUSH([C])
            AC_MSG_CHECKING([if linker supports RUNPATH])
            AC_LINK_IFELSE([AC_LANG_PROGRAM([], [return 7;])],
                           [MPIRSHIM_RPATH_SUPPORT=runpath
                            runpath_args="-Wl,--enable-new-dtags"
                            AC_MSG_RESULT([yes (-Wl,--enable-new-dtags)])],
                           [AC_MSG_RESULT([no])])
            AC_LANG_POP([C])])
    LDFLAGS=$LDFLAGS_save

    MPIRSHIM_VAR_SCOPE_POP
])

# Called to find all -L arguments in the LDFLAGS and add in RPATH args
# for each of them.  Then also add in an RPATH for @{libdir} (which
# will be replaced by the wrapper compile to the installdir libdir at
# runtime), and the RUNPATH args, if we have them.
AC_DEFUN([RPATHIFY_LDFLAGS_INTERNAL],[
    MPIRSHIM_VAR_SCOPE_PUSH([rpath_out rpath_dir rpath_tmp])
    AS_IF([test "$MPIRSHIM_RPATH_SUPPORT" != "unnecessary"], [
           rpath_out=""
           for val in ${$1}; do
               case $val in
               -L*)
                   rpath_dir=`echo $val | cut -c3-`
                   rpath_tmp=`echo ${$2} | sed -e s@LIBDIR@$rpath_dir@`
                   rpath_out="$rpath_out $rpath_tmp"
                   ;;
               esac
           done

           # Now add in the RPATH args for @{libdir}, and the RUNPATH args
           rpath_tmp=`echo ${$2} | sed -e s/LIBDIR/@{libdir}/`
           $1="${$1} $rpath_out $rpath_tmp ${$3}"
          ])
    MPIRSHIM_VAR_SCOPE_POP
])

AC_DEFUN([RPATHIFY_LDFLAGS],[RPATHIFY_LDFLAGS_INTERNAL([$1], [rpath_args], [runpath_args])])
