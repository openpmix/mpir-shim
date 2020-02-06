/*
 * A PMIx to MPIR wrapper program that extracts PMIx proc table
 * information from a PMIx launcher process (prun, mpirun, etc.) and
 * uses it to implement the MPIR specification.
 *
 * A tool that supports MPIR but does not yet support PMIx should be
 * able to use this program to acquire PMIx parallel processes.
 *
 * For example, it could be used with a legacy version of TotalView
 * that does not support PMIx:
 *
 *   totalview -args mpir prun -n 4 a.out
 *
 * TotalView sees the MPIR symbols in this wrapper program ("mpir")
 * and treats it like an MPIR starter program.  TotalView will attach
 * to the PMIx-enabled application ("a.out") processes launched by the
 * PMIx launcher program ("prun").
 *
 * This wrapper program initializes itself as a PMIx tool, spawns the
 * PMIx launcher program, which in turn launches the application, and
 * requests that the application wait for the debugger to attach.
 * Once the application is launched and waiting, this wrapper extracts
 * the PMIx proc table from a PMIx server, stores the data in the MPIR
 * variables, and calls MPIR_Breakpoint() to notify the debugger.  It
 * then waits for the PMIx launcher to exit.  Note that in MPIR lingo,
 * this wrapper program is an "MPIR starter process," even though it
 * proxies all launch requests through the PMIx server.
 *
 * Unfortunately, if anything goes wrong, this wrapper program will
 * either hang or generate a fatal error.  If that happens, it tends
 * to leave the spawned processes around, which have to be cleaned up
 * manually.  The most common thing to go wrong is to try to launch a
 * non-PMIx application with a PMIx launcher.  For example, the
 * following is know to NOT work:
 *
 *   mpir prun -n 4 hostname
 *
 * The "hostname" processes will run away, and the mpir and prun
 * process will hang.
 */

/*
 * Update log
 *
 * Jan 31 2020 JVD: Cleaned up the code to make it more production worthy.
 * Jan 28 2020 JVD: Added support for MPIR.
 * Jan 20 2020 JVD: Copied from PMIs's indirect.c example program, made more
 *		    C++-ish, and modified to dump MPIR_proctable[] information.
 */

/*
 * The following comment was in PMIx's indirect.c example program.
 */

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
 * Copyright (c) 2013-2019 Intel, Inc.  All rights reserved.
 * Copyright (c) 2015      Mellanox Technologies, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

/*
 * Copyright (c) 2020      Perforce Software, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>
#include <stdarg.h>

#include <pmix_tool.h>

#include <string>
#include <set>

/*  */
/**********************************************************************/
/* MPIR */
/**********************************************************************/
/*
 * MPIR definitions.
 *
 * The following comments and definitions were taken from "The MPIR
 * Process Acquisition Interface, Version 1.1 document."
 *
 * See: https://www.mpi-forum.org/docs/mpir-specification-03-01-2018.pdf
 *
 * We do NOT implement the entire MPIR interface here, just the parts
 * needed for basic MPIR support.
 */

/* 
 * The VOLATILE macro is defined to the volatile keyword for C++ and
 * ANSI C. Declaring a variable volatile informs the compiler that the
 * variable could be modified by an external entity and that its value
 * can change at any time.  VOLATILE is used with MPIR variables
 * defined in the starter and MPI processes but are set by the tool.
 */
#ifndef VOLATILE
#if defined(__STDC__) || defined(__cplusplus)
#define VOLATILE volatile
#else
#define VOLATILE
#endif
#endif

/*
 * MPIR_PROCDESC is a typedef name for an anonymous structure that
 * holds process de- scriptor information for a single MPI process.
 * The structure must contain three members with the same names and
 * types as specified above.  The tool must use the symbol table
 * information to determine the overall size of the structure, and
 * offset and size of each of the structures members.
 */
typedef struct {
  const char *host_name;
  const char *executable_name;
  int pid;
} MPIR_PROCDESC;

/*
 * MPIR_being_debugged is an integer variable that is set or cleared by
 * the tool to notify the starter process that a tool is present.
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
#define MPIR_DEBUG_SPAWNED  1   /* the starter process has spawned the MPI processes 
                                   and filled in the process descriptor table. The
                                   tool can attach to any additional MPI processes 
                                   that have appeared in the process descriptor table. 
                                   This is known as a “job spawn event”. */
#define MPIR_DEBUG_ABORTING 2   /* The MPI job has aborted and the tool can notify
                                   the user of the abort condition. The tool can
                                   read the reason for the job by reading the 
                                   character string out of the starter process, 
                                   which is pointed to by the MPIR_debug_abort_string
                                   variable in the starter process. */

/*
 * MPIR_debug_state is an integer value set in the starter process that
 * specifies the state of the MPI job at the point where the starter
 * process calls the MPIR_Breakpoint function.
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
char *MPIR_debug_abort_string = 0;

/*
 * MPIR_Breakpoint is the subroutine called by the starter process to
 * notify the tool that an MPIR event has occurred. The starter process
 * must set the MPIR_debug_state variable to an appropriate value before
 * calling this subroutine. The tool must set a breakpoint at the
 * MPIR_Breakpoint function, and when a thread running the starter
 * process hits the breakpoint, the tool must read the value of the
 * MPIR_debug_state variable to process an MPIR event.
 */
extern "C"
{
  void MPIR_Breakpoint ()
    {
      return;
    }  /* MPIR_Breakpoint */
}

