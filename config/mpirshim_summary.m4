dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2016      Los Alamos National Security, LLC. All rights
dnl                         reserved.
dnl Copyright (c) 2016-2018 Cisco Systems, Inc.  All rights reserved
dnl Copyright (c) 2016      Research Organization for Information Science
dnl                         and Technology (RIST). All rights reserved.
dnl Copyright (c) 2018-2020 Intel, Inc.  All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl
AC_DEFUN([MPIRSHIM_SUMMARY_ADD],[
    MPIRSHIM_VAR_SCOPE_PUSH([mpirshim_summary_section mpirshim_summary_line mpirshim_summary_section_current])

    dnl need to replace spaces in the section name with somethis else. _ seems like a reasonable
    dnl choice. if this changes remember to change MPIRSHIM_PRINT_SUMMARY as well.
    mpirshim_summary_section=$(echo $1 | tr ' ' '_')
    mpirshim_summary_line="$2: $4"
    mpirshim_summary_section_current=$(eval echo \$mpirshim_summary_values_$mpirshim_summary_section)

    if test -z "$mpirshim_summary_section_current" ; then
        if test -z "$mpirshim_summary_sections" ; then
            mpirshim_summary_sections=$mpirshim_summary_section
        else
            mpirshim_summary_sections="$mpirshim_summary_sections $mpirshim_summary_section"
        fi
        eval mpirshim_summary_values_$mpirshim_summary_section=\"$mpirshim_summary_line\"
    else
        eval mpirshim_summary_values_$mpirshim_summary_section=\"$mpirshim_summary_section_current,$mpirshim_summary_line\"
    fi

    MPIRSHIM_VAR_SCOPE_POP
])

AC_DEFUN([MPIRSHIM_SUMMARY_PRINT],[
    MPIRSHIM_VAR_SCOPE_PUSH([mpirshim_summary_section mpirshim_summary_section_name])
    cat <<EOF

MPIRSHIM configuration:
-----------------------
Version: $MPIRSHIM_MAJOR_VERSION.$MPIRSHIM_MINOR_VERSION.$MPIRSHIM_RELEASE_VERSION$MPIRSHIM_GREEK_VERSION
EOF

    if test $WANT_DEBUG = 0 ; then
        echo "Debug build: no"
    else
        echo "Debug build: yes"
    fi

    echo

    for mpirshim_summary_section in $(echo $mpirshim_summary_sections) ; do
        mpirshim_summary_section_name=$(echo $mpirshim_summary_section | tr '_' ' ')
        echo "$mpirshim_summary_section_name"
        echo "-----------------------"
        echo "$(eval echo \$mpirshim_summary_values_$mpirshim_summary_section)" | tr ',' $'\n' | sort -f
        echo " "
    done

    if test $WANT_DEBUG = 1 ; then
        cat <<EOF
*****************************************************************************
 THIS IS A DEBUG BUILD!  DO NOT USE THIS BUILD FOR PERFORMANCE MEASUREMENTS!
*****************************************************************************

EOF
    fi

    MPIRSHIM_VAR_SCOPE_POP
])
