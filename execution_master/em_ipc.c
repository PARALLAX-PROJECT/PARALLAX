/*
 * em_ipc.c — Implémentation IPC de l'Execution Master
 */

#include "em_ipc.h"

#include <sys/msg.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* ── État interne ─────────────────────────────────────────────────────────── */

static int g_submit_qid = -1;   /* file EM → Orchestrateur */
static int g_result_qid = -1;   /* file Orchestrateur → EM */

/* ── Cycle de vie ─────────────────────────────────────────────────────────── */

int em_ipc_init(void)
{
    g_submit_qid = msgget(EM_IPC_KEY_SUBMIT, IPC_CREAT | 0666);
    if (g_submit_qid == -1) {
        perror("[EM_IPC] msgget(SUBMIT) failed");
        return -1;
    }

    g_result_qid = msgget(EM_IPC_KEY_RESULT, IPC_CREAT | 0666);
    if (g_result_qid == -1) {
        perror("[EM_IPC] msgget(RESULT) failed");
        return -1;
    }

    printf("[EM_IPC] Files ouvertes : submit_qid=%d result_qid=%d\n",
           g_submit_qid, g_result_qid);
    return 0;
}

void em_ipc_destroy(void)
{
    /* L'EM ne supprime pas les files — l'Orchestrateur en est propriétaire. */
    g_submit_qid = -1;
    g_result_qid = -1;
    printf("[EM_IPC] Handles fermés.\n");
}