/*
 * MPIR_i_am_starter is a symbol of any type (preferably int) that marks
 * the process containing the symbol definition as a starter process
 * that is not also an MPI process. This symbol serves as a flag to mark
 * the process as a separate starter process or an MPI rank 0 process.
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
 * Not implemented here:
 */
// VOLATILE int MPIR_debug_gate;
// int MPIR_acquired_pre_main;
// char MPIR_executable_path[256];
// char MPIR_server_arguments[1024];
// char MPIR_attach_fifo[256];

/*  */
/**********************************************************************/
/* PMIx namespace */
/**********************************************************************/
/*
 * PMIx C++ binding definitions for convenience.
 */

namespace pmix
{
  typedef pmix_data_type_t data_type_t;
  typedef pmix_hdlr_reg_cbfunc_t hdlr_reg_cbfunc_t;
  typedef pmix_notification_fn_t notification_fn_t;
  typedef pmix_nspace_t nspace_t;
  typedef pmix_rank_t rank_t;
  typedef pmix_status_t status_t;

  struct app_t : pmix_app_t
  {
    app_t() { PMIX_APP_CONSTRUCT (this); }
    ~app_t() { PMIX_APP_DESTRUCT (this); }
  };  /* app_t */

  struct info_t : pmix_info_t
  {
    info_t() { PMIX_INFO_CONSTRUCT (this); }
    info_t(const char *k_, const void *d_, const data_type_t t_) { load (k_, d_, t_); }
    info_t(const char *k_, const void *d_) { load (k_, d_); }
    info_t(const char *k_, const char *d_) { load (k_, d_); }
    info_t(const char *k_, const bool d_)  { load (k_, d_); }
    void load(const char *k_, const void *d_, const data_type_t t_) { PMIX_INFO_LOAD (this, k_, d_, t_); }
    void load(const char *k_, const void *d_) { load (k_, d_, PMIX_POINTER); }
    void load(const char *k_, const char *d_) { load (k_, d_, PMIX_STRING); }
    void load(const char *k_, const bool d_)  { load (k_, &d_, PMIX_BOOL); }
    ~info_t() { PMIX_INFO_DESTRUCT (this); }
  };  /* info_t */

  struct proc_t : pmix_proc_t
  {
    proc_t() { PMIX_PROC_CONSTRUCT (this); }
    proc_t(const nspace_t ns_, rank_t r_) { load (ns_, r_); }
    void load(const nspace_t ns_, rank_t r_) { PMIX_PROC_LOAD (this, ns_, r_); }
    ~proc_t() { PMIX_PROC_DESTRUCT (this); }
  };  /* proc_t */

  struct query_t : pmix_query_t
  {
    query_t() { PMIX_QUERY_CONSTRUCT (this); }
    ~query_t() { PMIX_QUERY_DESTRUCT (this); }
  };  /* query_t */

  struct envar_t : pmix_envar_t
  {
    envar_t() { PMIX_ENVAR_CONSTRUCT (this); }
    envar_t(const char *e_, const char *v_, char s_) { load (e_, v_, s_); }
    void load(const char *e_, const char *v_, char s_) { PMIX_ENVAR_LOAD (this, e_, v_, s_); }
    ~envar_t() { PMIX_ENVAR_DESTRUCT (this); }
  };  /* envar_t */

}  /* namespace pmix */

/**********************************************************************/
/* Our PMIx objects */
/**********************************************************************/
/*
 * A lock object for synchronizing the main and callback threads.
 */

struct lock_t
{

  pthread_mutex_t mutex;
  pthread_cond_t cond;
  volatile bool active;
  pmix::status_t status;
  int count;

  lock_t()
    { 
      pthread_mutex_init (&mutex, NULL);
      pthread_cond_init (&cond, NULL);
      active = true;
      status = PMIX_SUCCESS;
      count = 0;
    }  /* lock_t */

  ~lock_t()
    {
      pthread_mutex_destroy (&mutex);
      pthread_cond_destroy (&cond);
    }  /* ~lock_t */

  void wait_thread()
    {
      pthread_mutex_lock (&mutex);
      while (active)
	pthread_cond_wait (&cond, &mutex);
      pthread_mutex_unlock (&mutex);
    }  /* wait_thread */

  void wakeup_thread()
  {
    pthread_mutex_lock (&mutex);
    active = false;
    pthread_cond_broadcast (&cond);
    pthread_mutex_unlock (&mutex);
  }  /* wakeup_thread */

};  /* lock_t */

/**********************************************************************/
/*
 * An object for returning query info data.
 */

struct query_data_t
{

  lock_t lock;
  pmix::status_t status;
  pmix::info_t *info;
  size_t ninfo;
  pmix::app_t *apps;
  size_t napps;

  query_data_t()
    : lock()
    {
      status = PMIX_SUCCESS;
      info = 0;
      ninfo = 0;
      apps = 0;
      napps = 0;
    }  /* query_data_t */

  ~query_data_t()
    {
      delete [] info;
      info = 0;
      ninfo = 0;
      delete [] apps;
      apps = 0;
      napps = 0;
    }  /* ~query_data_t */

};  /* query_data_t */

/**********************************************************************/
/*
 * An object to allow releasing the main thread when an event happens.
 */

struct release_t
{

  lock_t lock;
  const char *nspace;
  int exit_code;
  bool exit_code_given;

