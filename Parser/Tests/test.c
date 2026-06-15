/* =============================================================================
 * test.c  —  Parser end-to-end test
 *
 * Write your annotated functions here normally.
 * Run the parser to produce test.c_parsed.c, which will contain:
 *   - __parallax_prog_code__ / __parallax_prog_name__ globals
 *   - sum_generated()  – dispatch stub (replaces call sites)
 *   - sum_worker()     – worker stub   (runs on the node, deserializes & calls sum)
 *   - matcher()        – maps "sum_worker" → sum_worker
 *
 * How to parse:
 *   ./Parser/build/mytool Parser/Tests/test.c -- -I./parallax
 *
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Annotated function: the parser will transform this.
 *   vcpus:2 → distribute across 2 nodes
 *   First param (void *) → PARALLAX_SCATTER   (data to chunk)
 *   Second param (size_t) → PARALLAX_BROADCAST (sent to all nodes unchanged)
 * ───────────────────────────────────────────────────────────────────────────── */
__attribute__((annotate("vcpus:2")))
void *sum(void *data, size_t total_size) {
    int   *arr   = (int *)data;
    int    count = (int)(total_size / sizeof(int));
    long long result = 0;

    for (int i = 0; i < count; i++) {
        result += arr[i];
    }

    printf("[sum] counted %d ints, partial sum = %lld\n", count, result);

    char *out = malloc(64);
    if (out) snprintf(out, 64, "%lld", result);
    return out;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * main — initialise data, call sum (will be rewritten to sum_generated after
 *         parsing).
 * ───────────────────────────────────────────────────────────────────────────── */
int main(void) {
    const int N = 100;
    int *arr = malloc((size_t)N * sizeof(int));
    if (!arr) { fprintf(stderr, "malloc failed\n"); return 1; }

    /* fill 1..100, expected sum = 5050 */
    for (int i = 0; i < N; i++) arr[i] = i + 1;

    printf("[main] Dispatching sum over %d integers (expected total: 5050)...\n", N);

    /* After parsing this becomes: sum_generated(arr, N * sizeof(int))  */
    sum(arr, (size_t)N * sizeof(int));

    free(arr);
    printf("[main] Done.\n");
    return 0;
}
