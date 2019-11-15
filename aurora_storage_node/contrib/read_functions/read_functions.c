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
#include "storage/bufmgr.h"
#include "utils/guc.h"
#include "utils/snapmgr.h"
/* Needed for getting hostname of the host */
#include "unistd.h"

#include "assert.h"

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


// Code starts here

#include "storage/smgr.h"
#include "storage/relfilenode.h"
#include "common/relpath.h"
#include "storage/block.h"
#include "miscadmin.h"
#include "storage/bufmgr.h"
#include "storage/buf.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

FILE* A_logs;

int A_connect(int shift) {
    char address[] = "127.0.0.1";
    int port = 16000 + shift;
    struct sockaddr_in sin; 
    
    sin.sin_family = AF_INET; 
    sin.sin_port = htons(port); 
    sin.sin_addr.s_addr = inet_addr(address);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(A_logs, "Error creating socket\n");
        fflush(A_logs);
        return -1;
    } 
    
    fprintf(A_logs, "Socket created\n");
    fflush(A_logs);
    
    while (connect(sockfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        sleep(1);
    }

    fprintf(A_logs, "Connection established\n");
    fflush(A_logs);

    return sockfd;
}

struct A_msg {
    struct RelFileNode rfn;
    ForkNumber forknum;
    BlockNumber blocknum;
};

const int A_pagesize = 8192;

PG_FUNCTION_INFO_V1(read_functions_mon_main);
Datum
read_functions_mon_main(PG_FUNCTION_ARGS)
{
    /* Register functions for SIGTERM/SIGHUP management */
    elog(LOG, "-----------------------------------------------------------\n");
    int shift = PG_GETARG_INT32(0);
    elog(LOG, "arg=%d\n", shift);
    int sockfd = A_connect(shift);
    if (sockfd == -1) {
        PG_RETURN_VOID();
    }

    int my_sln = 0;

    while (true) {
        struct A_msg msg;
        if (read(sockfd, &msg, sizeof(msg)) < sizeof(msg)) {
            elog(LOG, "Error reading from Primary node\n");
          //  fflush(A_logs);
            PG_RETURN_VOID();
        }

        elog(LOG, "Message read %d\n", my_sln);
       // fflush(A_logs);

        Relation rel = NULL;

        int try_num = 0;

        elog(LOG, "relNode = %d\n", msg.rfn.relNode);

        while (true) {
            elog(LOG, "Try Number %d\n", try_num++);

            LockRelationOid(msg.rfn.relNode, RowExclusiveLock);

            rel = RelationIdGetRelation(msg.rfn.relNode);

            UnlockRelationOid(msg.rfn.relNode, RowExclusiveLock);
            
            if (rel != NULL) {
                break;
            }
            
            sleep(1);
        }

        elog(LOG, "Got relation\n");
       // fflush(A_logs);
   //    Buffer buf = ReadBufferWithoutRelcache(msg.rfn, msg.forknum, msg.blocknum, RBM_NORMAL, NULL);
         Buffer buf = ReadBufferExtended(rel, msg.forknum, msg.blocknum, RBM_NORMAL, NULL);
        elog(LOG, "Got buffer\n");
       // fflush(A_logs);
        char* ptr = BufferGetPage(buf);
        
        elog(LOG, "Start send\n");
        if (write(sockfd, ptr, A_pagesize) < A_pagesize) {
            elog(LOG, "Error sending page\n");
           // fflush(A_logs);
            PG_RETURN_VOID();
        }

        elog(LOG, "Message written %d\n", my_sln++);
       // fflush(A_logs);
    }

    /* No problems, so clean exit */
    PG_RETURN_VOID();
}

// Code ends here

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