  release_t()
    : lock()
    {
      nspace = 0;
      exit_code = 0;
      exit_code_given = false;
    }  /* release_t */

  ~release_t()
    {
      delete nspace;
      nspace = 0;
    }  /* ~release_t */

};  /* release_t */

/**********************************************************************/
/*
 * An object to make it easier to register events.
 */

struct register_event_handler_t
{

  pmix::status_t lock_status;

  register_event_handler_t() {}
  ~register_event_handler_t() {}

  pmix::status_t register_event_handler (pmix::status_t codes_[], size_t ncodes_,
					 pmix::info_t info_[], size_t ninfo_,
					 pmix::notification_fn_t event_hdlr_,
					 pmix::hdlr_reg_cbfunc_t cbfunc_)
  {
    lock_t lock;
    pmix::status_t rv = PMIx_Register_event_handler (codes_, ncodes_,
						     info_, ninfo_,
						     event_hdlr_,
						     cbfunc_, &lock);
    lock.wait_thread();
    lock_status = lock.status;
    return rv;
  }  /* register_event_handler */

  pmix::status_t register_event_handler (pmix::notification_fn_t event_hdlr_,
					 pmix::hdlr_reg_cbfunc_t cbfunc_)
  {
    return register_event_handler ((pmix::status_t*)0, size_t(0),
				   (pmix::info_t*)0, size_t(0),
				   event_hdlr_,
				   cbfunc_);
  }  /* register_event_handler */

};  /* register_event_handler_t */

/*  */
/**********************************************************************/
/* PMIx to MPIR wrapper tool code */
/**********************************************************************/
/* Global variables. */

static const char whoami[] = "mpir";	/* The name we go by */
static pmix::proc_t myproc;		/* Our (mpir's) PMIx process structure */
static bool debug_output = false;	/* Generate debug output? */

/* Note that we share the executable and hostname strings across
 * MPIR_proctable entries.  MPIR says:
 *
 *   The MPI implementation should share the host and executable name
 *   character strings across multiple process descriptor entries
 *   whenever possible.  For example, if all of the MPI processes are
 *   executing "/path/a.out", then the executable name field in each
 *   process descriptor should point to the same null-terminated
 *   character string.  Sharing the strings enhances the tools
 *   scalability by allowing it to cache data from the starter process
 *   and avoid reading redundant character strings.
 */
static std::set<std::string> mpir_executables, mpir_hostnames;

/**********************************************************************/
/* Utilities */
/**********************************************************************/
/* Print a fatal error message and exit. */

#if (__GNUC__)
static void
fatal_error (const char *format_ ...) __attribute__ ((format (printf, 1, 2)));
#endif

static void
fatal_error (const char *format_ ...)
{
  fprintf (stderr,
	   "%s: FATAL ERROR: ",
	   whoami);
  va_list arg_list;
  va_start (arg_list, format_);
  vfprintf (stderr, format_, arg_list);
  va_end (arg_list);
  fprintf (stderr, "\n");
  exit (1);
}  /* fatal_error */

/**********************************************************************/
/* Print a fatal error message along with the PMIx status (if it's not
   PMIX_SUCCESS), finialize the tool, and exit. */

#if (__GNUC__)
static void
pmix_fatal_error (pmix::status_t rc_, const char *format_ ...) __attribute__ ((format (printf, 2, 3)));
#endif

static void
pmix_fatal_error (pmix::status_t rc_, const char *format_ ...)
{
  fprintf (stderr,
	   "%s: FATAL ERROR: ",
	   whoami);
  va_list arg_list;
  va_start (arg_list, format_);
  vfprintf (stderr, format_, arg_list);
  va_end (arg_list);
  if (PMIX_SUCCESS != rc_)
    fprintf (stderr,
	     ": %s (%d)",
	     PMIx_Error_string (rc_),
	     rc_);
  fprintf (stderr, "\n");
  PMIx_tool_finalize();
  exit (1);
}  /* pmix_fatal_error */

/**********************************************************************/
/* Internal "debug printf" function. */

#if (__GNUC__)
static void
debug_printf (const char *format_ ...) __attribute__ ((format (printf, 1, 2)));
#endif

static pthread_mutex_t debug_printf_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
debug_printf (const char *format_ ...)
{
  if (debug_output)
    {
      pthread_mutex_lock (&debug_printf_mutex);
      fprintf (stderr,
	       "%s[%s:%u:%lu]: ",
	       whoami,
	       myproc.nspace,
	       (unsigned int) myproc.rank,
	       (unsigned long) getpid());
      va_list arg_list;
      va_start (arg_list, format_);
      vfprintf (stderr, format_, arg_list);
      va_end (arg_list);
      pthread_mutex_unlock (&debug_printf_mutex);
    }  /* if */
}  /* debug_printf */

/**********************************************************************/
/* Function entry/exit object. */

struct entry_exit_t
{
  const char *func;
  const char *file;
  int line;
  entry_exit_t (const char *func_, const char *file_, int line_)
    : func(func_), file(file_), line(line_)
    {
      debug_printf ("ENTERING: %s(), %s#%d\n", func, file, line);
    }
  ~entry_exit_t()
    {
      debug_printf ("EXITING : %s()\n", func);
    }
};  /* entry_exit_t */

#define NOTE_ENTRY_EXIT() entry_exit_t entry_exit (__func__, __FILE__, __LINE__)

