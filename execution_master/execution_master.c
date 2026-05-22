/*
 * execution_master.c — Implémentation de l'Execution Master
 */

#include "execution_master.h"
#include "em_parser.h"
#include "em_combiner.h"
#include "em_ipc.h"

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

/* ── État interne ─────────────────────────────────────────────────────────── */

static em_config_t           g_config;
static atomic_uint_fast64_t  g_job_counter = 0;
static int                   g_initialized  = 0;

/* ── Logging interne ──────────────────────────────────────────────────────── */

static void em_log(const char *level, const char *msg)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    printf("[%02d:%02d:%02d] [EXEC_MASTER] [%s] %s\n",
           t->tm_hour, t->tm_min, t->tm_sec, level, msg);
}

/* ── Configuration ────────────────────────────────────────────────────────── */

em_config_t em_default_config(void)
{
    em_config_t c;
    c.chunk_digits  = 3;
    c.job_timeout_s = 60;
    return c;
}

/* ── Utilitaires ──────────────────────────────────────────────────────────── */

uint64_t em_generate_job_id(void)
{
    /* Combine timestamp (secondes) et compteur pour garantir l'unicité
     * même en cas d'appels multiples dans la même seconde. */
    uint64_t ts  = (uint64_t)time(NULL);
    uint64_t seq = atomic_fetch_add(&g_job_counter, 1);
    return (ts << 20) | (seq & 0xFFFFF);
}

/* ── Cycle de vie ─────────────────────────────────────────────────────────── */

int em_init(const em_config_t *config)
{
    if (g_initialized) {
        em_log("WARN", "em_init() appelé deux fois, ignoré.");
        return 0;
    }

    g_config = config ? *config : em_default_config();

    if (em_ipc_init() != 0) {
        em_log("ERROR", "Échec de l'initialisation IPC.");
        return -1;
    }

    g_initialized = 1;
    em_log("INFO", "Execution Master initialisé.");
    return 0;
}

void em_shutdown(void)
{
    if (!g_initialized)
        return;

    em_ipc_destroy();
    g_initialized = 0;
    em_log("INFO", "Execution Master arrêté.");
}
