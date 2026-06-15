#include "ms_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parallax/parallax_param.h"

// Defined in master_exec.c
extern void execute_fxn(ParallaxParam *params, int param_count,
                        char *fxn_name, ParallaxExecutionCtx *ctx,
                        const char *prog_code, const char *prog_name);

void *my_aggregator(void *a, void *b) {
  if (!a && !b)
    return NULL;
  long long val_a = a ? atoll((char *)a) : 0;
  long long val_b = b ? atoll((char *)b) : 0;
  char *res = malloc(64);
  sprintf(res, "%lld", val_a + val_b);
  return res;
}

void *matcher(char *name) {
  if (strcmp(name, "my_aggregator") == 0) {
    return (void *)my_aggregator;
  }
  return NULL;
}

int main() {
  printf("[SubmittedProg] Starting execution of map-reduce task...\n");

  // Create the message queue for receiving NODES responses from controller
  create_mq("NODES_TEST", 0);
  map_entry *node_mq = find_by_msg_type("NODES_TEST");
  if (!node_mq) {
    fprintf(stderr,
            "[SubmittedProg] Error: Failed to create NODES_TEST queue\n");
    return 1;
  }
  printf("[SubmittedProg] NODES_TEST mq resolved with ID %d\n",
         node_mq->queue_id);

  // Create sample dataset (array of integers)
  int payload[100];
  for (int i = 0; i < 100; i++) {
    payload[i] = i + 1; // Sum should be 5050
  }

  int expected_node_count = 2; // Testing with 2 nodes expected

  // Construct ParallaxParam parameter structures
  ParallaxParam params[2];

  // Param 0: payload (SCATTER)
  params[0].data = payload;
  params[0].size = sizeof(payload);
  params[0].distribution = PARALLAX_SCATTER;
  params[0].index = 0;
  strcpy(params[0].type_name, "int *");

  // Param 1: total_size (SIZE_OF companion)
  size_t total_size = sizeof(payload);
  params[1].data = &total_size;
  params[1].size = sizeof(total_size);
  params[1].distribution = PARALLAX_SIZE_OF;
  params[1].index = 1;
  strcpy(params[1].type_name, "size_t");

  // Worker code matches the deserialized parameter signature
  char *worker_code =
      "#include <stdio.h>\n"
      "#include <stdlib.h>\n"
      "#include <string.h>\n"
      "#include \"parallax/parallax_param.h\"\n"
      "\n"
      "typedef void *(*fn)(void *);\n"
      "\n"
      "void *sum_array(void *arg) {\n"
      "    char *filename = (char*)arg;\n"
      "    printf(\"[WorkerTask] Running sum_array on file: %s\\n\", "
      "filename);\n"
      "    FILE *f = fopen(filename, \"rb\");\n"
      "    if (!f) {\n"
      "        printf(\"[WorkerTask] Failed to open file: %s\\n\", filename);\n"
      "        return strdup(\"0\");\n"
      "    }\n"
      "    fseek(f, 0, SEEK_END);\n"
      "    long size = ftell(f);\n"
      "    fseek(f, 0, SEEK_SET);\n"
      "    int count = size / sizeof(int);\n"
      "    printf(\"[WorkerTask] File size: %ld bytes (%d integers)\\n\", "
      "size, count);\n"
      "    int *arr = malloc(size);\n"
      "    fread(arr, sizeof(int), count, f);\n"
      "    fclose(f);\n"
      "    long long sum = 0;\n"
      "    for(int i = 0; i < count; i++) {\n"
      "        sum += arr[i];\n"
      "    }\n"
      "    printf(\"[WorkerTask] Calculated sum: %lld\\n\", sum);\n"
      "    char *result = malloc(64);\n"
      "    sprintf(result, \"%lld\", sum);\n"
      "    return result;\n"
      "}\n"
      "\n"
      "fn matcher(char *name) {\n"
      "    if (strcmp(name, \"sum_array\") == 0) {\n"
      "        return sum_array;\n"
      "    }\n"
      "    return NULL;\n"
      "}\n"
      "\n"
      "int main() { return 0; }\n";

  // Build the execution context
  ParallaxExecutionCtx ctx;
  ctx.expected_node_count = expected_node_count;
  strcpy(ctx.aggregator_name, "my_aggregator");

  execute_fxn(params, 2, "sum_array", &ctx,
              worker_code, "test_prog4");

  printf("[SubmittedProg] execute_fxn completed successfully!\n");
  return 0;
}