/**********************************************************************/
/* Print a usage message and exit */

static void
usage()
{
  fprintf (stderr,
	   "PMIx to MPIR wrapper program.\n\n"
	   "Usage: %s [OPTION] {prun|mpirun|mpiexec|prrterun} [ARGS] PROG [PROG-ARGS]\n\n"
	   "-h | --help    This message.\n"
	   "-d | --debug   Enable debug messages.\n\n"
	   "Report bugs to /dev/null\n",
	   whoami);
  exit (1);
}  /* usage */

/**********************************************************************/
/* Callback and event handler functions */
/**********************************************************************/
/* This is a callback function for the PMIx_Query API.  The query will
 * callback with a status indicating if the request could be fully
 * satisfied, partially satisfied, or completely failed.  The info
 * parameter contains an array of the returned data, with the info->key
 * field being the key that was provided in the query call.  Thus, you
 * can correlate the returned data in the info->value field to the
 * requested key.
 *
 * Once we have dealt with the returned data, we must call the
 * release_fn_ so that the PMIx library can cleanup.
 */

static void
query_callback_fn (pmix_status_t status_,
		   pmix_info_t *info_, size_t ninfo_,
		   void *cbdata_,
		   pmix_release_cbfunc_t release_fn_,
		   void *release_cbdata_)
{
  NOTE_ENTRY_EXIT();

  query_data_t *mq = (query_data_t*) cbdata_;
  mq->status = status_;

  /*
   * Save the returned info - the PMIx library "owns" it and will
   * release it and perform other cleanup actions when release_fn_()
   * is called.
   */
  if (0 < ninfo_)
    {
      mq->info = new pmix::info_t[ninfo_];
      mq->ninfo = ninfo_;
      for (size_t n = 0; n < ninfo_; n++)
	{
	  debug_printf ("Key '%s' Type '%s' (%d)\n",
			info_[n].key,
			PMIx_Data_type_string(info_[n].value.type),
			info_[n].value.type);
	  PMIX_INFO_XFER (&mq->info[n], &info_[n]);
	}  /* for */
    }  /* if */

  /*
   * Let the library release the data and cleanup from the operation.
   */
  if (NULL != release_fn_)
    release_fn_ (release_cbdata_);

  /*
   * Release the lock
   */
  mq->lock.wakeup_thread();
}  /* query_callback_fn */

/**********************************************************************/
/* This is the default event notification function we pass down below
 * when registering for general events.  We don't technically need to
 * register one, but it is usually good practice to catch any events
 * that occur.
 */

static void
default_notification_fn (size_t evhdlr_registration_id_,
			 pmix_status_t status_,
			 const pmix_proc_t *source_,
			 pmix_info_t info_[], size_t ninfo_,
			 pmix_info_t results_[], size_t nresults_,
			 pmix_event_notification_cbfunc_fn_t cbfunc_,
			 void *cbdata_)
{
  NOTE_ENTRY_EXIT();

  /* We don't do anything with default events.  Should we? */
  if (NULL != cbfunc_)
    cbfunc_ (PMIX_SUCCESS, NULL, 0, NULL, NULL, cbdata_);
}  /* default_notification_fn */

/**********************************************************************/
/* Event handler registration is done asynchronously because it may
 * involve the PMIx server registering with the host RM for external
 * events.  So we provide a callback function that returns the status of
 * the request (success or an error), plus a numerical index to the
 * registered event.  The index is used later on to deregister an event
 * handler - if we don't explicitly deregister it, then the PMIx server
 * will do so when it see us exit.
 */

static void
evhandler_reg_callbk (pmix_status_t status_,
		      size_t evhandler_ref_,
		      void *cbdata_)
{
  NOTE_ENTRY_EXIT();

  lock_t *lock = (lock_t *) cbdata_;
  if (PMIX_SUCCESS != status_)
    pmix_fatal_error (status_,
		      "Event handler registration failed: "
		      "evhandler_ref_=%lu",
		      (unsigned long) evhandler_ref_);
  lock->status = status_;
  lock->wakeup_thread();
}  /* evhandler_reg_callbk */

/**********************************************************************/
/* This is an event notification function that we explicitly request
 * be called when the PMIX_ERR_JOB_TERMINATED notification is issued
 * and when the launcher is ready.  We could catch it in the general
 * event notification function and test the status to see if it was
 * "job terminated" or "launcher ready", but it often is simpler to
 * declare a use-specific notification callback point.  In this case,
 * we are asking to know whenever the launcher is ready or terminates,
 * and we will then know we can proceed or exit.
 */

