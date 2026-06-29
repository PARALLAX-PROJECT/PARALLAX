#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Custom aggregator function
void *my_aggregator(void *a, void *b) {
  if (!a && !b)
    return NULL
  long long val_a = a ? atoll((char *)a) : 0;
  long long val_b = b ? atoll((char *)b) : 0;
  char *res = malloc(64);
  sprintf(res, "%lld", val_a + val_b);
  return res;
}

// Annotated map function: 2 nodes, with reduce custom aggregator
__attribute__((annotate("vcpus:2")))
__attribute__((annotate("reduce:my_aggregator")))
void *sum_array(void *data, size_t total_size) {
    int *arr = (int *)data;
    int count = (int)(total_size / sizeof(int));
    long long sum = 0;
    for (int i = 0; i < count; i++) {
        sum += arr[i];
    }
    printf("[WorkerTask] Calculated sum: %lld\n", sum);
    char *result = malloc(64);
    sprintf(result, "%lld", sum);
    return result;
}

int main() {
    printf("[SubmittedProg] Starting execution of map-reduce task...\n");

    // Create sample dataset (array of integers)
    int payload[100];
    for (int i = 0; i < 100; i++) {
        payload[i] = i + 1; // Sum should be 5050
    }

    // Call the annotated function. 
    // The parser will rewrite this call to use sum_array_generated.
    sum_array(payload, sizeof(payload));

    printf("[SubmittedProg] execute_fxn completed successfully!\n");
    return 0;
}
