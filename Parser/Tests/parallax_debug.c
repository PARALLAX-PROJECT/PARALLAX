/* =============================================================================
 * parallax_debug.c
 *
 * A drop-in local implementation of execute_fxn that does NOT distribute
 * anything.  Instead it prints the full metadata for every ParallaxParam
 * that the dispatch stub built.
 *
 * Compile the parsed file with this to test parser output without any
 * distributed infrastructure:
 *
 *   gcc -o test_parsed \
 *       Parser/Tests/test.c_parsed.c \
 *       Parser/Tests/parallax_debug.c \
 *       -I. -Wall
 *
 *   ./test_parsed
 * ============================================================================= */

#include <stdio.h>
#include <stddef.h>
#include "parallax/parallax_param.h"

/* Map distribution enum to a human-readable string */
static const char *dist_name(ParallaxDistribution d) {
    switch (d) {
        case PARALLAX_SCATTER:   return "SCATTER   (chunked across nodes)";
        case PARALLAX_BROADCAST: return "BROADCAST (sent to all nodes)";
        case PARALLAX_REDUCE:    return "REDUCE    (output collected back)";
        case PARALLAX_SIZE_OF:   return "SIZE_OF   (byte-size of prev ptr)";
        default:                 return "UNKNOWN";
    }
}

/* Debug-only implementation of execute_fxn */
void execute_fxn(ParallaxParam *params, int param_count,
                 char *fxn_name, int node_count,
                 const char *prog_code, const char *prog_name) {

    (void)prog_code;   /* suppress unused-variable warning for the big string */

    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║         execute_fxn  [DEBUG / LOCAL MODE]            ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("  worker function : %s\n", fxn_name);
    printf("  program name    : %s\n", prog_name);
    printf("  requested nodes : %d\n", node_count);
    printf("  param count     : %d\n", param_count);
    printf("\n");

    for (int i = 0; i < param_count; i++) {
        ParallaxParam *p = &params[i];
        printf("  ┌─ param[%d] ─────────────────────────────────────────\n", i);
        printf("  │  index        : %d\n",    p->index);
        printf("  │  type         : %s\n",    p->type_name);
        printf("  │  distribution : %s\n",    dist_name(p->distribution));
        printf("  │  size         : %zu bytes\n", p->size);
        printf("  │  data ptr     : %p\n",    p->data);

        /* If this is a size-companion param, show the numeric value it holds */
        if (p->distribution == PARALLAX_SIZE_OF && p->data) {
            printf("  │  value        : %zu  (size companion)\n",
                   *(size_t *)p->data);
        }

        /* If this is a SCATTER param and we know the size, peek at first bytes */
        if (p->distribution == PARALLAX_SCATTER && p->data && p->size > 0) {
            size_t preview = p->size < 5 * sizeof(int) ? p->size / sizeof(int)
                                                        : 5;
            int *arr = (int *)p->data;
            printf("  │  data preview : ");
            for (size_t j = 0; j < preview; j++) {
                printf("%d ", arr[j]);
            }
            printf("...\n");
        }

        printf("  └────────────────────────────────────────────────────\n");
    }

    printf("\n[DEBUG] Skipping actual distribution — no cluster required.\n\n");
}