static void
launcher_release_fn (size_t evhdlr_registration_id_,
		     pmix_status_t status_,
		     const pmix_proc_t *source_,
		     pmix_info_t info_[], size_t ninfo_,
		     pmix_info_t results_[], size_t nresults_,
		     pmix_event_notification_cbfunc_fn_t cbfunc_,
		     void *cbdata_)
{
  NOTE_ENTRY_EXIT();

  /*
   * Find our return object.
   */
  release_t *release = NULL;
  bool exit_code_found = false;
  int exit_code;
  pmix_proc_t *affected_proc = NULL;
  for (size_t n = 0; n < ninfo_; n++)
    {
      if (PMIX_CHECK_KEY (&info_[n], PMIX_EVENT_RETURN_OBJECT))
	{
	  release = (release_t *) info_[n].value.data.ptr;
	}  /* if */
      else if (PMIX_CHECK_KEY (&info_[n], PMIX_EXIT_CODE))
	{
	  exit_code = info_[n].value.data.integer;
	  exit_code_found = true;
	}  /* else-if */
      else if (PMIX_CHECK_KEY (&info_[n], PMIX_EVENT_AFFECTED_PROC))
	{
	  affected_proc = info_[n].value.data.proc;
	}  /* else-if */
    }  /* for */

  /*
   * If the object wasn't returned, then that is an error.
   */
  if (NULL == release)
    pmix_fatal_error (PMIX_SUCCESS,
		      "Release object wasn't returned in callback");

  /*
   * See if the code is LAUNCHER_READY.
   */
  if (PMIX_LAUNCHER_READY == status_)
    {
      debug_printf ("Notified that launcher is ready\n");
    }  /* if */
  else
    {
      debug_printf ("Notified job '%s' terminated, affected '%s'\n",
		    release->nspace,
		    (NULL == affected_proc
		     ? "NULL"
		     : affected_proc->nspace));
      if (exit_code_found)
	{
	  release->exit_code = exit_code;
	  release->exit_code_given = true;
	}  /* if */
    }  /* else */

  /*
   * Release the main thread.
   */
  release->lock.wakeup_thread();

  /*
   * Tell the event handler state machine that we are the last step.
   */
  if (NULL != cbfunc_)
    cbfunc_ (PMIX_EVENT_ACTION_COMPLETE, NULL, 0, NULL, NULL, cbdata_);
}  /* launcher_release_fn */

/**********************************************************************/
/* This is an event notification function that we explicitly request
 * be called when the PMIX_LAUNCH_COMPLETE notification is issued.  It
 * gathers the namespace of the application and returns it in the
 * release object.  We need the application namespace to query the
 * job's proc table and allow it to run after the MPI_Breakpoint() is
 * called.
 */

static void
debugger_release_fn (size_t evhdlr_registration_id_,
		     pmix_status_t status_,
		     const pmix_proc_t *source_,
		     pmix_info_t info_[], size_t ninfo_,
		     pmix_info_t results_[], size_t nresults_,
		     pmix_event_notification_cbfunc_fn_t cbfunc_,
		     void *cbdata_)
{
  NOTE_ENTRY_EXIT();

  const char *appspace = 0;
  release_t *release = NULL;

  /*
   * Search for the namespace of the application.
   */
  for (size_t n = 0; n < ninfo_; n++)
    {
      if (PMIX_CHECK_KEY (&info_[n], PMIX_NSPACE))
	{
	  appspace = info_[n].value.data.string;
	  debug_printf ("PMIX_NSPACE key found: namespace '%s'\n",
			appspace);
	}  /* if */
      else if (PMIX_CHECK_KEY (&info_[n], PMIX_EVENT_RETURN_OBJECT))
	{
	  release = (release_t *) info_[n].value.data.ptr;
	  debug_printf ("PMIX_EVENT_RETURN_OBJECT key found: pointer '%p'\n",
			release);
	}  /* else-if */
    }  /* for */

  /*
   * If the object wasn't returned, then that is an error.
   */
  if (NULL == release)
    pmix_fatal_error (PMIX_SUCCESS,
		      "Release object wasn't returned in callback");

  /*
   * If the namespace of the launched job wasn't returned, then that
   * is an error.
   */
  if (NULL == appspace)
    pmix_fatal_error (PMIX_SUCCESS,
		      "Launched application namespace wasn't returned in callback");

  /*
   * Copy the namespace of the application into the release object.
   */
  debug_printf ("Application namespace is '%s'\n",
		appspace);
  release->nspace = strdup (appspace);

  /*
   * Tell the event handler state machine that we are the last step
   */
  if (NULL != cbfunc_)
    cbfunc_ (PMIX_EVENT_ACTION_COMPLETE, NULL, 0, NULL, NULL, cbdata_);

  /*
   * Release the main thread.
   */
  release->lock.wakeup_thread();
}  /* debugger_release_fn */

/**********************************************************************/
/* Extract the PMIx proc table and use it to fill-in the MPIR proc
 * table.  Then, call MPIR_Breakpoint() to notify the debugger that is
 * debugging this process.
 */

