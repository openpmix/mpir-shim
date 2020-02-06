/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2010 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2009      Sun Microsystems, Inc.  All rights reserved.
 * Copyright (c) 2009-2013 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013      Mellanox Technologies, Inc.
 *                         All rights reserved.
 * Copyright (c) 2015-2017 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2015-2020 Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 * This file is included at the bottom of mpirshim_config.h, and is
 * therefore a) after all the #define's that were output from
 * configure, and b) included in most/all files in MPIRSHIM.
 *
 * Since this file is *only* ever included by mpirshim_config.h, and
 * mpirshim_config.h already has #ifndef/#endif protection, there is no
 * need to #ifndef/#endif protection here.
 */

#ifndef MPIRSHIM_CONFIG_H
#error "mpirshim_config_bottom.h should only be included from mpirshim_config.h"
#endif

/*
 * If we build a static library, Visual C define the _LIB symbol. In the
 * case of a shared library _USERDLL get defined.
 *
 * MPIRSHIM_BUILDING and _LIB define how mpirshim_config.h
 * handles configuring all of MPIRSHIM's "compatibility" code.  Both
 * constants will always be defined by the end of mpirshim_config.h.
 *
 * MPIRSHIM_BUILDING affects how much compatibility code is included by
 * mpirshim_config.h.  It will always be 1 or 0.  The user can set the
 * value before including either mpi.h or mpirshim_config.h and it will be
 * respected.  If mpirshim_config.h is included before mpi.h, it will
 * default to 1.  If mpi.h is included before mpirshim_config.h, it will
 * default to 0.
 */
#ifndef MPIRSHIM_BUILDING
#define MPIRSHIM_BUILDING 1
#endif

/*
 * Flex is trying to include the unistd.h file. As there is no configure
 * option or this, the flex generated files will try to include the file
 * even on platforms without unistd.h. Therefore, if we
 * know this file is not available, we can prevent flex from including it.
 */
#ifndef HAVE_UNISTD_H
#define YY_NO_UNISTD_H
#endif

/***********************************************************************
 *
 * code that should be in ompi_config_bottom.h regardless of build
 * status
 *
 **********************************************************************/

/*
 * BEGIN_C_DECLS should be used at the beginning of your declarations,
 * so that C++ compilers don't mangle their names.  Use END_C_DECLS at
 * the end of C declarations.
 */
#undef BEGIN_C_DECLS
#undef END_C_DECLS
#if defined(c_plusplus) || defined(__cplusplus)
# define BEGIN_C_DECLS extern "C" {
# define END_C_DECLS }
#else
#define BEGIN_C_DECLS          /* empty */
#define END_C_DECLS            /* empty */
#endif

/**
 * The attribute definition should be included before any potential
 * usage.
 */
#if MPIRSHIM_HAVE_ATTRIBUTE_ALIGNED
#    define __mpirshim_attribute_aligned__(a)    __attribute__((__aligned__(a)))
#    define __mpirshim_attribute_aligned_max__   __attribute__((__aligned__))
#else
#    define __mpirshim_attribute_aligned__(a)
#    define __mpirshim_attribute_aligned_max__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_ALWAYS_INLINE
#    define __mpirshim_attribute_always_inline__ __attribute__((__always_inline__))
#else
#    define __mpirshim_attribute_always_inline__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_COLD
#    define __mpirshim_attribute_cold__          __attribute__((__cold__))
#else
#    define __mpirshim_attribute_cold__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_CONST
#    define __mpirshim_attribute_const__         __attribute__((__const__))
#else
#    define __mpirshim_attribute_const__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_DEPRECATED
#    define __mpirshim_attribute_deprecated__    __attribute__((__deprecated__))
#else
#    define __mpirshim_attribute_deprecated__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_FORMAT
#    define __mpirshim_attribute_format__(a,b,c) __attribute__((__format__(a, b, c)))
#else
#    define __mpirshim_attribute_format__(a,b,c)
#endif

