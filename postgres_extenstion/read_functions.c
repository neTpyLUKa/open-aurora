/*-------------------------------------------------------------------------
 *
 * read_functions.c
 *      Receive read query and process them.
 *
 * Copyright (c) 1996-2019, PostgreSQL Global Development Group
 *
 *
 *-------------------------------------------------------------------------
 */

/* Some general headers for custom bgworker facility */
#include "postgres.h"
#include "fmgr.h"
#include "access/xact.h"
#include "lib/stringinfo.h"
#include "pgstat.h"
#include "executor/spi.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "utils/guc.h"
#include "utils/snapmgr.h"
/* Needed for getting hostname of the host */
#include "unistd.h"

/* Allow load of this module in shared libs */
PG_MODULE_MAGIC;

/* Entry point of library loading */
void _PG_init(void);

#if PG_VERSION_NUM >= 100000
void repl_mon_main(Datum) pg_attribute_noreturn();
#endif
/* Signal handling */
static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

/* GUC variables */
static int interval = 1000;
static char *tablename = "repl_mon";

/* Worker name */
static char *worker_name = "read_functions";

static char hostname[HOST_NAME_MAX];

#if PG_VERSION_NUM >= 100000
static char *get_current_lsn = "pg_current_wal_lsn()";
#else
static char *get_current_lsn = "pg_current_xlog_location()";
#endif

// Main functions
#if PG_VERSION_NUM < 100000
static void
#else
void
#endif
read_functions_mon_main(Datum main_arg)
{
    elog(WARNING, "Oh, shit! IT'S ALIVE!!!");
    proc_exit(0);
}


// Define variables
static void
read_functions_mon_load_params(void)
{
    DefineCustomIntVariable("repl_mon.interval",
                            "Time between writing timestamp (ms).",
                            "Default of 1s, max of 300s",
                            &interval,
                            1000,
                            1,
                            300000,
                            PGC_SIGHUP,
                            GUC_UNIT_MS,
                            NULL,
                            NULL,
                            NULL);
    DefineCustomStringVariable("repl_mon.table",
                               "Name of the table (in schema public).",
                               "Default is repl_mon",
                               &tablename,
                               "repl_mon",
                               PGC_SIGHUP,
                               0,
                               NULL,
                               NULL,
                               NULL);
}

/*
 * Entry point for worker loading
 */
void
_PG_init(void)
{
    BackgroundWorker worker;

    /* Add parameters */
    read_functions_mon_load_params();

    /* Worker parameter and registration */
    MemSet(&worker, 0, sizeof(BackgroundWorker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS |
        BGWORKER_BACKEND_DATABASE_CONNECTION;
    /* Start only on master hosts after finishing crash recovery */
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
#if PG_VERSION_NUM < 100000
    worker.bgw_main = read_functions_mon_main;
#endif
    snprintf(worker.bgw_name, BGW_MAXLEN, "%s", worker_name);
#if PG_VERSION_NUM >= 100000
    sprintf(worker.bgw_library_name, "read_functions");
    sprintf(worker.bgw_function_name, "read_functions_mon_main");
#endif
    /* Wait 10 seconds for restart after crash */
    worker.bgw_restart_time = 10;
    worker.bgw_main_arg = (Datum) 0;
#if PG_VERSION_NUM >= 90400
    /*
     * Notify PID is present since 9.4. If this is not initialized
     * a static background worker cannot start properly.
     */
    worker.bgw_notify_pid = 0;
#endif
    RegisterBackgroundWorker(&worker);
}