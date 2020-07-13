/*
 * Copyright (c) 2020      IBM Corporation.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
#include "mpirshim.h"
#include "mpirshim_config.h"

#include <stdio.h>
#include <stdlib.h>
#pragma push_macro("_GNU_SOURCE")
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <libgen.h>
#pragma pop_macro("_GNU_SOURCE")
#include <unistd.h>
#include <string.h>
#include <argp.h>

/* Use argp to parse the command line
 *   https://www.gnu.org/software/libc/manual/html_node/Argp.html
 */
static error_t mpir_parse_opt(int key, char *arg, struct argp_state *state);
static void mpir_version_hook(FILE *stream, struct argp_state *state);

static char args_doc[] = "[LAUNCHER] [ARGS] PROG [PROG-ARGS]";
static char args_extra_doc[] = 
    "MPIR Shim wrapper program\n"
    "\n"
    "LAUNCHER:\n"
    "  Name of a PMIx launcher, such as \"prun\", \"prterun\", or \"mpirun\".\n"
    "\n"
    "ARGS:\n"
    "  Arguments to pass to LAUNCHER, such as \"-n 3\".\n"
    "\n"
    "PROG:\n"
    "  Application program for LAUNCHER to launch.\n"
    "\n"
    "PROG-ARGS:\n"
    "  Arguments to pass to PROG.)\n"
    "\n"
    "A \"proxy run\" assumes that there is no persistent PMIx DVM running and\n"
    "the LAUNCHER will start a temporary DVM (e.g., \"mpirun\").\n"
    "\n"
    "A \"non-proxy run\" assumes that there is a persistent PMIx DVM running and\n"
    "the LAUNCHER will connect to that entity to start the application\n"
    "\n"
    "In the \"attach\" mode the launcher arguments are ignored, if present.\n"
    "\n"
    "By default, if LAUNCHER is named \"prun\" then a non-proxy run is performed,\n"
    "otherwise a proxy run is done.\n"
    "\n"
    "OPTIONS:";
#define ARGS_PMIX_PREFIX 0x80 // 128
static struct argp_option args_options[] =
    {
        {"debug",               'd', 0,     0, "Debugging output"},
        {"force-proxy-run",     'p', 0,     0, "Force a proxy run. (e.g., prterun)"},
        {"force-non-proxy-run", 'n', 0,     0, "Force a non-proxy run. (e.g., prun)"},
        {"pid",                 'c', "PID", 0, "Attach Mode: PID of launcher"},
        {"attach",              'c', "PID", OPTION_ALIAS, ""},
        {"pmix-prefix",         ARGS_PMIX_PREFIX, "PATH", 0, "PMIx Library to use"},
        {0}
    };
static struct argp argp = { args_options, mpir_parse_opt, args_doc, args_extra_doc};

struct mpir_args_t {
    pid_t pid;
    int debug;
    mpir_shim_mode_t mpir_mode;
    int num_run_args;
    char **run_args;
    char *pmix_prefix;
};
typedef struct mpir_args_t mpir_args_t;

/**
 * @name  mpir_parse_opt
 * @brief argp command line option parser
 * @param key: Single character version of the command line options
 * @param arg: Pointer to the argument provided to this option, if any.
 * @param state: The argp state object
 * @return 0 if successful, non-zero otherwise
 */
