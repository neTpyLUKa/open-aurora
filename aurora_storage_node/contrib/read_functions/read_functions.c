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

int A_sockfd = -1;

int accept_connection() {
    int port = 16000;
    elog(LOG, "----------------------------------------------------------------------------------\n");
    elog(LOG, "Initializing socket..., id=%d\n", MyBackendId);

    if (A_sockfd == -1) {
        A_sockfd = socket(PF_INET, SOCK_STREAM, 0);
        if (A_sockfd < 0) {
            elog(PANIC, "Error creating socket\n");
        }

        elog(LOG, "Socket created\n");
    
        int val = 1;
        setsockopt(A_sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
        setsockopt(A_sockfd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
        struct sockaddr_in info;
        info.sin_addr.s_addr = INADDR_ANY;
        info.sin_family = AF_INET;
        info.sin_port = htons(port);

        if (bind(A_sockfd, (struct sockaddr*)&info, sizeof(info)) < 0) {
            elog(PANIC, "Error setting socket params");
            return -1;
        }

        elog(LOG, "Port %d binded", port);
    
        if (listen(A_sockfd, 5) < 0) {
            elog(PANIC, "Too many connections?");
        }
    }
    
    elog(LOG, "Accepting connection");
    struct sockaddr_in paddr;
    socklen_t len = sizeof(paddr); // todo fork
    int cl = accept(A_sockfd, (struct sockaddr*)&paddr, &len);
    if (cl < 0) {
        elog(PANIC, "Error accepting new connection");
        return -1;
    }
    elog(LOG, "Connection accepted\n");
    return cl;
}

struct A_msg {
    struct RelFileNode rfn;
    ForkNumber forknum;
    BlockNumber blocknum;
};

const int A_pagesize = 8192; // todo use GetPageSize instead

PG_FUNCTION_INFO_V1(read_functions_mon_main);
Datum
read_functions_mon_main(PG_FUNCTION_ARGS) // todo fix signature (now accepting int, need void)
{
    int id = 23411;
    while (true) {
        elog(LOG, "-----------------------------------------------------------\n");
        int sockfd = accept_connection();
        if (sockfd == -1) {
            elog(LOG, "Error accepting connection, retrying...");
            continue;
        }

        pid_t ch_pid = fork_process(); // fork wrapper
        if (ch_pid < 0) {
            elog(FATAL, "Error forking");
        } else if (ch_pid > 0) { // in parent
            ++id;
            continue;
        }
        InitProcessGlobals();
        InitPostmasterChild(); // independent latch
        
        int my_sln = 0;

        while (true) {
            struct A_msg msg;
            if (read(sockfd, &msg, sizeof(msg)) < sizeof(msg)) {
                elog(LOG, "Error reading from Primary node\n");
                close(sockfd);
                proc_exit(1);
            }

         /*   SMgrRelation smgr_rel = smgropen(msg.rfn, id);
            while (!smgrexists(smgr_rel, msg.forknum)) {
                pg_usleep(100000);
                smgr_rel = smgropen(msg.rfn, id);
                elog(LOG, "Not exists, %d, %d", msg.rfn.relNode, msg.forknum);
            }*/

            elog(LOG, "Message read %d\n", my_sln);

            Relation rel = NULL;

            int try_num = 0;

            elog(LOG, "relNode = %d, fork_num = %d\n", msg.rfn.relNode, msg.forknum);

            while (true) {
                elog(LOG, "Try Number %d\n", try_num++);

                LockRelationOid(msg.rfn.relNode, AccessShareLock);

                rel = RelationIdGetRelation(msg.rfn.relNode);

                
                if (rel != NULL) {
                    break;
                }
                UnlockRelationOid(msg.rfn.relNode, AccessShareLock);
            }

            elog(LOG, "Got relation\n");
          //  Buffer buf = ReadBufferWithoutRelcache(msg.rfn, msg.forknum, msg.blocknum, RBM_NORMAL, NULL);
            Buffer buf = ReadBufferExtended(rel, msg.forknum, msg.blocknum, RBM_NORMAL, NULL);
            //Buffer buf = ReadBuffer(rel, msg.blocknum); // todo use ReadBufferExtended, bypass forkNum
            
            LockBuffer(buf, BUFFER_LOCK_SHARE);
            elog(LOG, "Got buffer\n");
            char* ptr = BufferGetPage(buf);
            
            elog(LOG, "Start send\n");
            if (write(sockfd, ptr, A_pagesize) < A_pagesize) {
                elog(LOG, "Error sending page\n");
                close(sockfd);
                UnlockReleaseBuffer(buf);
                relation_close(rel, AccessShareLock);
                proc_exit(1);
            }

            elog(LOG, "Message written %d\n", my_sln++);
            UnlockReleaseBuffer(buf);
            relation_close(rel, AccessShareLock);
        }
    }
    elog(PANIC, "How I get here?");

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
