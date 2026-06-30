/* === Parallax: embedded program source (auto-generated) === */
#include <string.h>
#include "parallax/parallax_param.h"
extern void execute_fxn(ParallaxParam *, int, char *, int, const char *, const char *);
void * sum_generated(void * data, size_t total_size);
void *sum_worker(void *);
static const char *__parallax_prog_code__ = "/* =============================================================================\n * test.c  —  Parser end-to-end test\n *\n * Write your annotated functions here normally.\n * Run the parser to produce test.c_parsed.c, which will contain:\n *   - __parallax_prog_code__ / __parallax_prog_name__ globals\n *   - sum_generated()  – dispatch stub (replaces call sites)\n *   - sum_worker()     – worker stub   (runs on the node, deserializes & calls sum)\n *   - matcher()        – maps \"sum_worker\" → sum_worker\n *\n * How to parse:\n *   ./Parser/build/mytool Parser/Tests/test.c -- -I./parallax\n *\n * ============================================================================= */\n\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n/* ─────────────────────────────────────────────────────────────────────────────\n * Annotated function: the parser will transform this.\n *   vcpus:2 → distribute across 2 nodes\n *   First param (void *) → PARALLAX_SCATTER   (data to chunk)\n *   Second param (size_t) → PARALLAX_BROADCAST (sent to all nodes unchanged)\n * ───────────────────────────────────────────────────────────────────────────── */\n__attribute__((annotate(\"vcpus:2\")))\nvoid *sum(void *data, size_t total_size) {\n    int   *arr   = (int *)data;\n    int    count = (int)(total_size / sizeof(int));\n    long long result = 0;\n\n    for (int i = 0; i < count; i++) {\n        result += arr[i];\n    }\n\n    printf(\"[sum] counted %d ints, partial sum = %lld\\n\", count, result);\n\n    char *out = malloc(64);\n    if (out) snprintf(out, 64, \"%lld\", result);\n    return out;\n}\n\n/* ─────────────────────────────────────────────────────────────────────────────\n * main — initialise data, call sum (will be rewritten to sum_generated after\n *         parsing).\n * ───────────────────────────────────────────────────────────────────────────── */\nint main(void) {\n    const int N = 100;\n    int *arr = malloc((size_t)N * sizeof(int));\n    if (!arr) { fprintf(stderr, \"malloc failed\\n\"); return 1; }\n\n    /* fill 1..100, expected sum = 5050 */\n    for (int i = 0; i < N; i++) arr[i] = i + 1;\n\n    printf(\"[main] Dispatching sum over %d integers (expected total: 5050)...\\n\", N);\n\n    /* After parsing this becomes: sum_generated(arr, N * sizeof(int))  */\n    sum_generated(arr, (size_t)N * sizeof(int));\n\n    free(arr);\n    printf(\"[main] Done.\\n\");\n    return 0;\n}\n\nvoid * sum_generated(void * data, size_t total_size) {\n    ParallaxParam __parallax_params[2];\n    __parallax_params[0].data = (void *)data;\n    __parallax_params[0].size = total_size;\n    __parallax_params[0].distribution = PARALLAX_SCATTER;\n    __parallax_params[0].index = 0;\n    strncpy(__parallax_params[0].type_name, \"void *\", 63);\n    __parallax_params[1].data = (void *)&total_size;\n    __parallax_params[1].size = sizeof(total_size);\n    __parallax_params[1].distribution = PARALLAX_SIZE_OF;\n    __parallax_params[1].index = 1;\n    strncpy(__parallax_params[1].type_name, \"size_t\", 63);\n    execute_fxn(__parallax_params, 2, \"sum_worker\", 2, __parallax_prog_code__, __parallax_prog_name__);\n    return (void *)NULL;\n}\n\nvoid *sum_worker(void *__arg) {\n    ParallaxParam *__p = (ParallaxParam *)__arg;\n    void * data = (void *)__p[0].data;\n    size_t total_size = *(size_t *)__p[1].data;\n    return (void *)sum(data, total_size);\n}\n\n\ntypedef void *(*fn)(void *);\n\nfn matcher(char *name) {\n    if (strcmp(name, \"sum_worker\") == 0) {\n        return (fn)sum_worker;\n    }\n    return NULL;\n}\n";
static const char *__parallax_prog_name__ = "test";

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
    sum_generated(arr, (size_t)N * sizeof(int));

    free(arr);
    printf("[main] Done.\n");
    return 0;
}

void * sum_generated(void * data, size_t total_size) {
    ParallaxParam __parallax_params[2];
    __parallax_params[0].data = (void *)data;
    __parallax_params[0].size = total_size;
    __parallax_params[0].distribution = PARALLAX_SCATTER;
    __parallax_params[0].index = 0;
    strncpy(__parallax_params[0].type_name, "void *", 63);
    __parallax_params[1].data = (void *)&total_size;
    __parallax_params[1].size = sizeof(total_size);
    __parallax_params[1].distribution = PARALLAX_SIZE_OF;
    __parallax_params[1].index = 1;
    strncpy(__parallax_params[1].type_name, "size_t", 63);
    execute_fxn(__parallax_params, 2, "sum_worker", 2, __parallax_prog_code__, __parallax_prog_name__);
    return (void *)NULL;
}

void *sum_worker(void *__arg) {
    ParallaxParam *__p = (ParallaxParam *)__arg;
    void * data = (void *)__p[0].data;
    size_t total_size = *(size_t *)__p[1].data;
    return (void *)sum(data, total_size);
}


typedef void *(*fn)(void *);

fn matcher(char *name) {
    if (strcmp(name, "sum_worker") == 0) {
        return (fn)sum_worker;
    }
    return NULL;
}
