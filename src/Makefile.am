#
# Copyright (c) 2020      Intel, Inc.  All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

AM_CPPFLAGS = -I$(top_builddir)/src/include

bin_PROGRAMS = mpir

mpir_SOURCES = mpir.cxx
mpir_CPPFLAGS = $(pmix_CPPFLAGS)
mpir_LDFLAGS = $(pmix_LDFLAGS)
mpir_LDADD =  $(pmix_LIBS)