static void
pmix_proc_table_to_mpir (const char *appspace_)
{
  NOTE_ENTRY_EXIT();

  pmix::status_t rc;

  /*
   * Extract proc table for the application namespace.
   */
  pmix::query_t query;
  PMIX_ARGV_APPEND (rc, query.keys, PMIX_QUERY_PROC_TABLE);
  query.qualifiers = new pmix::info_t (PMIX_NSPACE, appspace_);
  query.nqual = 1;
  query_data_t query_data;
  rc = PMIx_Query_info_nb (&query, 1, query_callback_fn, (void *) &query_data);
  if (PMIX_SUCCESS != rc)
    pmix_fatal_error (rc, "PMIx_Query_info_nb() failed");

  /*
   * Wait for a response.
   */
  debug_printf ("Waiting for proc table query response\n");
  query_data.lock.wait_thread();
  debug_printf ("Proc table query response received\n");

  /*
   * Check the query data status, info/ninfo, and data type (which
   * should be a data array).
   */
  if (PMIX_SUCCESS != query_data.status)
    pmix_fatal_error (rc, "PMIx proc table status error");
  if (NULL == query_data.info || 0 == query_data.ninfo)
    pmix_fatal_error (rc, "PMIx proc table info/ninfo is 0");
  if (PMIX_DATA_ARRAY != query_data.info[0].value.type)
    pmix_fatal_error (rc, "PMIx proc table has incorrect data type: %s (%d)",
		      PMIx_Data_type_string (query_data.info[0].value.type),
		      (int) query_data.info[0].value.type);
  if (NULL == query_data.info[0].value.data.darray->array)
    pmix_fatal_error (rc, "PMIx proc table data array is null");
  if (PMIX_PROC_INFO != query_data.info[0].value.data.darray->type)
    pmix_fatal_error (rc, "PMIx proc table data array has incorrect type: %s (%d)",
		      PMIx_Data_type_string (query_data.info[0].value.data.darray->type),
		      (int) query_data.info[0].value.data.darray->type);

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
  const size_t nprocs = query_data.info[0].value.data.darray->size;
  const pmix_proc_info_t *proc_info =
    (pmix_proc_info_t *) query_data.info[0].value.data.darray->array;
  if (debug_output)
    {
      debug_printf ("Received PMIx proc table for %lu procs:\n",
		    (unsigned long) nprocs);
      for (int i = 0; i < nprocs; i++)
	{
	  const pmix_proc_info_t *p = proc_info + i;
	  debug_printf ("proc_table[%d]: rank=%d, hostname='%s', "
			"executable_name='%s', pid=%d, exit_code=%d, "
			"state='%s'\n",
			i,
			int (p->proc.rank),
			p->hostname,
			p->executable_name,
			int (p->pid),
			p->exit_code,
			PMIx_Proc_state_string(p->state));
	}  /* for */
    }  /* if */

  /*
   * Create the MPIR data structures.
   *
   */
  MPIR_proctable = new MPIR_PROCDESC[nprocs];
  MPIR_proctable_size = nprocs;
  for (int i = 0; i < nprocs; i++)
    {
      const pmix_proc_info_t *p = proc_info + i;
      std::pair <std::set<std::string>::iterator,bool> host_res =
	mpir_hostnames.insert (std::string (p->hostname));
      std::pair <std::set<std::string>::iterator,bool> exec_res =
	mpir_executables.insert (std::string (p->executable_name));
      MPIR_proctable[i].host_name = host_res.first->c_str();
      MPIR_proctable[i].executable_name = exec_res.first->c_str();
      MPIR_proctable[i].pid = p->pid;
    }  /* for */
  MPIR_debug_state = MPIR_DEBUG_SPAWNED;

  /*
   * Notify the debugger.
   */
  MPIR_Breakpoint();
}  /* pmix_proc_table_to_mpir */

/**********************************************************************/
/* The main() program. */