static error_t mpir_parse_opt(int key, char *arg, struct argp_state *state)
{
    mpir_args_t *mpir_args = state->input;
    char *endp = NULL;
    size_t len = 0;

    switch(key)
        {
        case 'c':
            mpir_args->pid = strtol(arg, &endp, 10);
            if ('\0' != *endp) {
                fprintf(stderr, "Error: Invalid PID for the PMIx Server: '%s'.\n", arg);
                exit(1);
            }
            mpir_args->mpir_mode = MPIR_SHIM_ATTACH_MODE;
            break;
        case 'd':
            mpir_args->debug = 1;
            break;
        case 'n':
            mpir_args->mpir_mode = MPIR_SHIM_NONPROXY_MODE;
            break;
        case 'p':
            mpir_args->mpir_mode = MPIR_SHIM_PROXY_MODE;
            break;
        case ARGS_PMIX_PREFIX:
            if (NULL != mpir_args->pmix_prefix) {
                fprintf(stderr, "Error: Multiple --pmix-prefix options provided.\n");
                exit(1);
            }
            if ('/' != arg[0]) {
                fprintf(stderr, "Error: Absolute path required for --pmix-prefix option.\n");
                exit(1);
            }
            if (0 != access(arg, R_OK)) {
                fprintf(stderr, "Error: --pmix-prefix directory does not exist. '%s'\n", arg);
                exit(1);
            }
            endp = NULL;
            len = strlen(arg) + strlen("/lib/libpmix.dylib") + 1;
            endp = (char*)malloc(sizeof(char) * len);
#if defined(__APPLE__)
            snprintf(endp, len, "%s/lib/libpmix.dylib", arg);
#else
            snprintf(endp, len, "%s/lib/libpmix.so", arg);
#endif
            if (0 != access(endp, F_OK)) {
                fprintf(stderr, "Error: --pmix-prefix directory does not contain 'lib/libpmix.so'. '%s'\n", arg);
                free(endp);
                exit(1);
            }
            free(endp);
            endp = NULL;
            mpir_args->pmix_prefix = arg;
            break;
        case ARGP_KEY_ARG:
            // Skip to 'ARGP_KEY_ARGS' to consume the rest of the string
            return ARGP_ERR_UNKNOWN;
        case ARGP_KEY_ARGS:
            mpir_args->run_args     = state->argv + state->next;
            mpir_args->num_run_args = state->argc - state->next;
            // Stop parser here
            state->next = state->argc;
            return ARGP_KEY_SUCCESS;
            break;
        case ARGP_KEY_END: // Finished parsing
        case ARGP_KEY_INIT:
        case ARGP_KEY_FINI:
            break;
        default:
            return ARGP_ERR_UNKNOWN;
        }

    return 0;
}

/**
 * @name  mpir_version_hook
 * @brief Display version information when requested
 * @param stream: The IO stream to use when displaying the version string
 * @param state: The argp state object
 */
static void mpir_version_hook(FILE *stream, struct argp_state *state)
{
    fprintf(stream,
            "MPIR Shim program for PMIx launchers.\n"
            "Version %s [%s]\n",
            PACKAGE_VERSION,
            mpirshim_RELEASE_DATE);
}

/**
 * @name   main
 * @brief  Module entry point when this module is used as a MPIR shim module
 * @param  argc: Number of arguments passed to this module
 * @param  argv: Null terminated array of module arguments
 * @return 0 if successful, 1 if failed
 */
/***********************************************************************/
/*
 * A PMIx to MPIR shim program that extracts PMIx proctable information
 * from a PMIx launcher process (prun, mpirun, etc.) and uses it to
 * implement the MPIR specification.
 *
 * Used with PRRTE v2/OpenPMIx v4 or Open MPI v5 and later in conjunction with
 * legacy versions of tools that support MPIR only, for example:
 *
 *   totalview -args mpirc mpirun -n 32 mpi-program
 *
 * TotalView treats "mpirc" as an MPI starter program because it contains
 * the MPIR symbols.  When "mpirc" starts running, it uses PMIx to launch
 * "mpirun", which in turn will launch the "mpi-program".  When "mpirc"
 * receives the program-launch PMIx event, it extracts the process table
 * from PMIx, fills in the MPIR_proctable[], and calls MPIR_breakpoint.
 * When "mpirc" hits the MPIR_breakpoint, TotalView extracts the MPI
 * process information from the MPIR_proctable[] and attaches to the
 * processes in the job.
 *
 * See: https://github.com/openpmix/mpir-shim
 */
/***********************************************************************/
int main(int argc, char *argv[])
{
    int rc;
    mpir_args_t mpir_args;

    /*
     * Parse args
     */
    mpir_args.pid = 0;
    mpir_args.debug = 0;
    mpir_args.mpir_mode = MPIR_SHIM_DYNAMIC_PROXY_MODE;
    mpir_args.num_run_args = 0;
    mpir_args.run_args = NULL;
    mpir_args.pmix_prefix = NULL;

    argp_program_version_hook= mpir_version_hook;
    argp_program_bug_address = "the OpenPMIx mailing list or GitHub.\nhttps://openpmix.github.io";
    argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &mpir_args);
    if ((0 == mpir_args.pid) && (0 == mpir_args.num_run_args)) {
        fprintf(stderr, "No MPI application invocation specified, exiting.\n");
        exit(1);
    }

    /*
     * Call the main driver
     */
    rc = MPIR_Shim_common(mpir_args.mpir_mode,
                          mpir_args.pid,
                          mpir_args.debug,
                          mpir_args.num_run_args,
                          mpir_args.run_args,
                          mpir_args.pmix_prefix);

    exit(rc);
}
