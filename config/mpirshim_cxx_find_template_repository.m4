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
dnl Copyright (c) 2015      Research Organization for Information Science
dnl                         and Technology (RIST). All rights reserved.
dnl Copyright (c) 2020      Intel, Inc.  All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl

AC_DEFUN([MPIRSHIM_CXX_FIND_TEMPLATE_REPOSITORY],[
    AC_REQUIRE([AC_PROG_GREP])

    #
    # Arguments: None
    #
    # Dependencies: None
    #
    # See if the compiler makes template repository directories
    # Warning: this is a really screwy example! -JMS
    #
    # Sets MPIRSHIM_CXX_TEMPLATE_REPOSITORY to the template repository, or blank.
    # Must call AC_SUBST manually
    #
    AC_CACHE_CHECK([for C++ template_repository_directory],
                   [mpirshim_cv_cxx_template_repository],
                   [_MPIRSHIM_CXX_FIND_TEMPLATE_REPOSITORY])

    if test "$mpirshim_cv_cxx_template_repository" = "not used" ; then
        MPIRSHIM_CXX_TEMPLATE_REPOSITORY=
    elif test "$mpirshim_cv_cxx_template_repository" = "templates not supported" ; then
        MPIRSHIM_CXX_TEMPLATE_REPOSITORY=
    else
        MPIRSHIM_CXX_TEMPLATE_REPOSITORY="$mpirshim_cv_cxx_template_repository"
    fi
])


AC_DEFUN([_MPIRSHIM_CXX_FIND_TEMPLATE_REPOSITORY],[
# Find the repository
mkdir conf_tmp_$$
cd conf_tmp_$$
cat > conftest.h <<EOF
template <class T>
class foo {
public:
  foo(T yow) : data(yow) { yow.member(3); };
  void member(int i);
private:
  T data;
};

class bar {
public:
  bar(int i) { data = i; };
  void member(int j) { data = data * j; };

private:
  int data;
};
EOF

cat > conftest2.C <<EOF
#include "conftest.h"

void
some_other_function(void)
{
  foo<bar> var1(6);
  foo< foo<bar> > var2(var1);
}
EOF

cat > conftest1.C <<EOF
#include "conftest.h"

void some_other_function(void);

template <class T>
void
foo<T>::member(int i)
{
  i += 2;
}

int
main()
{
  foo<bar> var1(6);
  foo< foo<bar> > var2(var1);

  some_other_function();
  return 0;
}
EOF

mpirshim_template_failed=
echo configure:__oline__: $CXX $CXXFLAGS -c conftest1.C >&5
$CXX $CXXFLAGS -c conftest1.C >&5 2>&5
if test ! -f conftest1.o ; then
    mpirshim_cv_cxx_template_repository="templates not supported"
    echo configure:__oline__: here is the program that failed: >&5
    cat conftest1.C >&5
    echo configure:__oline__: here is conftest.h: >&5
    cat conftest.h >&5
    mpirshim_template_failed=1
else
    echo configure:__oline__: $CXX $CXXFLAGS -c conftest2.C >&5
    $CXX $CXXFLAGS -c conftest2.C >&5 2>&5
    if test ! -f conftest2.o ; then
        mpirshim_cv_cxx_template_repository=
	echo configure:__oline__: here is the program that failed: >&5
	cat conftest2.C >&5
	echo configure:__oline__: here is conftest.h: >&5
	cat conftest.h >&5
    else
	rm -rf conftest*

	for mpirshim_file in `ls`
	do
	    if test "$mpirshim_file" != "." && test "$mpirshim_file" != ".."; then
		# Is it a directory?
		if test -d "$mpirshim_file"; then
		    mpirshim_template_dir="$mpirshim_file $mpirshim_template_dir"

		# Or is it a file?
		else
		    name="`echo $mpirshim_file | cut -d. -f1`"

		    temp_mask=
		    if test "$name" = "main" || test "$name" = "other"; then
			temp_mask="`echo $mpirshim_file | cut -d. -f2`"
			if test "$mpirshim_template_filemask" = ""; then
			mpirshim_template_filemask="$temp_mask";
			elif test "`echo $mpirshim_template_filemask | $GREP $temp_mask`" = ""; then
			mpirshim_template_filemask="$mpirshim_template_filemask $temp_mask"
			fi
		    fi
		fi
	    fi
	done
	if test "$mpirshim_template_filemask" != ""; then
	    temp_mask=
	    for mask in $mpirshim_template_filemask
	    do
		temp_mask="*.$mask $temp_mask"
	    done
	    mpirshim_template_filemask=$temp_mask
	fi
    fi
fi
mpirshim_cv_cxx_template_repository="$mpirshim_template_dir $mpirshim_template_filemask"

if test "`echo $mpirshim_cv_cxx_template_repository`" = ""; then
    mpirshim_cv_cxx_template_repository="not used"
fi
cd ..
rm -rf conf_tmp_$$

# Clean up
unset mpirshim_file mpirshim_template_failed mpirshim_template_dir])
