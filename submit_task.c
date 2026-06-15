#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Parallax distributed task
//  Annotate with vcpus:<n> to tell the parser how many nodes to target.
// ─────────────────────────────────────────────────────────────────────────────

__attribute__((annotate("vcpus:2")))
void *sum(void *data, size_t total_size) {
    int *arr   = (int *)data;
    int  count = (int)(total_size / sizeof(int));

    long long result = 0;
    for (int i = 0; i < count; i++) {
        result += arr[i];
    }

    char *out = malloc(64);
    if (out) snprintf(out, 64, "%lld", result);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point – initialise the dataset and hand it off to the distributed
//  sum (after the parser rewrites this call to sum_generated).
// ─────────────────────────────────────────────────────────────────────────────
int main(void) {
    const int N = 100;
    int *arr = malloc(N * sizeof(int));
    if (!arr) { fprintf(stderr, "malloc failed\n"); return 1; }

    for (int i = 0; i < N; i++) arr[i] = i + 1;   // 1..100, sum = 5050

    printf("[SubmittedProg] Launching distributed sum over %d integers...\n", N);

    sum(arr, (size_t)N * sizeof(int));

    free(arr);
    printf("[SubmittedProg] Done.\n");
    return 0;
}