/* Use this __atribute__ on function-ptr declarations, only */
#if MPIRSHIM_HAVE_ATTRIBUTE_FORMAT_FUNCPTR
#    define __mpirshim_attribute_format_funcptr__(a,b,c) __attribute__((__format__(a, b, c)))
#else
#    define __mpirshim_attribute_format_funcptr__(a,b,c)
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_HOT
#    define __mpirshim_attribute_hot__           __attribute__((__hot__))
#else
#    define __mpirshim_attribute_hot__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_MALLOC
#    define __mpirshim_attribute_malloc__        __attribute__((__malloc__))
#else
#    define __mpirshim_attribute_malloc__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_MAY_ALIAS
#    define __mpirshim_attribute_may_alias__     __attribute__((__may_alias__))
#else
#    define __mpirshim_attribute_may_alias__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_NO_INSTRUMENT_FUNCTION
#    define __mpirshim_attribute_no_instrument_function__  __attribute__((__no_instrument_function__))
#else
#    define __mpirshim_attribute_no_instrument_function__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_NOINLINE
#    define __mpirshim_attribute_noinline__  __attribute__((__noinline__))
#else
#    define __mpirshim_attribute_noinline__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_NONNULL
#    define __mpirshim_attribute_nonnull__(a)    __attribute__((__nonnull__(a)))
#    define __mpirshim_attribute_nonnull_all__   __attribute__((__nonnull__))
#else
#    define __mpirshim_attribute_nonnull__(a)
#    define __mpirshim_attribute_nonnull_all__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_NORETURN
#    define __mpirshim_attribute_noreturn__      __attribute__((__noreturn__))
#else
#    define __mpirshim_attribute_noreturn__
#endif

/* Use this __atribute__ on function-ptr declarations, only */
#if MPIRSHIM_HAVE_ATTRIBUTE_NORETURN_FUNCPTR
#    define __mpirshim_attribute_noreturn_funcptr__  __attribute__((__noreturn__))
#else
#    define __mpirshim_attribute_noreturn_funcptr__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_PACKED
#    define __mpirshim_attribute_packed__        __attribute__((__packed__))
#else
#    define __mpirshim_attribute_packed__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_PURE
#    define __mpirshim_attribute_pure__          __attribute__((__pure__))
#else
#    define __mpirshim_attribute_pure__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_SENTINEL
#    define __mpirshim_attribute_sentinel__      __attribute__((__sentinel__))
#else
#    define __mpirshim_attribute_sentinel__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_UNUSED
#    define __mpirshim_attribute_unused__        __attribute__((__unused__))
#else
#    define __mpirshim_attribute_unused__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_VISIBILITY
#    define __mpirshim_attribute_visibility__(a) __attribute__((__visibility__(a)))
#else
#    define __mpirshim_attribute_visibility__(a)
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_WARN_UNUSED_RESULT
#    define __mpirshim_attribute_warn_unused_result__ __attribute__((__warn_unused_result__))
#else
#    define __mpirshim_attribute_warn_unused_result__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_WEAK_ALIAS
#    define __mpirshim_attribute_weak_alias__(a) __attribute__((__weak__, __alias__(a)))
#else
#    define __mpirshim_attribute_weak_alias__(a)
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_DESTRUCTOR
#    define __mpirshim_attribute_destructor__    __attribute__((__destructor__))
#else
#    define __mpirshim_attribute_destructor__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_OPTNONE
#    define __mpirshim_attribute_optnone__    __attribute__((__optnone__))
#else
#    define __mpirshim_attribute_optnone__
#endif

#if MPIRSHIM_HAVE_ATTRIBUTE_EXTENSION
#    define __mpirshim_attribute_extension__    __extension__
#else
#    define __mpirshim_attribute_extension__
#endif

#  if MPIRSHIM_C_HAVE_VISIBILITY
#    define MPIRSHIM_EXPORT           __mpirshim_attribute_visibility__("default")
#  else
#    define MPIRSHIM_EXPORT
#  endif

#if !defined(__STDC_LIMIT_MACROS) && (defined(c_plusplus) || defined (__cplusplus))
/* When using a C++ compiler, the max / min value #defines for std
   types are only included if __STDC_LIMIT_MACROS is set before
   including stdint.h */
#define __STDC_LIMIT_MACROS
#endif
#include "mpirshim_config.h"
#include "mpirshim_stdint.h"

/***********************************************************************
 *
 * Code that is only for when building MPIRSHIM or utilities that are
 * using the internals of MPIRSHIM.  It should not be included when
 * building MPI applications
 *
 **********************************************************************/
