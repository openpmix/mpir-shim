/*
 * Copyright (c) 2020      IBM Corporation.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 * @file  mpirshim_test.c
 * @brief A small test program that uses the MPIR symbols to display the
 *        proctable structure. This test can be used in proxy, non-proxy, and
 *        attach modes.
 *
 * Proxy mode: (a.out is a MPI program)
 *   mpirshim_test mpirun -np 2 ./a.out
 *
 * Non-proxy mode: (a.out is a MPI program)
 *   prte --daemonize
 *   mpirshim_test prun -np 2 ./a.out
 *   pterm
 *
 * Expected output shoud look something like (Last two lines from the application:
 *   MPIR_Breakpoint called
 *   MPIR_debug_state is 1
 *   Task: 0 host: node01 ; executable: ./a.out ; pid: 39137
 *   Task: 1 host: node01 ; executable: ./a.out ; pid: 39138
 *   Pause application for 2 seconds. Testing hold in init
 *   Release application
 *     1/  2) [node01] Running...
 *     0/  2) [node01] Running...
 */
#include "mpirshim.h"
#include "mpirshim_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma push_macro("_GNU_SOURCE")
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <libgen.h>
#pragma pop_macro("_GNU_SOURCE")
#include <unistd.h>
#include <argp.h>

/* Use argp to parse the command line
 *   https://www.gnu.org/software/libc/manual/html_node/Argp.html
 */
static error_t mpir_parse_opt(int key, char *arg, struct argp_state *state);

static char args_doc[] = "[LAUNCHER with args]";
static char args_extra_doc[] = 
    "MPIR Shim test program\n"
    "\n"
    "OPTIONS:";
static struct argp_option args_options[] =
    {
        {"debug",               'd', 0,     0, "Debugging output"},
        {"force-proxy-run",     'p', 0,     0, "Force a proxy run. (e.g., prterun)"},
        {"force-non-proxy-run", 'n', 0,     0, "Force a proxy run. (e.g., prterun)"},
        {"pid",                 'c', "PID", 0, "Attach Mode: PID of launcher"},
        {"attach",              'c', "PID", OPTION_ALIAS, ""},
        {"sleep",               's', "SEC", 0, "Time to sleep in MPIR_Breakpoint_hook (Default: 2 seconds)"},
        {0}
    };
static struct argp argp = { args_options, mpir_parse_opt, args_doc, args_extra_doc};

static int sleep_hook = 2;
struct mpir_args_t {
    pid_t pid;
    int debug;
    mpir_shim_mode_t mpir_mode;
    int num_run_args;
    char **run_args;
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

    switch(key)
        {
        case 'c':
            mpir_args->pid = strtol(arg, &endp, 10);
            if ('\0' != *endp) {
                fprintf(stderr, "Invalid PID for the PMIx Server: '%s'.\n", arg);
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
        case 's':
            sleep_hook = strtol(arg, &endp, 10);
            if ('\0' != *endp || sleep_hook < 0 ) {
                fprintf(stderr, "Invalid number supplied for -s: '%s'.\n", arg);
                exit(1);
            }
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

typedef struct MPIR_PROCDESC {
  const char *host_name;
  const char *executable_name;
  int pid;
} MPIR_PROCDESC;

extern int MPIR_debug_state;
extern int MPIR_proctable_size;
extern MPIR_PROCDESC *MPIR_proctable;

void MPIR_Breakpoint_hook(void)
{
    int i;
    int rc;

    printf("MPIR_Breakpoint called\n");
    printf("MPIR_debug_state is %d\n", MPIR_debug_state);
    if (1 == MPIR_debug_state) {
        for (i = 0; i < MPIR_proctable_size; i++) {
            printf("Task: %d host: %s ; executable: %s ; pid: %d\n", i,
                   MPIR_proctable[i].host_name,
                   MPIR_proctable[i].executable_name,
                   MPIR_proctable[i].pid);
            if (0 == MPIR_proctable[i].pid) {
                printf("Error: Rank %d pid is zero.\n", i);
            }
        }
    }

    printf("Pause application for %d seconds. Testing hold in init\n", sleep_hook);
    sleep(sleep_hook);

    printf("Release application\n");
    rc = MPIR_Shim_release_application();
    if (0 != rc) {
        printf("MPIR_Shim_release_application failed\n");
    }
    fflush(stdout);
}

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
                          NULL);

    exit(rc);
}