int main (int argc, char **argv)
{
  /*
   * Process any arguments we were given.
   */
  int argi = 1;			/* Index of starter program name  */
  for (int i = 1; i < argc; i++)
    {
      if (argv[i][0] != '-')
	break;
      argi = i + 1;
      if (!strcmp (argv[i], "-h") || !strcmp (argv[i], "--help"))
	usage();		/* Print the usage message and exit */
      else if (!strcmp (argv[i], "-d") || !strcmp (argv[i], "--debug"))
	debug_output = true;
    }  /* for */
  if (argi >= argc)		/* No program arguments? */
    usage();			/* Print the usage message and exit. */

  NOTE_ENTRY_EXIT();

  /*
   * Check to see if we are using an intermediate launcher that we
   * recognize.  This is a crock... Is it necessary?
   */
  debug_printf ("Checking if '%s' is a recognized launcher\n", argv[argi]);
  {  /* let */
    static const char *launchers[] =
      {
	"prun",
	"mpirun",
	"mpiexec",
	"prrterun",
	NULL
      };
    bool found = false;
    for (int n = 0; NULL != launchers[n]; n++)
      {
	if (!strcmp (argv[argi], launchers[n]))
	  {
	    found = true;
	    break;
	  }   /* if */
      }  /* for */
    if (!found)
      usage();			/* A recognized launcher was not found */
  }  /* let */

  /*
   * Initialize ourselves as a PMIx tool.
   */
  debug_printf ("Initializing as a PMIx tool\n");
  {  /* let */
				/* Use the system connection first, if available. */
    pmix::info_t info (PMIX_CONNECT_SYSTEM_FIRST, true);
				/* PMIx_tool_init() starts a thread running PMIx progress_engine() */
    pmix::status_t rc = PMIx_tool_init (&myproc, &info, 1);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc, "PMIx_tool_init() failed");
  }  /* let */

  debug_printf ("Running as a PMIx tool\n");

  /*
   * Object we use to register event handlers.
   */
  register_event_handler_t event_registrar;

  /* Default event handler */
  debug_printf ("Registering default event handler\n");
  {  /* let */
    pmix::status_t rc =
      event_registrar.register_event_handler (default_notification_fn,
					      evhandler_reg_callbk);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc, "Registering default event handler");
  }  /* let */

  /*
   * Objects used to synchronize initial and callback/event threads.
   */
  release_t launcher_ready;	/* Launcher is ready to communicate */
  release_t launcher_complete;	/* Launcher has completed launching */
  release_t launcher_terminate;	/* Launcher has terminated */

  /*
   * Register to receive the "launcher-ready" event telling us that
   * the launcher is ready for us to connect to it.  The "launcher"
   * here is prun, mpirun, mpiexec, etc.
   */
  debug_printf ("Registering \"launcher-ready\" event handler\n");
  {  /* let */
    pmix::status_t code = PMIX_LAUNCHER_READY;
    pmix::info_t info[2], *iptr = info;
    (iptr++)->load (PMIX_EVENT_RETURN_OBJECT, (void *) &launcher_ready);
    (iptr++)->load (PMIX_EVENT_HDLR_NAME, "LAUNCHER-READY");
    pmix::status_t rc =
      event_registrar.register_event_handler (&code, 1,
					      info, iptr - info,
					      launcher_release_fn,
					      evhandler_reg_callbk);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc,
			"Registering \"launcher-ready\" event handler");
    if (PMIX_SUCCESS != event_registrar.lock_status)
      pmix_fatal_error (event_registrar.lock_status,
			"Registering \"launcher-ready\" event handler: lock status");
  }  /* let */

  /*
   * Register to receive the "launch-complete" event, which will
   * telling us the namespace of the job being debugged, so that we
   * can fill in the MPIR_proctable[].
   */
  debug_printf ("Registering \"launcher-complete\" event handler\n");
  {  /* let */
    pmix::status_t code = PMIX_LAUNCH_COMPLETE;
    pmix::info_t info[2], *iptr = info;
    (iptr++)->load (PMIX_EVENT_RETURN_OBJECT, (void *) &launcher_complete);
    (iptr++)->load (PMIX_EVENT_HDLR_NAME, "LAUNCHER-COMPLETE");
    pmix::status_t rc =
      event_registrar.register_event_handler (&code, 1,
					      info, iptr - info,
					      debugger_release_fn,
					      evhandler_reg_callbk);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc,
			"Registering \"launch-complete\" event handler");
    if (PMIX_SUCCESS != event_registrar.lock_status)
      pmix_fatal_error (event_registrar.lock_status,
			"Registering \"launch-complete\" event handler: lock status");
  }  /* let */

  /*
   * The namespace of the launcher process.
   */
  char clientspace[PMIX_MAX_NSLEN+1];

  /*
   * We are going to spawn an intermediate launcher (prun, mpirun,
   * etc.), and we will use the reference PMIx erver to start it.
   * Tell it to wait after launch for directive prior to spawning the
   * application.
   */
  {  /* let */
    /*
     * Setup the launcher's application parameters.
     */
    pmix::status_t rc;
    pmix::app_t app;
				/* The executable name */
    app.cmd = strdup (argv[argi]);
				/* The argv to pass to the application */
    PMIX_ARGV_APPEND (rc, app.argv, argv[argi]);
    for (int n = argi + 1; n < argc; n++)
      {
	PMIX_ARGV_APPEND (rc, app.argv, argv[n]);
      }	 /* for */
				/* Try to use the same working directory */
    char cwd[PATH_MAX];
    getcwd (cwd, PATH_MAX);
    app.cwd = strdup(cwd);
				/* Just one launcher process please */
    app.maxprocs = 1;
    /*
     * Provide job-level directives so the apps do what the user requested
     */
    pmix::info_t info[7], *iptr = info;
				/* Map by slot */
    (iptr++)->load (PMIX_MAPBY, "slot");
				/* Tell the launcher to wait for directives */
    char *tmp;
    asprintf (&tmp, "%s:%d", myproc.nspace, myproc.rank);
    pmix::envar_t envar ("PMIX_LAUNCHER_PAUSE_FOR_TOOL", tmp, ':');
    free(tmp);
    (iptr++)->load (PMIX_SET_ENVAR, &envar, PMIX_ENVAR);
				/* stdout/stderr forwarding */
    (iptr++)->load (PMIX_FWD_STDOUT, true);
    (iptr++)->load (PMIX_FWD_STDERR, true);
				/* Notify us when the job completes */
    (iptr++)->load (PMIX_NOTIFY_COMPLETION, true);
				/* We are spawning a tool */
    (iptr++)->load (PMIX_SPAWN_TOOL, true);
#ifdef PMIX_LAUNCHER_RENDEZVOUS_FILE
#if 0
    // I'm not sure what a rendezvous file is, but this program
    // doesn't need it.
				/* Have it output a specific rndz file */
    (iptr++)->load (PMIX_LAUNCHER_RENDEZVOUS_FILE, "dbgr.rndz.txt");
#endif
#endif
    /*
     * Spawn the job - the function will return when the launcher has
     * been launched.  Note that this doesn't tell us anything about
     * the launcher's state - it just means that the launcher has been
     * fork/exec'd.
     */
    debug_printf ("Spawning launcher '%s'\n", app.cmd);
    rc = PMIx_Spawn (info, iptr - info, &app, 1, clientspace);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc, "PMIx_Spawn() failed");
    debug_printf ("Launcher's namespace is '%s'\n", clientspace);
  }  /* let */

  /*
   * Wait here for the launcher to declare itself ready.
   */
  debug_printf ("Waiting for the launcher to be ready\n");
  launcher_ready.lock.wait_thread();
  debug_printf ("Launcher is ready\n");

  /*
   * Register callback for when the launcher terminates.
   */
  debug_printf ("Registering \"launcher-terminate\" event handler\n");
  {  /* let */
    pmix::status_t code = PMIX_ERR_JOB_TERMINATED;
    launcher_terminate.nspace = strdup(clientspace);
    pmix::info_t info[2], *iptr = info;
    (iptr++)->load (PMIX_EVENT_RETURN_OBJECT, &launcher_terminate);
				/* Only call me back when this specific job terminates */
    pmix::proc_t proc (clientspace, PMIX_RANK_WILDCARD);
    (iptr++)->load (PMIX_EVENT_AFFECTED_PROC, &proc, PMIX_PROC);
    pmix::status_t rc =
      event_registrar.register_event_handler (&code, 1,
					      info, iptr - info,
					      launcher_release_fn,
					      evhandler_reg_callbk);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc,
			"Registering \"launch-terminate\" event handler");
    if (PMIX_SUCCESS != event_registrar.lock_status)
      pmix_fatal_error (event_registrar.lock_status,
			"Registering \"launch-terminate\" event handler: lock status");
  }  /* let */

  /*
   * Send the launch directives.
   */
  {  /* let */
				/* Provide a few job-level directives */
    pmix_data_array_t darray;
				/* Setup the infos for the data array */
    pmix::info_t dinfo[4], *diptr = dinfo;
#if 0
    // A couple of examples of how to set environment variables.
				/* Set FOOBAR=1 */
    pmix::envar_t envar_foobar ("FOOBAR", "1", ':');
    (diptr++)->load (PMIX_SET_ENVAR, &envar_foobar, PMIX_ENVAR);
				/* Set PATH=/home/common/local/toad:$PATH */
    pmix::envar_t envar_path ("PATH", "/home/common/local/toad", ':');
    (diptr++)->load (PMIX_PREPEND_ENVAR, &envar_path, PMIX_ENVAR);
#endif
				/* Stop the processes in PMIx_Init() */
    (diptr++)->load (PMIX_DEBUG_STOP_IN_INIT, true);
				/* Notify us when the job is launched */
    (diptr++)->load (PMIX_NOTIFY_LAUNCH, true);
				/* Fill in the data array fields */
    darray.type = PMIX_INFO;
    darray.size = diptr - dinfo;
    darray.array = dinfo;

    pmix::info_t info[3], *iptr = info;
				/* The launcher's namespace, rank 0 */
    pmix::proc_t proc (clientspace, 0);
				/* Deliver to the target launcher */
    (iptr++)->load (PMIX_EVENT_CUSTOM_RANGE, &proc, PMIX_PROC);
				/* Only non-default handlers */
    (iptr++)->load (PMIX_EVENT_NON_DEFAULT, true);
				/* Load the data array */
    (iptr++)->load (PMIX_DEBUG_JOB_DIRECTIVES, &darray, PMIX_DATA_ARRAY);
    debug_printf ("Sending launch directives\n");
    pmix::status_t rc = 
      PMIx_Notify_event (PMIX_LAUNCH_DIRECTIVE,
			 NULL, PMIX_RANGE_CUSTOM,
			 info, iptr - info,
			 NULL, NULL);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc, "PMIx_Notify_event() failed sending PMIX_LAUNCH_DIRECTIVE");
  }  /* let */

  /*
   * Wait for the launcher to launch the job and get the namespace of
   * the application.
   */
  debug_printf ("Waiting for the launcher's launch to complete\n");
  launcher_complete.lock.wait_thread();
  debug_printf ("Launcher's launch completed\n");

  /*
   * Get the application's namespace. 
   */
  const char *appspace = launcher_complete.nspace;

  /*
   * Extract the proctable and fill in the MPIR information.  If there
   * is a debugger controlling us and it knows about MPIR, it will
   * probably attach to the application processes.
   */
  pmix_proc_table_to_mpir (appspace);

  /*
   * Release the starter process and allow it to run.
   */
  {  /* let */
    pmix::proc_t proc (appspace, PMIX_RANK_WILDCARD);
    pmix::info_t info[2], *iptr = info;
				/* Deliver to the target nspace */
    (iptr++)->load (PMIX_EVENT_CUSTOM_RANGE, &proc, PMIX_PROC);
    (iptr++)->load (PMIX_EVENT_NON_DEFAULT, true);
    debug_printf ("Sending debugger release\n");
    pmix::status_t rc = 
      PMIx_Notify_event (PMIX_ERR_DEBUGGER_RELEASE,
			 NULL, PMIX_RANGE_CUSTOM,
			 info, iptr - info,
			 NULL, NULL);
    if (PMIX_SUCCESS != rc)
      pmix_fatal_error (rc, "PMIx_Notify_event() failed sending PMIX_ERR_DEBUGGER_RELEASE");
  }  /* let */

  /*
   * Wait for the launcher to terminate.
   */
  debug_printf ("Waiting for the launcher to terminate\n");
  launcher_terminate.lock.wait_thread();
  debug_printf ("Launcher has terminated: exit_code_give==%s, exit_code=%d\n",
		launcher_terminate.exit_code_given ? "true" : "false",
		launcher_terminate.exit_code);

  /*
   * Finalize as a PMIx tool.
   */
  debug_printf ("Finalizing as a PMIx tool\n");
  (void) PMIx_tool_finalize();

  /*
   * If the launcher returned an exit code, pass it along,
   * otherwise exit with 0.
   */
  int exit_code = (launcher_terminate.exit_code_given
		   ? launcher_terminate.exit_code
		   : 0);
  debug_printf ("Exiting with status %d\n", exit_code);
  return exit_code;

}  /* main */

