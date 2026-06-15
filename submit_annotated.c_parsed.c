/* === Parallax: embedded program source (auto-generated) === */
#include <string.h>
#include "parallax/parallax_param.h"
extern void execute_fxn(ParallaxParam *, int, char *, ParallaxExecutionCtx *, const char *, const char *);
void * sum_array_generated(void * data, size_t total_size);
void *sum_array_worker(void *);
static const char *__parallax_prog_code__ = "#include <string.h>\n#include \"parallax/parallax_param.h\"\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n// Custom aggregator function\nvoid *my_aggregator(void *a, void *b) {\n  if (!a && !b)\n    return NULL;\n  long long val_a = a ? atoll((char *)a) : 0;\n  long long val_b = b ? atoll((char *)b) : 0;\n  char *res = malloc(64);\n  sprintf(res, \"%lld\", val_a + val_b);\n  return res;\n}\n\n// Annotated map function: 2 nodes, with reduce custom aggregator\n__attribute__((annotate(\"vcpus:2\")))\n__attribute__((annotate(\"reduce:my_aggregator\")))\nvoid *sum_array(void *data, size_t total_size) {\n    int *arr = (int *)data;\n    int count = (int)(total_size / sizeof(int));\n    long long sum = 0;\n    for (int i = 0; i < count; i++) {\n        sum += arr[i];\n    }\n    printf(\"[WorkerTask] Calculated sum: %lld\\n\", sum);\n    char *result = malloc(64);\n    sprintf(result, \"%lld\", sum);\n    return result;\n}\n\nint main() {\n    printf(\"[SubmittedProg] Starting execution of map-reduce task...\\n\");\n\n    // Create sample dataset (array of integers)\n    int payload[100];\n    for (int i = 0; i < 100; i++) {\n        payload[i] = i + 1; // Sum should be 5050\n    }\n\n    // Call the annotated function. \n    // The parser will rewrite this call to use sum_array_generated.\n    sum_array_generated(payload, sizeof(payload));\n\n    printf(\"[SubmittedProg] execute_fxn completed successfully!\\n\");\n    return 0;\n}\n\nvoid * sum_array_generated(void * data, size_t total_size) {\n    ParallaxParam __parallax_params[2];\n    __parallax_params[0].data = (void *)data;\n    __parallax_params[0].size = total_size;\n    __parallax_params[0].distribution = PARALLAX_SCATTER;\n    __parallax_params[0].index = 0;\n    strncpy(__parallax_params[0].type_name, \"void *\", 63);\n    __parallax_params[1].data = (void *)&total_size;\n    __parallax_params[1].size = sizeof(total_size);\n    __parallax_params[1].distribution = PARALLAX_SIZE_OF;\n    __parallax_params[1].index = 1;\n    strncpy(__parallax_params[1].type_name, \"size_t\", 63);\n    ParallaxExecutionCtx __parallax_ctx;\n    __parallax_ctx.expected_node_count = 2;\n    strncpy(__parallax_ctx.aggregator_name, \"my_aggregator\", 63);\n    __parallax_ctx.aggregator_name[63] = '\\0';\n    execute_fxn(__parallax_params, 2, \"sum_array_worker\", &__parallax_ctx, __parallax_prog_code__, __parallax_prog_name__);\n    return (void *)NULL;\n}\n\nvoid *sum_array_worker(void *__arg) {\n    ParallaxParam *__p = (ParallaxParam *)__arg;\n    void * data = (void *)__p[0].data;\n    size_t total_size = *(size_t *)__p[1].data;\n    return (void *)sum_array(data, total_size);\n}\n\n\ntypedef void *(*fn)(void *);\n\nfn matcher(char *name) {\n    if (strcmp(name, \"sum_array_worker\") == 0) {\n        return (fn)sum_array_worker;\n    }\n    if (strcmp(name, \"my_aggregator\") == 0) {\n        extern void * my_aggregator(void *, void *);\n        return (fn)my_aggregator;\n    }\n    return NULL;\n}\n";
static const char *__parallax_prog_name__ = "submit_annotated";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Custom aggregator function
void *my_aggregator(void *a, void *b) {
  if (!a && !b)
    return NULL;
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
    sum_array_generated(payload, sizeof(payload));

    printf("[SubmittedProg] execute_fxn completed successfully!\n");
    return 0;
}

void * sum_array_generated(void * data, size_t total_size) {
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
    ParallaxExecutionCtx __parallax_ctx;
    __parallax_ctx.expected_node_count = 2;
    strncpy(__parallax_ctx.aggregator_name, "my_aggregator", 63);
    __parallax_ctx.aggregator_name[63] = '\0';
    execute_fxn(__parallax_params, 2, "sum_array_worker", &__parallax_ctx, __parallax_prog_code__, __parallax_prog_name__);
    return (void *)NULL;
}

void *sum_array_worker(void *__arg) {
    ParallaxParam *__p = (ParallaxParam *)__arg;
    void * data = (void *)__p[0].data;
    size_t total_size = *(size_t *)__p[1].data;
    return (void *)sum_array(data, total_size);
}


typedef void *(*fn)(void *);

fn matcher(char *name) {
    if (strcmp(name, "sum_array_worker") == 0) {
        return (fn)sum_array_worker;
    }
    if (strcmp(name, "my_aggregator") == 0) {
        extern void * my_aggregator(void *, void *);
        return (fn)my_aggregator;
    }
    return NULL;
}
