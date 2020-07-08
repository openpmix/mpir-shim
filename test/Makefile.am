#
# Copyright (c) 2020      Intel, Inc.  All rights reserved.
# Copyright (c) 2020      IBM Corporation.  All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

AM_CPPFLAGS = -I$(top_builddir)/src/include

noinst_PROGRAMS = mpirshim_test
mpirshim_test_SOURCES = mpirshim_test.c $(top_builddir)/src/include/mpirshim.h $(top_builddir)/src/include/mpirshim_test.h
mpirshim_test_CFLAGS = $(pmix_CPPFLAGS) -DMPIR_SHIM_TESTCASE
mpirshim_test_CPPFLAGS = $(pmix_CPPFLAGS) -DMPIR_SHIM_TESTCASE
mpirshim_test_LDFLAGS = $(pmix_LDFLAGS)
mpirshim_test_LDADD =  $(pmix_LIBS) $(top_builddir)/src/libmpirshimtest.la
