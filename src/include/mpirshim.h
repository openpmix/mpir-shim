/*
 * Copyright (c) 2020      IBM Corporation.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MPIRSHIM_H
#define MPIRSHIM_H

#include <unistd.h>

/*
 * The VOLATILE macro is defined to the volatile keyword for C++ and
 * ANSI C. Declaring a variable volatile informs the compiler that the
 * variable could be modified by an external entity and that its value
 * can change at any time.  VOLATILE is used with MPIR variables
 * defined in the starter and MPI processes but are set by the tool.
 *
 * Note: This macro is required by the MPIR specification
 */
#ifndef VOLATILE
#if defined(__STDC__) || defined(__cplusplus)
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

/**
 * Operative modes
 *  - DYNAMIC_PROXY_MODE = determine based upon argv[0]
 *  - PROXY_MODE         = Force proxy mode
 *  - NONPROXY_MODE      = Force non-proxy mode
 *  - ATTACH_MODE        = Force attach mode (requires pid_)
 */
typedef enum {
    MPIR_SHIM_DYNAMIC_PROXY_MODE = 0,
    MPIR_SHIM_PROXY_MODE,
    MPIR_SHIM_NONPROXY_MODE,
    MPIR_SHIM_ATTACH_MODE
} mpir_shim_mode_t;

/**
 * @name   MPIR_Shim_common
 * @brief  Common top-level processing for this module, used when this module is
 *         invoked as a shim module between the tool and the launcher, and when
 *         this module is used as a shared library when used in a MPIR testcase.
 * @param  mpir_mode_: Force this into proxy mode or non-proxy mode (Default: dynamic)
 * @param  pid_: Connect to this PID in an attach mode (Default: disabled = 0)
 * @param  debug_: Enable debugging output and tracing (Default: Disabled)
 * @param  argc: Number of launcher and application command line arguments
 * @param  argv: Array of launcher and application command line arguments, terminated with NULL entry.
 * @param  pmix_prefix_: Value to set PMIX_PREFIX info key when call tool init. NULL if skip.
 * @return 0 if successful, 1 if failed
 */
int MPIR_Shim_common(mpir_shim_mode_t mpir_mode_, pid_t pid_, int debug_,
                     int argc, char *argv[], const char *pmix_prefix_);

#endif /* MPIRSHIM_H */
