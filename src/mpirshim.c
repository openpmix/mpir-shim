/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2013 Los Alamos National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2009-2012 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2013-2020 Intel, Inc.  All rights reserved.
 * Copyright (c) 2015      Mellanox Technologies, Inc.  All rights reserved.
 * Copyright (c) 2020      IBM Corporation.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 * @file   mpir.c
 * @brief  This module implements a shim layer between a tool such as a
 *         debugger and a PMIx-based MPI application launcher such as mpirun.
 *         The tool interacts with this module, reading application process
 *         mapping information through MPIR_* variables as defined in the
 *         MPIR document at
 *         https://www.mpi-forum.org/docs/mpir-specification-03-01-2018.pdf
 *
 */

#include "mpirshim_config.h"
#include "mpirshim.h"

#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#pragma push_macro("_GNU_SOURCE")
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <libgen.h>
#pragma pop_macro("_GNU_SOURCE")
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <pmix_tool.h>


/**********************************************************************/
/* Print a fatal error message along with the PMIx status (if it's not
   PMIX_SUCCESS), finalize the tool, and exit. */
static void pmix_fatal_error(pmix_status_t rc, const char *format, ...);


/**********************************************************************/
/* Internal "debug printf" function. */
static void debug_print(char *format, ...);

/**********************************************************************/
/* Funtion entry and exit tracing */
#define MPIR_SHIM_DEBUG_ENTER(...) { MPIR_SHIM_DEBUG_ENTER_(__VA_ARGS__, ""); }
#define MPIR_SHIM_DEBUG_ENTER_(format, ...)                             \
    debug_print(">>> ENTER (%s): " format "\n", __FUNCTION__, ##__VA_ARGS__);

#define MPIR_SHIM_DEBUG_EXIT(...) { MPIR_SHIM_DEBUG_EXIT_(__VA_ARGS__, ""); }
#define MPIR_SHIM_DEBUG_EXIT_(format, ...)                             \
    debug_print("<<< EXIT  (%s): " format "\n", __FUNCTION__, ##__VA_ARGS__);


/**********************************************************************
 * MPIR definitions.
 *
 * The following comments and definitions were taken from "The MPIR
 * Process Acquisition Interface, Version 1.1 document."
 *
 * See: https://www.mpi-forum.org/docs/mpir-specification-03-01-2018.pdf
 *
 * We do NOT implement the entire MPIR interface here, just the parts
 * needed for basic MPIR support.
 **********************************************************************/

/*
 * MPIR_PROCDESC is a typedef name for an anonymous structure that
 * holds process descriptor information for a single MPI process.
 * The structure must contain three members with the same names and
 * types as specified above.  The tool must use the symbol table
 * information to determine the overall size of the structure, and
 * offset and size of each of the structures members.
 */
typedef struct MPIR_PROCDESC {
  char *host_name;
  char *executable_name;
  int pid;
} MPIR_PROCDESC;

/*
 * MPIR_being_debugged is an integer variable that is set or cleared by
 * the tool to notify the starter process that a tool is present.
 *
 * For OpenMPI, this variable only exists within this shim module. It is set
 * when the launcher process is spawned and cleared when the launcher
 * terminates. The MPIR document states this may also exist in application
 * processes so that a (MPI) runtime may be able to check if the application
 * task is currently being debugged and act accordingly.
 * For OpenMPI, this symbol will not exist in the application process, but that
 * is of no concern since no OpenMPI runtime code will be checking it either.
 *
 */
VOLATILE int MPIR_being_debugged = 0;

/*
 * MPIR_proctable is a pointer variable set by the starter process that
 * points to an array of MPIR_PROCDESC structures containing
 * MPIR_proctable_size elements. This array of structures is the process
 * descriptor table.
 */
MPIR_PROCDESC *MPIR_proctable = 0;

/*
 * MPIR_proctable_size is an integer variable set by the starter process
 * that specifies the number of elements in the procedure descriptor
 * table pointed to by the MPIR_proctable variable.
 */
int MPIR_proctable_size = 0;


#define MPIR_NULL           0   /* The tool should ignore the event and continue
                                   the starter process.*/
#define MPIR_DEBUG_SPAWNED  1   /* the starter process has spawned the MPI
                                   processes and filled in the process
                                   descriptor table. The tool can attach to any
                                   additional MPI processes that have appeared
                                   in the process descriptor table.
                                   This is known as a “job spawn event”. */
#define MPIR_DEBUG_ABORTING 2   /* The MPI job has aborted and the tool can
                                   notify the user of the abort condition. The
                                   tool can read the reason for the job by
                                   reading the character string out of the
                                   starter process, which is pointed to by the
                                   MPIR_debug_abort_string variable in the
                                   starter process. */

/*
 * MPIR_debug_state is an integer value set in the starter process that
 * specifies the state of the MPI job at the point where the starter
 * process calls the MPIR_Breakpoint function.
 *
 * For Open MPI, there does not seem to be a way to detect that the MPI
 * application has aborted, so the only state change posted is MPI_DEBUG_SPAWNED
*/
VOLATILE int MPIR_debug_state = MPIR_NULL;

/*
 * MPIR_debug_abort_string is a pointer to a null-terminated character
 * string set by the starter process when MPI job has aborted. When an
 * MPIR_DEBUG_ABORTING event is reported, the tool can read the reason
 * for aborting the job by reading the character string out of the
 * starter process. The abort reason string can then be reported to the
 * user, and is intended to be a human readable string.
 */
char *MPIR_debug_abort_string = NULL;


/*
 * MPIR_i_am_starter is a symbol of any type (preferably int) that marks
 * the process containing the symbol definition as a starter process
 * that is not also an MPI process. This symbol serves as a flag to mark
 * the process as a separate starter process or an MPI rank 0 process.
 * We define this since this shim module is not the application MPI task rank 0.
 */
int MPIR_i_am_starter;

/*
 * MPIR_force_to_main is a symbol of any type (preferably int) that
 * informs the tool that it should display the source code of the main
 * subprogram after acquiring the MPI processes. The presence of the
 * symbol MPIR_force_to_main does not imply that the MPI processes have
 * been stopped before dynamic linking has occurred.
 */
int MPIR_force_to_main;

/*
 * MPIR_partial_attach_ok is a symbol of any type (preferably int) that
 * informs the tool that the MPI implementation supports attaching to a
 * subset of the MPI processes.
 */
int MPIR_partial_attach_ok;

/*
 * MPIR_ignore_queues is a symbol of any type (preferably int) that
 * informs the tool that MPI message queues support should be
 * suppressed. This is useful when the MPIR Process Acquisition
 * Interface is being used in a non-MPI environment.
 */
int MPIR_ignore_queues;

/*
 * MPIR_Breakpoint is the subroutine called by the starter process to
 * notify the tool that an MPIR event has occurred. The starter process
 * must set the MPIR_debug_state variable to an appropriate value before
 * calling this subroutine. The tool must set a breakpoint at the
 * MPIR_Breakpoint function, and when a thread running the starter
 * process hits the breakpoint, the tool must read the value of the
 * MPIR_debug_state variable to process an MPIR event.
 * This function is called once MPIR_proctable has been populated when the
 * application is being launched and when the application is connected to.
 * If the user does not set any application breakpoints that will be hit
 * during execution before resuming the mpirun process past this point,
 * then the debugger will not get control until the application exits, aborts,
 * or traps on a signal sent to an application process.
 */
void MPIR_Breakpoint(void);

void MPIR_Breakpoint(void)
{
    MPIR_SHIM_DEBUG_ENTER("");

#ifdef MPIR_SHIM_TESTCASE
#pragma weak MPIR_Breakpoint_hook
    void MPIR_Breakpoint_hook(void);
    debug_print("MPI_Breakpoint_hook=%p\n", MPIR_Breakpoint_hook);
    if (NULL != MPIR_Breakpoint_hook) {
        MPIR_Breakpoint_hook();
    }
#endif

    return;
}

/*
 * If the following variables are compiled into the code as external symbols
 * where the variables may be of any type, int preferred, that is an indicator
 * to the connecting tool that the feature they control is requested. For
 * Open MPI, these features are not used, so the variables are not defined.
 *
 * VOLATILE int MPIR_debug_gate
 *   - Part of the MPI application not the RM wrapper
 * int MPIR_aquired_pre_main
 *   - MPI processes are suspended before entry to main().
 * char MPIR_executable_path[256]
 *   - 
 * char MPIR_server_arguments[1024
 *   - 
 * char MPIR_attach_fifo[256]
 *   - 
 */
 
/*******************************************************************************
*                           End of MPIR declarations                           *
*******************************************************************************/
#define STATUS_OK 0
#define STATUS_FAIL 1

typedef struct MPIR_Shim_Condition {
    char *name;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int flag;
} MPIR_Shim_Condition;


// Initialize/Finalize this tool
static int initialize_as_tool(void);
static int finalize_as_tool(void);

// Connect this tool to a server
static int connect_to_server(void);

// Access MPIR Proctable
static int pmix_proc_table_to_mpir(void);

// PMIx Spawn of launcher which will then spawn the application
static int spawn_launcher_and_application(void);

// Register various event handlers
static int register_default_event_handler(void);
static int register_launcher_complete_handler(void);
static int register_launcher_ready_handler(void);
static int register_launcher_terminate_handler(void);
static int register_application_terminate_handler(void);

// Handlers for the various events
static void registration_complete_handler(pmix_status_t status,
                                          size_t handler_ref, void *cbdata);
static void default_event_handler(size_t handler_id, pmix_status_t status,
                                  const pmix_proc_t *source,
                                  pmix_info_t info[], size_t ninfo,
                                  pmix_info_t results[], size_t nresults,
                                  pmix_event_notification_cbfunc_fn_t cbfunc, void *cbdata);
static void launcher_complete_handler(size_t handler_id, pmix_status_t status,
                                      const pmix_proc_t *source,
                                      pmix_info_t info[], size_t ninfo,
                                      pmix_info_t results[], size_t nresults,
                                      pmix_event_notification_cbfunc_fn_t cbfunc,
                                      void *cbdata);
static void launcher_ready_handler(size_t handler_id, pmix_status_t status,
                                   const pmix_proc_t *source,
                                   pmix_info_t info[], size_t ninfo,
                                   pmix_info_t results[], size_t nresults,
                                   pmix_event_notification_cbfunc_fn_t cbfunc,
                                   void *cbdata);
static void launcher_terminate_handler(size_t handler_id, pmix_status_t status,
                                       const pmix_proc_t *source,
                                       pmix_info_t info[], size_t ninfo,
                                       pmix_info_t results[], size_t nresults,
                                       pmix_event_notification_cbfunc_fn_t cbfunc,
                                       void *cbdata);
static void application_terminate_handler(size_t handler_id, pmix_status_t status,
                                          const pmix_proc_t *source,
                                          pmix_info_t info[], size_t ninfo,
                                          pmix_info_t results[], size_t nresults,
                                          pmix_event_notification_cbfunc_fn_t cbfunc,
                                          void *cbdata);

// atexit handler to make sure we cleanup
static void exit_handler(void);

// Signal handler to trigger cleanup
static void signal_handler(int signum);

// Utility functions
static void delete_directory(char *dirname);
static void wait_for_condition(MPIR_Shim_Condition *wait_cond);
static void post_condition(MPIR_Shim_Condition *wait_cond);
static void release_conditions(void);
static int setup_session_directory(void);
static int setup_signal_handlers(void);

// Command line options
static int process_options(mpir_shim_mode_t mpir_mode_, pid_t pid_, int debug_, int arg, char *argv[]);

// Query the launcher/application namespace
static int query_launcher_namespace(void);
static int query_application_namespace(void);

// Release all processes in the specified namespace
static int release_procs_in_namespace(char *namespace, pmix_rank_t rank);

// Send the launch directives to the waiting launcher
static int send_launch_directives(void);

// Environment
extern char **environ;

// Useful reference
static const int const_true = 1;

// Command line arguments after the mpir args
static int num_run_args;
static char **run_args;
static const char *pmix_prefix = NULL;
static char *tool_binary_name;

// General state flags
static int pmix_initialized = 0;
static int session_count = 0;
static int app_terminated;
static int app_exit_code = PMIX_SUCCESS;
static int launcher_terminated;
static int launcher_exit_code = PMIX_SUCCESS;

// Callback ids
static size_t callback_reg_id;
static pmix_status_t callback_reg_status;
static size_t default_cb_id = -1;
static size_t launch_complete_cb_id = -1;
static size_t launch_ready_cb_id = -1;
static size_t launcher_terminate_cb_id = -1;
static size_t app_terminate_cb_id = -1;

// CLI option: Connect to PID (-c)
static pid_t connect_pid;
// CLI option: Debugging (-d)
static char debug_active;
// CLI option: Use proxy (e.g., prterun) (-p)
static mpir_shim_mode_t mpir_mode = MPIR_SHIM_DYNAMIC_PROXY_MODE;

// Session / Rendezvous paths
static char session_dirname[_POSIX_PATH_MAX + 1];
static char rendezvous_filename[_POSIX_PATH_MAX + 1];

// PMIx names for various agents
static pmix_proc_t tool_proc;
static pmix_proc_t launcher_proc;
static pmix_proc_t application_proc;

// Synchronization controls
static MPIR_Shim_Condition launch_complete_cond = {"launch_complete",
       PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 1};
static MPIR_Shim_Condition launch_ready_cond = {"launch-ready",
       PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 1};
static MPIR_Shim_Condition launch_term_cond = {"launch-terminated",
       PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 1};
static MPIR_Shim_Condition registration_cond = {"callback-registration",
       PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 1};
static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;


/**
 * @name   pmix_fatal_error
 * @brief  Print a fatal error message along with the PMIx status, and exit
 * @param  rc: PMix status
 * @param  format: printf-style format string for message. Additional parameters
 *           follow as needed.
 */
static void pmix_fatal_error(pmix_status_t rc, const char *format, ...)
{
    va_list arg_list;

    fprintf(stderr, "FATAL ERROR: ");

    va_start(arg_list, format);
    vfprintf(stderr, format, arg_list);
    va_end (arg_list);

    if (PMIX_SUCCESS != rc) {
        fprintf(stderr, ": %s (%d)",
                PMIx_Error_string(rc), rc);
    }

    fprintf(stderr, "\n");

    finalize_as_tool();

    exit(1);
}  /* pmix_fatal_error */

/**
 * @name   debug_print
 * @brief  Print a debug message to stdout.
 * @param  format: printf-style format string for message. Additional parameters
 *           follow as needed.
 */
void debug_print(char *format, ...)
{
    va_list args;

    if (1 == debug_active) {
        pthread_mutex_lock(&print_lock);

        va_start(args, format);
        printf("DEBUG: ");
        vprintf(format, args);
        va_end(args);
        fflush(stdout);

        pthread_mutex_unlock(&print_lock);
    }
}

/**
 * @name   finalize_as_tool
 * @brief  Finalize the PMIx environment for this module.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int finalize_as_tool(void)
{
    pmix_status_t rc;

    MPIR_SHIM_DEBUG_ENTER("");

    if (0 < pmix_initialized) {
        debug_print("Call PMIx_tool_finalize (%d)\n", pmix_initialized);

        rc = PMIx_tool_finalize();
        --pmix_initialized;
        if (PMIX_SUCCESS != rc) {
            fprintf(stderr, "PMIx_tool_finalize failed: %s\n",
                    PMIx_Error_string(rc));
            MPIR_SHIM_DEBUG_EXIT("");
            return STATUS_FAIL;
        }
    }

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   initialize_as_tool
 * @brief  Initialize the PMIx environment for this module.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int initialize_as_tool(void)
{
    int rc, n;
    char tool_namespace[PMIX_MAX_NSLEN+1];
    pmix_info_t *attrs = NULL;
    size_t num_attrs = 0;
    int rank = 0;

    MPIR_SHIM_DEBUG_ENTER("");

    tool_namespace[0] = '\0';
    sprintf(tool_namespace, "%s.%d", tool_binary_name, getpid());
    debug_print("Requested Tool namespace of '%s'\n", tool_namespace);

    PMIX_PROC_LOAD(&tool_proc, tool_namespace, 0);

    if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
        num_attrs = 4;
        if (NULL != pmix_prefix) {
            ++num_attrs;
        }
        PMIX_INFO_CREATE(attrs, num_attrs);
        n = 0;
        /* Do not connect to a PMIx server yet */
        PMIX_INFO_LOAD(&attrs[n], PMIX_TOOL_DO_NOT_CONNECT, &const_true,
                       PMIX_BOOL);
        n++;
        /* We assign the unique namespace in this case */
        PMIX_INFO_LOAD(&attrs[n], PMIX_TOOL_NSPACE, tool_namespace,
                       PMIX_STRING);
        n++;
        /* We're always rank 0 */
        PMIX_INFO_LOAD(&attrs[n], PMIX_TOOL_RANK, &rank, PMIX_UINT32);
        n++;
        /* Tool is a launcher and needs rendezvous files created */
        PMIX_INFO_LOAD(&attrs[n], PMIX_LAUNCHER, &const_true, PMIX_BOOL);
        n++;
    }
    else if (MPIR_SHIM_ATTACH_MODE == mpir_mode) {
        num_attrs = 1;
        if (NULL != pmix_prefix) {
            ++num_attrs;
        }
        PMIX_INFO_CREATE(attrs, num_attrs);
        n = 0;
        /* The PID of the target server for a tool */
        PMIX_INFO_LOAD(&attrs[n], PMIX_SERVER_PIDINFO, &connect_pid, PMIX_PID);
        n++;
        session_count = 1;
    }
    else {
        num_attrs = 0;
        session_count = 1;
    }

    /* If the user provided an explicit path to the PMIx install */
    if (NULL != pmix_prefix) {
        if (0 == num_attrs) {
            num_attrs = 1;
            PMIX_INFO_CREATE(attrs, num_attrs);
            n = 0;
        }
        debug_print("PMIx Prefix: '%s'\n", pmix_prefix);
        PMIX_INFO_LOAD(&attrs[n], PMIX_PREFIX, pmix_prefix, PMIX_STRING);
        n++;
    }


    rc = PMIx_tool_init(&tool_proc, attrs, num_attrs);
    PMIX_INFO_FREE(attrs, num_attrs);

    if (PMIX_SUCCESS != rc) {
        fprintf(stderr, "Unable to initialize MPIR module, PMIx status: %s.\n",
                PMIx_Error_string(rc));
        MPIR_SHIM_DEBUG_EXIT("Failed");
        return STATUS_FAIL;
    }

    debug_print("Tool namespace='%s', rank=%d\n", tool_proc.nspace,
                tool_proc.rank);

    if (MPIR_SHIM_ATTACH_MODE == mpir_mode) {
        // Access Launcher information
        query_launcher_namespace();
    }

    pmix_initialized += 1;

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   setup_session_directory
 * @brief  Create the directory which will contain PMIx files for this session,
 *         and set up the pathname for the PMIx rendezvous file. If the TMPDIR
 *         environment variable is set, the session directory will be created
 *         in that directory, otherwise it will be created in /tmp.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int setup_session_directory(void)
{
    char *envp;
    char *temp_dir = NULL;

    MPIR_SHIM_DEBUG_ENTER("");

    envp = getenv("TMPDIR");
    if (NULL == envp) {
        temp_dir = "/tmp";
    }
    else {
        struct stat file_info;

        if (0 == stat(envp, &file_info)) {
            if (S_ISDIR(file_info.st_mode) &&
                (S_IRWXU == (file_info.st_mode & S_IRWXU))) {
                temp_dir = envp;
            }
            else {
                temp_dir = "/tmp";
            }
        }
        else {
            fprintf(stderr, "stat failed on the TMPDIR '%s'\n", envp);
            MPIR_SHIM_DEBUG_EXIT("");
            return STATUS_FAIL;
        }
    }

    snprintf(session_dirname, sizeof session_dirname, "%s/%s.session.%d.%d",
             temp_dir, tool_binary_name, geteuid(), getpid());
    snprintf(rendezvous_filename, sizeof rendezvous_filename, "%s/%s.rndz.%d",
             session_dirname, tool_binary_name, getpid());

    if (0 != mkdir(session_dirname, S_IRWXU)) {
        fprintf(stderr, "An error occured creating the session directory: %s.\n",
                strerror(errno));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    debug_print("session directory is %s\n", session_dirname);
    debug_print("rendezvoud file is %s\n", rendezvous_filename);

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   delete_directory
 * @brief  Recursiblely delete ths specified directory and its contents
 * @param  dirname: Pathname of the directory to be deleted.
 */
void delete_directory(char *dirname)
{
    DIR *dir;
    char name[_POSIX_PATH_MAX + 1];

    MPIR_SHIM_DEBUG_ENTER("Dirname '%s'", dirname);

    dir = opendir(dirname);
    if (NULL != dir) {
        struct dirent *entry;

        entry = readdir(dir);
        while (NULL != entry) {
            if (DT_DIR == entry->d_type) {
                if ((0 != strcmp(".", entry->d_name)) &&
                    (0 != strcmp("..", entry->d_name))) {
                    snprintf(name, sizeof name, "%s/%s", dirname,
                             entry->d_name);
                    delete_directory(name);
                }
            }
            else {
                snprintf(name, sizeof name, "%s/%s", dirname, entry->d_name);
                unlink(name);
            }
            entry = readdir(dir);
        }
    }
    closedir(dir);
    rmdir(dirname);

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   process_options
 * @brief  Process command line options
 * @param  mpir_mode_: Force this into proxy mode or non-proxy mode (Default: dynamic)
 * @param  pid_: Connect to this PID in a an attach mode (Default: disabled = 0)
 * @param  debug_: Enable debugging output and tracing (Default: Disabled)
 * @param  argc: Number of launcher and application command line arguments
 * @param  argv: Array of launcher and application command line arguments, terminated with NULL entry.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int process_options(mpir_shim_mode_t mpir_mode_, pid_t pid_, int debug_, int argc, char *argv[])
{
    char *launcher_base = NULL;

    if (MPIR_SHIM_DYNAMIC_PROXY_MODE == mpir_mode_) {
        launcher_base = strrchr(argv[0], '/');
        if( NULL == launcher_base ) {
            launcher_base = argv[0];
        } else {
            launcher_base = launcher_base + 1;
        }

        // Detect proxy mode based on the binary named used to launch
        if (0 == strcmp(launcher_base, "prun")) {
            mpir_mode = MPIR_SHIM_NONPROXY_MODE;
        }
        else {
            mpir_mode = MPIR_SHIM_PROXY_MODE;
        }
    }
    else if (MPIR_SHIM_ATTACH_MODE == mpir_mode_) {
        if (0 >= pid_) {
            fprintf(stderr, "Invalid connect pid %d.\n", (int)pid_);
            MPIR_SHIM_DEBUG_EXIT("");
            return STATUS_FAIL;
        }
        connect_pid = pid_;
        mpir_mode = MPIR_SHIM_ATTACH_MODE;
    }

    debug_active = (bool)debug_;

    num_run_args = argc;
    run_args     = argv;

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   exit_handler
 * @brief  atexit function to clean up resources obtained by this module.
 */
void exit_handler(void)
{
    int i;

    MPIR_SHIM_DEBUG_ENTER("");

    // PMIx_tool_finalize must be called to make sure the launcher exits
    finalize_as_tool();

    // In connect mode there is no session directory to delete
    if ((MPIR_SHIM_ATTACH_MODE != mpir_mode) && 
        ('\0' != session_dirname[0]) ) {
            delete_directory(session_dirname);
    }

    if (NULL != MPIR_proctable) {
        for (i = 0; i < MPIR_proctable_size; i++) {
            free( MPIR_proctable[i].host_name );
            free( MPIR_proctable[i].executable_name );
        }
        free(MPIR_proctable);
    }

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   signal_handler
 * @brief  Handle selected signals by calling exit to perform orderly shutdown,
 *         including running atexit handler functions.
 */
void signal_handler(int signum)
{
    MPIR_SHIM_DEBUG_ENTER("Signum: %d", signum);

    finalize_as_tool();

    // exit_handler will do further cleanup
    exit(1);
}

/**
 * @name   setup_signal_handlers
 * @brief  Register signal handlers for signals we want to trap in order to
 *         perform an orderly shutdown on receipt of those signals.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int setup_signal_handlers(void)
{
    struct sigaction signal_parms;
    int signals[] = {SIGHUP, SIGINT, SIGTERM};
    size_t i;

    MPIR_SHIM_DEBUG_ENTER("");

    signal_parms.sa_handler = signal_handler;
    signal_parms.sa_flags = SA_RESTART;
    sigemptyset(&signal_parms.sa_mask);
    signal_parms.sa_restorer = NULL;

    for (i = 0; i < (sizeof(signals) / sizeof(int)); i++) {
        if (-1 == sigaction(signals[i], &signal_parms, NULL)) {
            fprintf(stderr, "An error occured setting a signal handler: %s.\n",
                    strerror(errno));
            MPIR_SHIM_DEBUG_EXIT("");
            return STATUS_FAIL;
        }
    }

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   release_conditions
 * @brief  Post all condition variables so any threads waiting for them to
 *         be posted are not blocked and preventing this module's termination.
 */
void release_conditions()
{
    MPIR_SHIM_DEBUG_ENTER("");

    if (1 == registration_cond.flag) {
        pthread_cond_broadcast(&registration_cond.condition);
        registration_cond.flag = 0;
    }
    if (1 == launch_ready_cond.flag) {
        pthread_cond_broadcast(&launch_ready_cond.condition);
        launch_ready_cond.flag = 0;
    }
    if (1 == launch_term_cond.flag) {
        pthread_cond_broadcast(&launch_complete_cond.condition);
        launch_complete_cond.flag = 0;
    }
    if (1 == launch_term_cond.flag) {
        pthread_cond_broadcast(&launch_term_cond.condition);
        launch_term_cond.flag = 0;
    }

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   post_condition
 * @brief  Post a condition variable so any threads waiting for that condition
 *         can resume execution.
 * @param  wait_cond: The condition variable to post
 */
void post_condition(MPIR_Shim_Condition *wait_cond)
{
    MPIR_SHIM_DEBUG_ENTER("Condition '%s'", wait_cond->name);

    pthread_mutex_lock(&(wait_cond->mutex));
    pthread_cond_broadcast(&(wait_cond->condition));
    wait_cond->flag = 0;
    pthread_mutex_unlock(&(wait_cond->mutex));

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   wait_for_condition
 * @brief  Suspend a thread until the specified condition is posted.
 * @param  wait_cond: The condition to wait for completion
 */
void wait_for_condition(MPIR_Shim_Condition *wait_cond) 
{
    MPIR_SHIM_DEBUG_ENTER("Condition '%s'", wait_cond->name);

    pthread_mutex_lock(&(wait_cond->mutex));

    while ((1 == wait_cond->flag) && (0 == launcher_terminated)) {
        debug_print("Wait for condition %s to be posted\n", wait_cond->name);
        pthread_cond_wait(&(wait_cond->condition), &(wait_cond->mutex));
    }

    MPIR_SHIM_DEBUG_EXIT("Condition '%s'", wait_cond->name);

    // Reset condition flag in preparation for next wait on this condition.
    wait_cond->flag = 1;
    pthread_mutex_unlock(&(wait_cond->mutex));
}

/**
 * @name   registration_complete_handler
 * @brief  Handle notification that a callback has been registered.
 * @param  status: Event id for callback
 * @param  handler_ref: Callback id, used to de-register the handler
 * @param  cbdata: Data passed to this callback
 */
void registration_complete_handler(pmix_status_t status, size_t handler_ref, 
                                   void *cbdata)
{
    MPIR_SHIM_DEBUG_ENTER("Status '%s'", PMIx_Error_string(status));

    // PMIx callbacks do not provide a way to pass information back to the
    // code that invoked the callback. Therefore the registration callback
    // uses two global variables, callback_reg_status and callback_reg_id to
    // hold the needed information. The code which registered the callback
    // reads these variables. This requires that a callback registration
    // must complete before a subsequent callback can be registered in order 
    // to avoid clobbering variables.
    callback_reg_status = status;
    callback_reg_id = handler_ref;

    post_condition(&registration_cond);

    MPIR_SHIM_DEBUG_EXIT("String '%s'", (NULL == cbdata ? "null" : (char*)cbdata) );
}

/**
 * @name   default_event_handler
 * @brief  Default callback for notifications received by this module but not
 *         handled by another callback.
 * @param  handler_id: Callback id for this callback
 * @param: status: The event this handler was invoked for
 * @param  source: The source for the notification
 * @param  info: Array of pmix_info_t objects passed to this callback by sender
 * @param  ninfo: Number of elements in ninfo array
 * @param  results: Array of pmix_info_t objects from previous handlers in chain
 * @param  nresults: Number of elements in results array
 * @param  cbfunc: Function to be called to propagate notification
 * @param  cbdata: Data passed to this callback
 */
void default_event_handler(size_t handler_id, pmix_status_t status,
                           const pmix_proc_t *source,
                           pmix_info_t info[], size_t ninfo,
                           pmix_info_t results[], size_t nresults,
                           pmix_event_notification_cbfunc_fn_t cbfunc, void *cbdata)
{
    MPIR_SHIM_DEBUG_ENTER("Event '%s', nspace '%s', rank '%ld'",
                          PMIx_Error_string(status),
                          source ? source->nspace : "null",
                          source ? source->rank : -1L);

    if (PMIX_ERR_LOST_CONNECTION_TO_SERVER == status) {
        fprintf(stderr, "Connection to application being debugged was lost. (sessions %d)\n", session_count);
        // In non-proxy mode there can be 2 sessions since the code originally
        // connects to the server in PMIx_tool_init then again in 
        // PMIx_tool_connect_to_server. In this case, the first lost
        // connection shouldn't cause this module to exit.
        // Also, call _exit() to exit since the termination may occur within a
        // callback, and calling PMIx functions in the atexit handler while
        // within a callback can result in hangs.
        if (1 == session_count) {
            release_conditions();
            MPIR_SHIM_DEBUG_EXIT("");
            _exit(1);
        }
        session_count = session_count - 1;
    }

    if (NULL != cbfunc) {
        cbfunc(PMIX_EVENT_ACTION_COMPLETE, NULL, 0, NULL, NULL, cbdata);
    }

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   launcher_complete_handler
 * @brief  Callback to handle notificaton that launcher has completed spawning
 *         application processes.
 * @param  handler_id: Callback id for this callback
 * @param: status: The event this handler was invoked for
 * @param  source: The source for the notification
 * @param  info: Array of pmix_info_t objects passed to this callback by sender
 * @param  ninfo: Number of elements in ninfo array
 * @param  results: Array of pmix_info_t objects from previous handlers in chain
 * @param  nresults: Number of elements in results array
 * @param  cbfunc: Function to be called to propagate notification
 * @param  cbdata: Data passed to this callback
 *
 * This is an event notification function that we explicitly request
 * be called when the PMIX_LAUNCH_COMPLETE notification is issued.  It
 * gathers the namespace of the application and returns it in the
 * release object.  We need the application namespace to query the
 * job's proc table and allow it to run after the MPI_Breakpoint() is
 * called.
 */
void launcher_complete_handler(size_t handler_id, pmix_status_t status,
                               const pmix_proc_t *source,
                               pmix_info_t info[], size_t ninfo,
                               pmix_info_t results[], size_t nresults,
                               pmix_event_notification_cbfunc_fn_t cbfunc,
                               void *cbdata)
{
    char application_namespace[PMIX_MAX_NSLEN + 1];
    int i;

    MPIR_SHIM_DEBUG_ENTER("Event '%s', nspace '%s', rank '%ld'",
                          PMIx_Error_string(status),
                          source ? source->nspace : "null",
                          source ? source->rank : -1L);

    /*
     * Search for the namespace of the application.
     */
    application_namespace[0] = '\0';
    for (i = 0; i < (int)ninfo; i++) {
        if (PMIX_CHECK_KEY(&info[i], PMIX_NSPACE)) {
            debug_print("PMIX_NSPACE key found: namespace '%s'\n",
                        info[i].value.data.string);
            // Always take the last one found
            strcpy(application_namespace, info[i].value.data.string);
        }
    }

    /*
     * If the namespace of the launched job wasn't returned, then that
     * is an error.
     */
    if ('\0' == application_namespace[0]) {
        fprintf(stderr, "No application namespace found in notification.\n");
        pmix_fatal_error(PMIX_ERROR, "Launched application namespace wasn't returned in callback");
    }

    PMIX_PROC_LOAD(&application_proc, application_namespace, PMIX_RANK_WILDCARD);
    debug_print("Application namespace is '%s'\n", application_proc.nspace);
    post_condition(&launch_complete_cond);

    /*
     * Tell the event handler state machine that we are the last step
     */
    if (NULL != cbfunc) {
        cbfunc(PMIX_EVENT_ACTION_COMPLETE, NULL, 0, NULL, NULL, cbdata);
    }

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   launcher_ready_handler
 * @brief  Callback to handle notificaton that launcher is ready to accept
 *         directives from the tool process.
 * @param  handler_id: Callback id for this callback
 * @param: status: The event this handler was invoked for
 * @param  source: The source for the notification
 * @param  info: Array of pmix_info_t objects passed to this callback by sender
 * @param  ninfo: Number of elements in ninfo array
 * @param  results: Array of pmix_info_t objects from previous handlers in chain
 * @param  nresults: Number of elements in results array
 * @param  cbfunc: Function to be called to propagate notification
 * @param  cbdata: Data passed to this callback
 *
 * This is an event notification function that we explicitly request
 * be called when the PMIX_LAUNCHER_READY notification is issued.
 */
void launcher_ready_handler(size_t handler_id, pmix_status_t status,
                            const pmix_proc_t *source,
                            pmix_info_t info[], size_t ninfo,
                            pmix_info_t results[], size_t nresults,
                            pmix_event_notification_cbfunc_fn_t cbfunc,
                            void *cbdata)
{
    MPIR_SHIM_DEBUG_ENTER("Event '%s', nspace '%s', rank '%ld'",
                          PMIx_Error_string(status),
                          source ? source->nspace : "null",
                          source ? source->rank : -1L);

    callback_reg_status = status;
    post_condition(&launch_ready_cond);

    /*
     * Tell the event handler state machine that we are the last step.
     */
    if (NULL != cbfunc) {
        cbfunc(PMIX_EVENT_ACTION_COMPLETE, NULL, 0, NULL, NULL, cbdata);
    }

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   application_terminate_handler
 * @brief  Callback to handle notificaton that application has exited.
 * @param  handler_id: Callback id for this callback
 * @param: status: The event this handler was invoked for
 * @param  source: The source for the notification
 * @param  info: Array of pmix_info_t objects passed to this callback by sender
 * @param  ninfo: Number of elements in ninfo array
 * @param  results: Array of pmix_info_t objects from previous handlers in chain
 * @param  nresults: Number of elements in results array
 * @param  cbfunc: Function to be called to propagate notification
 * @param  cbdata: Data passed to this callback
 *
 * This is an event notification function that we explicitly request
 * be called when the PMIX_ERR_JOB_TERMINATED notification is issued.
 */
void application_terminate_handler(size_t handler_id, pmix_status_t status,
                                   const pmix_proc_t *source,
                                   pmix_info_t info[], size_t ninfo,
                                   pmix_info_t results[], size_t nresults,
                                   pmix_event_notification_cbfunc_fn_t cbfunc,
                                   void *cbdata)
{
    size_t n;
    pmix_proc_t *affected_proc = NULL;

    MPIR_SHIM_DEBUG_ENTER("Event '%s', nspace '%s', rank '%ld'",
                          PMIx_Error_string(status),
                          source ? source->nspace : "null",
                          source ? source->rank : -1L);

    /*
     * Extract the error code
     */
    for( n = 0; n < ninfo; ++n ) {
        if( PMIX_CHECK_KEY(&info[n], PMIX_EXIT_CODE) ) {
            app_exit_code = info[n].value.data.integer;
            if( app_exit_code != 0  ) {
                MPIR_debug_state = MPIR_DEBUG_ABORTING;
                if( NULL == MPIR_debug_abort_string ) {
                    asprintf(&MPIR_debug_abort_string,
                             "The application exited with return code %d", app_exit_code);
                }
            }
        }
        else if( PMIX_CHECK_KEY(&info[n], PMIX_JOB_TERM_STATUS) ) {
            app_exit_code = info[n].value.data.status;
            if( app_exit_code != 0  ) {
                MPIR_debug_state = MPIR_DEBUG_ABORTING;
                if( NULL == MPIR_debug_abort_string ) {
                    asprintf(&MPIR_debug_abort_string,
                             "The application exited with return code %d", app_exit_code);
                }
            }
        }
        else if( PMIX_CHECK_KEY(&info[n], PMIX_EVENT_AFFECTED_PROC) ) {
            affected_proc = info[n].value.data.proc;
            debug_print("Notified job terminated, affected '%s'.%d\n",
                        (NULL == affected_proc ? "NULL" : affected_proc->nspace),
                        (NULL == affected_proc ? -1 : (int)affected_proc->rank));
        }
    }

    debug_print("Notified job terminated, affected '%s', exit status %d\n",
                (NULL == affected_proc ? "NULL" : affected_proc->nspace),
                app_exit_code);

    // Mark launcher terminated so any subsequent condition waits are assumed
    // satisfied and so this module will not hang on those conditions.
    app_terminated = 1;
    launcher_terminated = 2;
    post_condition(&launch_term_cond);

    // Main thread could be waiting for any of these conditions to post ready.
    // Post them here so main thread is not hung after launcher terminates.
    release_conditions();

    /*
     * Tell the event handler state machine that we are the last step.
     */
    if (NULL != cbfunc) {
        cbfunc(PMIX_EVENT_ACTION_COMPLETE, NULL, 0, NULL, NULL, cbdata);
    }

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   launcher_terminate_handler
 * @brief  Callback to handle notificaton that launcher has exited.
 * @param  handler_id: Callback id for this callback
 * @param: status: The event this handler was invoked for
 * @param  source: The source for the notification
 * @param  info: Array of pmix_info_t objects passed to this callback by sender
 * @param  ninfo: Number of elements in ninfo array
 * @param  results: Array of pmix_info_t objects from previous handlers in chain
 * @param  nresults: Number of elements in results array
 * @param  cbfunc: Function to be called to propagate notification
 * @param  cbdata: Data passed to this callback
 *
 * This is an event notification function that we explicitly request
 * be called when the PMIX_ERR_JOB_TERMINATED notification is issued.
 */
void launcher_terminate_handler(size_t handler_id, pmix_status_t status,
                                const pmix_proc_t *source,
                                pmix_info_t info[], size_t ninfo,
                                pmix_info_t results[], size_t nresults,
                                pmix_event_notification_cbfunc_fn_t cbfunc,
                                void *cbdata)
{
    size_t n;
    pmix_proc_t *affected_proc = NULL;

    MPIR_SHIM_DEBUG_ENTER("Event '%s', nspace '%s', rank '%ld'",
                          PMIx_Error_string(status),
                          source ? source->nspace : "null",
                          source ? source->rank : -1L);

    /*
     * Extract the error code
     */
    for( n = 0; n < ninfo; ++n ) {
        if( PMIX_CHECK_KEY(&info[n], PMIX_EXIT_CODE) ) {
            launcher_exit_code = info[n].value.data.integer;
            if( launcher_exit_code != 0 ) {
                MPIR_debug_state = MPIR_DEBUG_ABORTING;
                if( NULL == MPIR_debug_abort_string ) {
                    asprintf(&MPIR_debug_abort_string, "The launcher exited with return code %d", launcher_exit_code);
                }
            }
        }
        else if( PMIX_CHECK_KEY(&info[n], PMIX_JOB_TERM_STATUS) ) {
            launcher_exit_code = info[n].value.data.status;
            if( launcher_exit_code != 0 ) {
                MPIR_debug_state = MPIR_DEBUG_ABORTING;
                if( NULL == MPIR_debug_abort_string ) {
                    asprintf(&MPIR_debug_abort_string, "The launcher exited with return code %d", launcher_exit_code);
                }
            }
        }
        else if( PMIX_CHECK_KEY(&info[n], PMIX_EVENT_AFFECTED_PROC) ) {
            affected_proc = info[n].value.data.proc;
        }
    }

    debug_print("Notified job terminated, affected '%s', exit status %d\n",
                (NULL == affected_proc ? "NULL" : affected_proc->nspace),
                launcher_exit_code);

    // Mark launcher terminated so any subsequent condition waits are assumed
    // satisfied and so this module will not hang on those conditions.
    launcher_terminated = 1;
    post_condition(&launch_term_cond);

    // Main thread could be waiting for any of these conditions to post ready.
    // Post them here so main thread is not hung after launcher terminates.
    release_conditions();

    /*
     * Tell the event handler state machine that we are the last step.
     */
    if (NULL != cbfunc) {
        cbfunc(PMIX_EVENT_ACTION_COMPLETE, NULL, 0, NULL, NULL, cbdata);
    }

    MPIR_SHIM_DEBUG_EXIT("");
}

/**
 * @name   register_default_event_handler
 * @brief  Register default event notification callback, which handles
 *         notifications sent to this module but not handled elsewhere.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int register_default_event_handler(void)
{
    MPIR_SHIM_DEBUG_ENTER("");

    PMIx_Register_event_handler(NULL, 0,
                                NULL, 0,
                                default_event_handler,
                                registration_complete_handler, 
                                "default-callback");
    wait_for_condition(&registration_cond);
    if (PMIX_SUCCESS != callback_reg_status) {
        fprintf(stderr,
               "An error occurred registering default callback %s.\n",
               PMIx_Error_string(callback_reg_status));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    default_cb_id = callback_reg_id;

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   register_launcher_complete_handler
 * @brief  Register callback to handle launch complete notifications
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int register_launcher_complete_handler(void)
{
    pmix_status_t event;
    pmix_info_t *infos = NULL;
    size_t num_infos, n;

    MPIR_SHIM_DEBUG_ENTER("");

    event = PMIX_LAUNCH_COMPLETE;

    num_infos = 2;
    PMIX_INFO_CREATE(infos, num_infos);
    n = 0;
    /* Object to be returned whenever the registered cbfunc is invoked */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_RETURN_OBJECT, (void *)&registration_cond, PMIX_POINTER);
    n++;
    /* String name identifying this handler */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_HDLR_NAME, "LAUNCHER-COMPLETE", PMIX_STRING);
    n++;

    PMIx_Register_event_handler(&event, 1,
                                infos, num_infos,
                                launcher_complete_handler,
                                registration_complete_handler, 
                                "launcher-complete-callback");
    wait_for_condition(&registration_cond);
    PMIX_INFO_FREE(infos, num_infos);
    if (PMIX_SUCCESS != callback_reg_status) {
        fprintf(stderr,
               "An error occurred registering launch complete callback %s.\n",
               PMIx_Error_string(callback_reg_status));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    launch_complete_cb_id = callback_reg_id;

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   register_launcher_ready_handler
 * @brief  Register callback to handle launcher ready notifications.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int register_launcher_ready_handler(void)
{
    pmix_status_t event;
    pmix_info_t *infos = NULL;
    size_t num_infos, n;

    MPIR_SHIM_DEBUG_ENTER("");

    event = PMIX_LAUNCHER_READY;

    num_infos = 2;
    PMIX_INFO_CREATE(infos, num_infos);
    n = 0;
    /* Object to be returned whenever the registered cbfunc is invoked */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_RETURN_OBJECT, (void *)&registration_cond, PMIX_POINTER);
    n++;
    /* String name identifying this handler */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_HDLR_NAME, "LAUNCHER-READY", PMIX_STRING);
    n++;

    PMIx_Register_event_handler(&event, 1,
                                infos, num_infos,
                                launcher_ready_handler,
                                registration_complete_handler, 
                                "launcher-ready-callback");
    wait_for_condition(&registration_cond);
    PMIX_INFO_FREE(infos, num_infos);
    if (PMIX_SUCCESS != callback_reg_status) {
        fprintf(stderr,
               "An error occurred registering launcher ready callback %s.\n",
               PMIx_Error_string(callback_reg_status));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    launch_ready_cb_id = callback_reg_id;

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   register_launcher_terminate_handler
 * @brief  Register callback to handle launcher terminated notifications.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int register_launcher_terminate_handler(void)
{
    pmix_status_t event;
    pmix_info_t *infos = NULL;
    size_t num_infos, n;

    MPIR_SHIM_DEBUG_ENTER("");

    event = PMIX_ERR_JOB_TERMINATED;

    num_infos = 2;
    PMIX_INFO_CREATE(infos, num_infos);
    n = 0;
    /* Object to be returned whenever the registered cbfunc is invoked */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_RETURN_OBJECT, (void *)&registration_cond, PMIX_POINTER);
    n++;
    /* String name identifying this handler */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_AFFECTED_PROC, &launcher_proc, PMIX_PROC);
    n++;

    PMIx_Register_event_handler(&event, 1,
                                infos, num_infos,
                                launcher_terminate_handler,
                                registration_complete_handler, 
                                "launcher-terminate-callback");
    wait_for_condition(&registration_cond);
    PMIX_INFO_FREE(infos, num_infos);
    if (PMIX_SUCCESS != callback_reg_status) {
        fprintf(stderr,
             "An error occurred registering launcher terminated callback %s.\n",
             PMIx_Error_string(callback_reg_status));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    launcher_terminate_cb_id = callback_reg_id;

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   register_application_terminate_handler
 * @brief  Register callback to handle application terminated notifications.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int register_application_terminate_handler(void)
{
    pmix_status_t event;
    pmix_info_t *infos = NULL;
    size_t num_infos, n;

    MPIR_SHIM_DEBUG_ENTER("");

    event = PMIX_ERR_JOB_TERMINATED;

    num_infos = 2;
    PMIX_INFO_CREATE(infos, num_infos);
    n = 0;
    /* Object to be returned whenever the registered cbfunc is invoked */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_RETURN_OBJECT, (void *)&registration_cond, PMIX_POINTER);
    n++;
    /* String name identifying this handler */
    PMIX_INFO_LOAD(&infos[n], PMIX_EVENT_AFFECTED_PROC, &application_proc, PMIX_PROC);
    n++;

    PMIx_Register_event_handler(&event, 1,
                                infos, num_infos,
                                application_terminate_handler,
                                registration_complete_handler, 
                                "application-terminate-callback");
    wait_for_condition(&registration_cond);
    PMIX_INFO_FREE(infos, num_infos);
    if (PMIX_SUCCESS != callback_reg_status) {
        fprintf(stderr,
             "An error occurred registering application terminated callback %s.\n",
             PMIx_Error_string(callback_reg_status));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    app_terminate_cb_id = callback_reg_id;

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   spawn_launcher_and_application
 * @brief  Set up command line and environment variable then spawn the launcher
 *         which will in turn spawn the application tasks.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int spawn_launcher_and_application(void)
{
    char launcher_namespace[PMIX_MAX_NSLEN + 1];
    pmix_info_t *attrs = NULL;
    int i, n;
    pmix_status_t rc;
    size_t num_attrs;
    pmix_app_t app_context;
    char cwd[_POSIX_PATH_MAX + 1];
    char * tmp = NULL;
    pmix_envar_t envar;

    MPIR_SHIM_DEBUG_ENTER("");

    PMIX_LOAD_NSPACE(launcher_namespace, NULL);
    PMIX_LOAD_NSPACE(launcher_proc.nspace, NULL);
    launcher_proc.rank = PMIX_RANK_WILDCARD;

    /*
     * Setup the launcher's application parameters.
     */
    PMIX_APP_CONSTRUCT(&app_context);

    /* Setup the executable */
    app_context.cmd = strdup(run_args[0]);

    /* The argv to pass to the application */
    for (i = 0; i < num_run_args; i++) {
        PMIX_ARGV_APPEND(rc, app_context.argv, run_args[i]);
        if( PMIX_SUCCESS != rc ) {
            fprintf(stderr, "PMIX_ARGV_APPEND() failed %d\n", rc);
            MPIR_SHIM_DEBUG_EXIT("");
            return STATUS_FAIL;
        }
    }

    /* Try to use the same working directory */
    if (NULL == getcwd(cwd, sizeof(cwd) - 1)) {
        app_context.cwd = strdup("");
    }
    else {
        app_context.cwd = strdup(cwd);
    }

    /* Just one launcher process please */
    app_context.maxprocs = 1;

    /*
     * Copy the environment, if it's a proxy run
     */
    if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
        // What's the right thing to do here? If we copy the entire current 
        // environment pool, then mpirun and the application should have the
        // variables they need. However, we could just allocate an environment
        // variable pool containing just PATH and LD_LIBRARY_PATH, maybe a few
        // others, and then add the PMIX_* environment variables we need. But that
        // probably means the environment variables the application needs might
        // need to be added with mpirun -x flags, which could be a very long list.
        for( i = 0; NULL != environ[i] ; ++i) {
            PMIX_ARGV_APPEND(rc, app_context.env, environ[i]);
            if (PMIX_SUCCESS != rc) {
                fprintf(stderr, "PMIX_ARGV_APPEND(env) failed %d\n", rc);
                MPIR_SHIM_DEBUG_EXIT("");
                return STATUS_FAIL;
            }
        }
    }

    if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
        /* Have it use this session directory */
        PMIX_SETENV(rc, "PMIX_SERVER_TMPDIR", session_dirname, &app_context.env);

        /* Have it output a specific rendezvous file */
        // Rendezvous file is not needed in non-proxy mode since PMIx server
        // specifies the connection URI in this case.
        PMIX_SETENV(rc, "PMIX_LAUNCHER_RENDEZVOUS_FILE", rendezvous_filename, &app_context.env);
    }

    app_context.info = NULL;
    app_context.ninfo = 0;

    if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
        num_attrs = 5;
    }
    else {
        num_attrs = 6;
    }

    // Spawn the launcher, for example, mpirun
    PMIX_INFO_CREATE(attrs, num_attrs);
    n = 0;
    /* Map by slot */
    PMIX_INFO_LOAD(&attrs[n], PMIX_MAPBY, "slot", PMIX_STRING);
    n++;
    // Forward stdout to me
    PMIX_INFO_LOAD(&attrs[n], PMIX_FWD_STDOUT, &const_true, PMIX_BOOL);
    n++;
    // Forward stderr to me
    PMIX_INFO_LOAD(&attrs[n], PMIX_FWD_STDERR, &const_true, PMIX_BOOL);
    n++;
    // Notify us when the job completes
    PMIX_INFO_LOAD(&attrs[n], PMIX_NOTIFY_COMPLETION, &const_true, PMIX_BOOL);
    n++;
    // We are spawning a tool
    PMIX_INFO_LOAD(&attrs[n], PMIX_SPAWN_TOOL, &const_true, PMIX_BOOL);
    n++;

    /* Tell the launcher to wait for directives */
    asprintf(&tmp, "%s:%d", tool_proc.nspace, tool_proc.rank);
    if (MPIR_SHIM_NONPROXY_MODE == mpir_mode) {
        // launcher is to wait for directives
        PMIX_ENVAR_LOAD(&envar, "PMIX_LAUNCHER_PAUSE_FOR_TOOL", tmp, ':');
        PMIX_INFO_LOAD(&attrs[n], PMIX_SET_ENVAR, &envar, PMIX_ENVAR);
        PMIX_ENVAR_DESTRUCT(&envar);
        n++;
    }
    if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
        PMIX_SETENV(rc, "PMIX_LAUNCHER_PAUSE_FOR_TOOL", tmp, &app_context.env);
    }
    free(tmp);

    /*
     * Spawn the job - the function will return when the launcher has
     * been launched.  Note that this doesn't tell us anything about
     * the launcher's state - it just means that the launcher has been
     * fork/exec'd.
     */
    debug_print("Calling PMIx_Spawn for %s\n", app_context.cmd);
    rc = PMIx_Spawn(attrs, num_attrs, &app_context, 1, launcher_namespace);
    PMIX_APP_DESTRUCT(&app_context);
    debug_print("PMIx_Spawn status %s launcher_namespace: %s\n",
                PMIx_Error_string(rc), launcher_namespace);
    PMIX_INFO_FREE(attrs, num_attrs);

    if ((PMIX_SUCCESS != rc) && (PMIX_OPERATION_SUCCEEDED != rc)) {
        fprintf(stderr,
                "An error occurred launching the application: %s.\n",
                PMIx_Error_string(rc));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    // Proxy case fills this in during connect_to_server()
    if (MPIR_SHIM_NONPROXY_MODE == mpir_mode) {
        PMIX_PROC_LOAD(&launcher_proc, launcher_namespace, 0);
    }

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   connect_to_server
 * @brief  Connect to the PMIx server for this session
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int connect_to_server(void)
{
    pmix_info_t *attrs;
    size_t num_attrs, n;
    pmix_status_t rc;
    int retry_delay = 1;
    int max_retries = 10;

    MPIR_SHIM_DEBUG_ENTER("");

    /*
     * Attributes for connecting to the server.
     */
    num_attrs = 3;
    PMIX_INFO_CREATE(attrs, num_attrs);
    n = 0;
    /* Number of times to try to connect */
    PMIX_INFO_LOAD(&attrs[n], PMIX_CONNECT_MAX_RETRIES, &max_retries,
                   PMIX_INT32);
    n++;
    /* Number of seconds to wait between connect attempts */
    PMIX_INFO_LOAD(&attrs[n], PMIX_CONNECT_RETRY_DELAY, &retry_delay,
                   PMIX_INT32);
    n++;

    if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
        debug_print("In %s, rendezvous_file=%s\n", __FUNCTION__,
                    rendezvous_filename);
        /* Rendezvous file passed in PMIX_LAUNCHER_RENDEZVOUS_FILE */
        PMIX_INFO_LOAD(&attrs[n], PMIX_TOOL_ATTACHMENT_FILE,
                       rendezvous_filename, PMIX_STRING);
        n++;
    }
    else {
        // Rendezvous file is not needed in non-proxy mode since PMIx server
        // specifies the connection URI in this case.
        PMIX_INFO_LOAD(&attrs[n], PMIX_CONNECT_SYSTEM_FIRST, &const_true,
                       PMIX_BOOL);
        n++;
    }

    rc = PMIx_tool_connect_to_server(&tool_proc, attrs, num_attrs);
    PMIX_INFO_FREE(attrs, num_attrs);
    if (PMIX_SUCCESS != rc) {
        fprintf(stderr, "An error occurred connecting to PMIx server: %s.\n",
                PMIx_Error_string(rc));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    // Access Launcher information
    query_launcher_namespace();

    session_count = session_count + 1;

    MPIR_SHIM_DEBUG_EXIT("Connected to launcher nspace '%s' rank %d",
                         launcher_proc.nspace, launcher_proc.rank);
    return STATUS_OK;
}

/**
 * @name   release_procs_in_namespace
 * @brief  Notify processes in the specified namespace that they are to resume
 *         execution.
 * @param  namespace: Namespace containing the processes to be notified.
 * @param  rank: Process rank(s) to notify
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int release_procs_in_namespace(char *namespace, pmix_rank_t rank)
{
    pmix_info_t *attrs;
    size_t num_attrs, n;
    pmix_status_t rc;
    pmix_proc_t target_procs;

    MPIR_SHIM_DEBUG_ENTER("Namespace '%s', rank %d", namespace, rank);

    PMIX_PROC_LOAD(&target_procs, namespace, rank);
    num_attrs = 2;
    PMIX_INFO_CREATE(attrs, 2);
    n = 0;
    /* Deliver to the target processes */
    PMIX_INFO_LOAD(&attrs[n], PMIX_EVENT_CUSTOM_RANGE, &target_procs, PMIX_PROC);
    n++;
    /* Only non-default handlers */
    PMIX_INFO_LOAD(&attrs[n], PMIX_EVENT_NON_DEFAULT, &const_true, PMIX_BOOL);
    n++;

    // Is PMIX_ERR_DEBUGGER_RELEASE correct? This looks like an error status
    // not a notification code. But looking at PMIX debugger.c example it
    // seems this is right.
    rc = PMIx_Notify_event(PMIX_ERR_DEBUGGER_RELEASE,
                           NULL, PMIX_RANGE_CUSTOM,
                           attrs, num_attrs,
                           NULL, NULL);
    PMIX_INFO_FREE(attrs, num_attrs);
    if ((PMIX_SUCCESS != rc) && (PMIX_OPERATION_SUCCEEDED != rc)) {
        fprintf(stderr, "An error occurred resuming launcher process: %s.\n",
                PMIx_Error_string(rc));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   send_launch_directives
 * @brief  Send directives to the launcher asking that the application pause in
 *         init and that we are notified when launch completes. If running in
 *         attach mode then only the launch notification request is sent.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int send_launch_directives(void)
{
    pmix_info_t *directives = NULL;
    pmix_info_t *attrs = NULL;
    size_t num_attrs, n;
    pmix_data_array_t directive_array;
    pmix_status_t rc;

    MPIR_SHIM_DEBUG_ENTER("Namespace '%s', rank %d", launcher_proc.nspace, launcher_proc.rank);

    /* provide a few job-level directives */
    directive_array.type = PMIX_INFO;
    n = 0;
    if (MPIR_SHIM_ATTACH_MODE == mpir_mode) {
        directive_array.size = 1;
        PMIX_INFO_CREATE(directive_array.array, directive_array.size);
        if( NULL == directive_array.array ) {
            pmix_fatal_error(PMIX_ERROR, "Failed to allocate the send_launch directive array");
        }
        directives = (pmix_info_t*)directive_array.array;

        /* Notify us that the job was launched (or when it does finally launch) */
        PMIX_INFO_LOAD(&directives[n], PMIX_NOTIFY_LAUNCH, &const_true,
                       PMIX_BOOL);
        n++;
    }
    else {
        directive_array.size = 2;
        PMIX_INFO_CREATE(directive_array.array, directive_array.size);
        if( NULL == directive_array.array ) {
            pmix_fatal_error(PMIX_ERROR, "Failed to allocate the send_launch directive array");
        }
        directives = (pmix_info_t*)directive_array.array;

        /* Stop the processes in PMIx_Init() */
        PMIX_INFO_LOAD(&directives[n], PMIX_DEBUG_STOP_IN_INIT, &const_true,
                       PMIX_BOOL);
        n++;
        /* Notify us when the job is launched */
        PMIX_INFO_LOAD(&directives[n], PMIX_NOTIFY_LAUNCH, &const_true,
                       PMIX_BOOL);
        n++;
    }

    num_attrs = 3;
    PMIX_INFO_CREATE(attrs, num_attrs);
    n = 0;
    /* The launcher's namespace, rank 0 */
    /* Deliver to the target launcher */
    PMIX_INFO_LOAD(&attrs[n], PMIX_EVENT_CUSTOM_RANGE, &launcher_proc, PMIX_PROC);
    n++;
    /* Only non-default handlers */
    PMIX_INFO_LOAD(&attrs[n], PMIX_EVENT_NON_DEFAULT, &const_true, PMIX_BOOL);
    n++;
    /* Load the data array */
    PMIX_INFO_LOAD(&attrs[n], PMIX_DEBUG_JOB_DIRECTIVES, &directive_array, 
                   PMIX_DATA_ARRAY);
    n++;

    rc = PMIx_Notify_event(PMIX_LAUNCH_DIRECTIVE,
                           NULL, PMIX_RANGE_CUSTOM,
                           attrs, num_attrs,
                           NULL, NULL);
    PMIX_INFO_FREE(directives, directive_array.size);
    PMIX_INFO_FREE(attrs, num_attrs);
    if ((PMIX_SUCCESS != rc) && (PMIX_OPERATION_SUCCEEDED != rc)) {
        fprintf(stderr, "An error occurred sending launch directives: %s.\n",
                PMIx_Error_string(rc));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   query_launcher_namespace
 * @brief  Access the server namespace/rank and save it in launcher_proc
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int query_launcher_namespace(void)
{
    char launcher_namespace[PMIX_MAX_NSLEN + 1];
    pmix_value_t *val = NULL;

    MPIR_SHIM_DEBUG_ENTER("");

    // Access Launcher information
    // https://github.com/openpmix/openpmix/issues/1801#issuecomment-648365247
    PMIx_Get(&tool_proc, PMIX_SERVER_NSPACE, NULL, 0, &val);
    if( NULL != val && val->type == PMIX_STRING ) {
        PMIX_LOAD_NSPACE(launcher_namespace, val->data.string);

        PMIx_Get(&tool_proc, PMIX_SERVER_RANK, NULL, 0, &val);
        if( NULL != val && val->type == PMIX_PROC_RANK ) {
            PMIX_PROC_LOAD(&launcher_proc, launcher_namespace, val->data.rank);
        }
    }

    MPIR_SHIM_DEBUG_EXIT("Connected to launcher nspace '%s' rank %d",
                         launcher_proc.nspace, launcher_proc.rank);
    return STATUS_OK;
}

/**
 * @name   query_application_namespace
 * @brief  Set application_proc to the name of the application namespace
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int query_application_namespace(void)
{
    pmix_info_t *namespace_query_data = NULL;
    size_t namespace_query_size;
    pmix_status_t rc;
    pmix_query_t namespace_query;
    int n;

    MPIR_SHIM_DEBUG_ENTER("");

    PMIX_QUERY_CONSTRUCT(&namespace_query);
    PMIX_ARGV_APPEND(rc, namespace_query.keys, PMIX_QUERY_NAMESPACES);
    if (PMIX_SUCCESS != rc) {
        fprintf(stderr, "An error occurred creating namespace query.");
        PMIX_QUERY_DESTRUCT(&namespace_query);
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    namespace_query.nqual = 2;
    PMIX_INFO_CREATE(namespace_query.qualifiers, namespace_query.nqual);
    n = 0;
    /* Ask the launcher for the namespaces it knows of */
    PMIX_INFO_LOAD(&namespace_query.qualifiers[n], PMIX_NSPACE,
                   launcher_proc.nspace, PMIX_STRING);
    n++;
    PMIX_INFO_LOAD(&namespace_query.qualifiers[n], PMIX_RANK,
                   &launcher_proc.rank,  PMIX_INT32);
    n++;

    rc = PMIx_Query_info(&namespace_query, 1, &namespace_query_data,
                         &namespace_query_size);
    PMIX_QUERY_DESTRUCT(&namespace_query);
    if (PMIX_SUCCESS != rc) {
        fprintf(stderr,
                "An error occurred querying application namespace: %s.\n",
                PMIx_Error_string(rc));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    if ((1 != namespace_query_size) ||
              (PMIX_STRING != namespace_query_data->value.type)) {
        fprintf(stderr, "The response to namespace query has wrong format.\n");
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    PMIX_PROC_LOAD(&application_proc, namespace_query_data->value.data.string, PMIX_RANK_WILDCARD);
    debug_print("Application namespace is '%s'\n", application_proc.nspace);

    if (NULL != namespace_query_data) {
        free(namespace_query_data);
    }

    MPIR_SHIM_DEBUG_EXIT("");
    return STATUS_OK;
}

/**
 * @name   pmix_proc_table_to_mpir
 * @brief  Request the process mapping data from PMIX, build the MPIR_proctable
 *         array, and call MPIR_Breakpoint to notify the tool that the process
 *         map info is available.
 * @return STATUS_OK if successful, otherwise STATUS_FAIL
 */
int pmix_proc_table_to_mpir(void)
{
    pmix_info_t *proctable_query_data = NULL;
    size_t proctable_query_size;
    pmix_data_array_t *response_array;
    pmix_proc_info_t *proc_info;
    pmix_status_t rc;
    int i, n, rank;
    pmix_query_t proctable_query;

    MPIR_SHIM_DEBUG_ENTER("");

    /*
     * Query PMIx for the process table for the application namespace.
     */
    PMIX_QUERY_CONSTRUCT(&proctable_query);
    PMIX_ARGV_APPEND(rc, proctable_query.keys, PMIX_QUERY_PROC_TABLE);
    if (PMIX_SUCCESS != rc) {
        fprintf(stderr, "An error occurred creating proctable query.");
        PMIX_QUERY_DESTRUCT(&proctable_query);
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    proctable_query.nqual = 1;
    PMIX_INFO_CREATE(proctable_query.qualifiers, proctable_query.nqual);
    n = 0;
    PMIX_INFO_LOAD(&proctable_query.qualifiers[n], PMIX_NSPACE,
                   application_proc.nspace, PMIX_STRING);
    n++;

    rc = PMIx_Query_info(&proctable_query, 1, &proctable_query_data,
                         &proctable_query_size);
    PMIX_QUERY_DESTRUCT(&proctable_query);
    if (PMIX_SUCCESS != rc) {
        fprintf(stderr, "An error occurred querying the proctable: %s.\n",
                PMIx_Error_string(rc));
        MPIR_SHIM_DEBUG_EXIT("");
        return STATUS_FAIL;
    }

    /*
     * Check the query data status, info/ninfo, and data type (which
     * should be a data array).
     */
    if (NULL == proctable_query_data || 0 >= proctable_query_size) {
        pmix_fatal_error(rc, "PMIx proc table info/ninfo is 0");
    }
    if (PMIX_DATA_ARRAY != proctable_query_data[0].value.type) {
        pmix_fatal_error(rc, "PMIx proc table has incorrect data type: %s (%d)",
                         PMIx_Data_type_string (proctable_query_data[0].value.type),
                         (int) proctable_query_data[0].value.type);
    }
    if (NULL == proctable_query_data[0].value.data.darray->array) {
        pmix_fatal_error(rc, "PMIx proc table data array is null");
    }
    if (PMIX_PROC_INFO != proctable_query_data[0].value.data.darray->type) {
        pmix_fatal_error(rc, "PMIx proc table data array has incorrect type: %s (%d)",
                         PMIx_Data_type_string (proctable_query_data[0].value.data.darray->type),
                         (int) proctable_query_data[0].value.data.darray->type);
    }

    /*
     * The data array consists of a struct:
     *     size_t size;
     *     void* array;
     *
     * In this case, the array is composed of pmix_proc_info_t structs:
     *     pmix_proc_t proc;   // contains the nspace,rank of this proc
     *     char* hostname;
     *     char* executable_name;
     *     pid_t pid;
     *     int exit_code;
     *     pmix_proc_state_t state;
     */
    debug_print("Proctable query returns %d elements of type %s\n",
               proctable_query_size,
               PMIx_Data_type_string(proctable_query_data->value.type));
    response_array =
        (pmix_data_array_t *) proctable_query_data->value.data.darray;
    proc_info = response_array->array;

    debug_print("Received PMIx proc table for %lu procs:\n", response_array->size);

    MPIR_proctable_size = response_array->size;
    MPIR_proctable = malloc(MPIR_proctable_size * sizeof(MPIR_PROCDESC));
    for (i = 0; i < MPIR_proctable_size; i++) {
        rank = proc_info[i].proc.rank;

        MPIR_proctable[rank].pid = proc_info[i].pid;
        MPIR_proctable[rank].host_name = strdup(proc_info[i].hostname);
        MPIR_proctable[rank].executable_name = strdup(proc_info[i].executable_name);

        debug_print("Task %d host=%s exec=%s pid=%d state='%s'\n", i, 
                    proc_info[i].hostname, proc_info[i].executable_name,
                    proc_info[i].pid,
                    PMIx_Proc_state_string(proc_info[i].state));
    }
    MPIR_debug_state = MPIR_DEBUG_SPAWNED;

    if (NULL != proctable_query_data) {
        PMIX_INFO_FREE(proctable_query_data, proctable_query_size);
    }

    /*
     * Notify the debugger.
     */
    MPIR_Breakpoint();

    MPIR_SHIM_DEBUG_EXIT("");
    return PMIX_SUCCESS;
}

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
                     int argc, char *argv[], const char *pmix_prefix_)
{
    MPIR_SHIM_DEBUG_ENTER("");

    tool_binary_name = strdup("mpir");

    PMIX_LOAD_NSPACE(launcher_proc.nspace, NULL);
    launcher_proc.rank = PMIX_RANK_WILDCARD;

    PMIX_LOAD_NSPACE(application_proc.nspace, NULL);
    application_proc.rank = PMIX_RANK_WILDCARD;


    /*
     * Process options provided
     */
    if (STATUS_FAIL == process_options(mpir_mode_, pid_, debug_, argc, argv)) {
        return STATUS_FAIL;
    }
    if (NULL != pmix_prefix_) {
        pmix_prefix = pmix_prefix_;
    }
    debug_print("Launcher '%s', performing a %s\n", tool_binary_name,
                (MPIR_SHIM_PROXY_MODE == mpir_mode ? "proxy run" : 
                 (MPIR_SHIM_NONPROXY_MODE == mpir_mode ? "non-proxy run" :
                  (MPIR_SHIM_ATTACH_MODE == mpir_mode ? "attach run" : "(unknown"))));

    /*
     * Setup signal handlers.
     */
    if (STATUS_FAIL == setup_signal_handlers()) {
        return STATUS_FAIL;
    }

    /*
     * Setup an atexit handler to make sure we cleanup
     */
    if (0 != atexit(exit_handler)) {
        fprintf(stderr, "An error occurred setting an exit handler.\n");
        return STATUS_FAIL;
    }

    /*
     * Initialize ourselves as a PMIx tool.
     */
    if (STATUS_FAIL == initialize_as_tool()) {
        return STATUS_FAIL;
    }

    /*
     * If we are not connecting to another pid, then setup the session directory
     */
    if (MPIR_SHIM_ATTACH_MODE != mpir_mode) {
        if (STATUS_FAIL == setup_session_directory()) {
            return STATUS_FAIL;
        }
    }

    /*
     * Register the default event handler.
     */
    if( STATUS_OK != register_default_event_handler() ) {
        return STATUS_FAIL;
    }

    /*
     * If we are using the rendezvous mechanism for connecting to the PMIx server
     */
    if (MPIR_SHIM_ATTACH_MODE != mpir_mode) {
        /*
         * Spawn the launcher process with the application arguments
         */
        if (STATUS_FAIL == spawn_launcher_and_application()) {
            return STATUS_FAIL;
        }

        /*
         * Connect to the server.
         */
        if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
            if (STATUS_FAIL == connect_to_server()) {
                return STATUS_FAIL;
            }
        }

        // There's apparently a restriction, noted in the mpir-shim git log
        // entry dated 3/29/20 that states the launch complete and launch
        // terminate callbacks can't be registered until after this code
        // connects to the server.
        /*
         * Register for the "launcher is ready to communicate" event.
         */
        if (STATUS_FAIL == register_launcher_ready_handler() ) {
            return STATUS_FAIL;
        }

        /*
         * Register for the "launcher has completed launching" event.
         */
        if (STATUS_FAIL == register_launcher_complete_handler() ) {
            return STATUS_FAIL;
        }

        /*
         * Wait here for the launcher to declare itself ready.
         */
        debug_print("Waiting for launcher to become ready\n");
        wait_for_condition(&launch_ready_cond);
        debug_print("Launcher is ready\n");

        /*
         * Register for the "launcher has terminated" event.
         * In a 'proxy' (prun) scenario this will tell us when everything is done
         */
        if (STATUS_FAIL == register_launcher_terminate_handler() ) {
            return STATUS_FAIL;
        }

        /*
         * Send the launch directives.
         */
        if (STATUS_FAIL == send_launch_directives()) {
            return STATUS_FAIL;
        }

        /*
         * Wait for the launcher to launch the job and get the namespace of
         * the application.
         */
        debug_print("Wait for launch to complete\n");
        wait_for_condition(&launch_complete_cond);
        debug_print("Launch complete\n");

        // At this point we have the application info in 'application_proc'

        /*
         * Extract the proctable and fill in the MPIR information.  If there
         * is a debugger controlling us and it knows about MPIR, it will
         * probably attach to the application processes.
         */
        if (STATUS_FAIL == pmix_proc_table_to_mpir()) {
            return STATUS_FAIL;
        }

        /*
         * Register for the "application has terminated" event.
         * In a 'proxy' (prterun) scenario this will tell us when the job is
         * done and avoid a race between the prterun shutting down and this
         * processes receiving the event.
         */
        if (MPIR_SHIM_PROXY_MODE == mpir_mode) {
            if (STATUS_FAIL == register_application_terminate_handler() ) {
                return STATUS_FAIL;
            }
        }

        /*
         * Release the launcher process and allow it to run.
         */
        if (STATUS_FAIL == release_procs_in_namespace(launcher_proc.nspace, 0)) {
            return STATUS_FAIL;
        }

#ifndef MPIR_SHIM_TESTCASE
        /*
         * Also release the application processes and allow them to run.
         * - If we are building this for the shim testcases then we skip this
         *   and let them do it in their own time.
         */
        if (STATUS_FAIL == release_procs_in_namespace(application_proc.nspace,
                                                      PMIX_RANK_WILDCARD)) {
            return STATUS_FAIL;
        }
#endif
    }
    /*
     * If we are connecting to a running PID
     */
    else {
        /*
         * Already connected to the launcher during initialize_as_tool()
         */

        /*
         * Access the application's namespace
         */
        if (STATUS_FAIL == query_application_namespace()) {
            return STATUS_FAIL;
        }

        /*
         * Extract the proctable and fill in the MPIR information.  If there
         * is a debugger controlling us and it knows about MPIR, it will
         * probably attach to the application processes.
         */
        if (STATUS_FAIL == pmix_proc_table_to_mpir()) {
            return STATUS_FAIL;
        }

        /*
         * Register for the "application has terminated" event.
         */
        if (STATUS_FAIL == register_application_terminate_handler() ) {
            return STATUS_FAIL;
        }
    }

    /*
     * Wait for the launcher to terminate.
     */
    debug_print("Waiting for launcher to terminate\n");
    wait_for_condition(&launch_term_cond);
    debug_print("Launcher terminated\n");

    /*
     * Finalize as a PMIx tool.
     */
    debug_print("Finalizing as a PMIx tool\n");
    (void) finalize_as_tool();

    /*
     * If the launcher returned an exit code, pass it along,
     * otherwise exit with 0.
     */
    debug_print("Exiting with status %d\n", launcher_exit_code);
    return launcher_exit_code;
}


/*
 * Functions specific to the testing version of this library (not shipped)
 * that make it easier to automate the testing.
 */
#ifdef MPIR_SHIM_TESTCASE
#include "mpirshim_test.h"

/**
 * @name   MPIR_release_application
 * @brief  Release application processes from hold in MPI_Init so they may
 *         contine execution.
 * @return STATUS_OK if successful, STATUS_FAIL otherwise
 */
int MPIR_Shim_release_application(void)
{
    MPIR_SHIM_DEBUG_ENTER("");
    return release_procs_in_namespace(application_proc.nspace, PMIX_RANK_WILDCARD);
}
#endif