#if MPIRSHIM_BUILDING

/*
 * Maximum size of a filename path.
 */
#include <limits.h>
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if defined(PATH_MAX)
#define MPIRSHIM_PATH_MAX   (PATH_MAX + 1)
#elif defined(_POSIX_PATH_MAX)
#define MPIRSHIM_PATH_MAX   (_POSIX_PATH_MAX + 1)
#else
#define MPIRSHIM_PATH_MAX   256
#endif

/*
 * Set the compile-time path-separator on this system and variable separator
 */
#define MPIRSHIM_PATH_SEP "/"
#define MPIRSHIM_ENV_SEP  ':'

#if defined(MAXHOSTNAMELEN)
#define MPIRSHIM_MAXHOSTNAMELEN (MAXHOSTNAMELEN + 1)
#elif defined(HOST_NAME_MAX)
#define MPIRSHIM_MAXHOSTNAMELEN (HOST_NAME_MAX + 1)
#else
/* SUSv2 guarantees that "Host names are limited to 255 bytes". */
#define MPIRSHIM_MAXHOSTNAMELEN (255 + 1)
#endif

#define MPIRSHIM_DEBUG_ZERO(obj)

/*
 * Define __func__-preprocessor directive if the compiler does not
 * already define it.  Define it to __FILE__ so that we at least have
 * a clue where the developer is trying to indicate where the error is
 * coming from (assuming that __func__ is typically used for
 * printf-style debugging).
 */
#if defined(HAVE_DECL___FUNC__) && !HAVE_DECL___FUNC__
#define __func__ __FILE__
#endif

#define IOVBASE_TYPE  void

/* ensure the bool type is defined as it is used everywhere */
#include <stdbool.h>

/**
 * If we generate our own bool type, we need a special way to cast the result
 * in such a way to keep the compilers silent.
 */
#  define MPIRSHIM_INT_TO_BOOL(VALUE)  (bool)(VALUE)

#if defined(__APPLE__) && defined(HAVE_INTTYPES_H)
/* Prior to Mac OS X 10.3, the length modifier "ll" wasn't
   supported, but "q" was for long long.  This isn't ANSI
   C and causes a warning when using PRI?64 macros.  We
   don't support versions prior to OS X 10.3, so we dont'
   need such backward compatibility.  Instead, redefine
   the macros to be "ll", which is ANSI C and doesn't
   cause a compiler warning. */
#include <inttypes.h>
#if defined(__PRI_64_LENGTH_MODIFIER__)
#undef __PRI_64_LENGTH_MODIFIER__
#define __PRI_64_LENGTH_MODIFIER__ "ll"
#endif
#if defined(__SCN_64_LENGTH_MODIFIER__)
#undef __SCN_64_LENGTH_MODIFIER__
#define __SCN_64_LENGTH_MODIFIER__ "ll"
#endif
#endif

#ifdef MCS_VXWORKS
/* VXWorks puts some common functions in oddly named headers.  Rather
   than update all the places the functions are used, which would be a
   maintenance disaster, just update here... */
#ifdef HAVE_IOLIB_H
/* pipe(), ioctl() */
#include <ioLib.h>
#endif
#ifdef HAVE_SOCKLIB_H
/* socket() */
#include <sockLib.h>
#endif
#ifdef HAVE_HOSTLIB_H
/* gethostname() */
#include <hostLib.h>
#endif
#endif

/* If we're in C++, then just undefine restrict and then define it to
   nothing.  "restrict" is not part of the C++ language, and we don't
   have a corresponding AC_CXX_RESTRICT to figure out what the C++
   compiler supports. */
#if defined(c_plusplus) || defined(__cplusplus)
#undef restrict
#define restrict
#endif

#else

/* For a similar reason to what is listed in mpirshim_config_top.h, we
   want to protect others from the autoconf/automake-generated
   PACKAGE_<foo> macros in mpirshim_config.h.  We can't put these undef's
   directly in mpirshim_config.h because they'll be turned into #defines'
   via autoconf.

   So put them here in case any only else includes OMPI/MPIRSHIM's
   config.h files. */

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE_URL
#undef HAVE_CONFIG_H

#endif /* MPIRSHIM_BUILDING */
