#include "ms_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parallax/parallax_param.h"

// Defined in master_exec.c
extern void execute_fxn(ParallaxParam *params, int param_count,
                        char *fxn_name, ParallaxExecutionCtx *ctx,
                        const char *prog_code, const char *prog_name);

// Custom aggregator: multiplies partial products from workers
void *mul_add_aggregator(void *a, void *b) {
  if (!a && !b)
    return NULL;
  long long val_a = a ? atoll((char *)a) : 1;
  long long val_b = b ? atoll((char *)b) : 1;
  long long result = val_a * val_b;
  char *res = malloc(64);
  sprintf(res, "%lld", result);
  return res;
}

// Master-side matcher to resolve the custom aggregator
void *matcher(char *name) {
  if (strcmp(name, "mul_add_aggregator") == 0) {
    return (void *)mul_add_aggregator;
  }
  return NULL;
}

int main() {
  printf("[SubmittedProg] Starting execution of product map-reduce task...\n");

  // Create the message queue for receiving NODES responses from controller
  create_mq("NODES_TEST", 0);
  map_entry *node_mq = find_by_msg_type("NODES_TEST");
  if (!node_mq) {
    fprintf(stderr, "[SubmittedProg] Error: Failed to create NODES_TEST queue\n");
    return 1;
  }
  printf("[SubmittedProg] NODES_TEST mq resolved with ID %d\n", node_mq->queue_id);

  // Create sample dataset (array of 12 integers, all initialized to 2)
  // Total product should be 2^12 = 4096
  int payload[12];
  for (int i = 0; i < 12; i++) {
    payload[i] = 2;
  }

  int expected_node_count = 2; // Split across 2 nodes

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
      "void *product_array(void *arg) {\n"
      "    ParallaxParam *p = (ParallaxParam *)arg;\n"
      "    int *arr = (int *)p[0].data;\n"
      "    size_t total_size = *(size_t *)p[1].data;\n"
      "    int count = total_size / sizeof(int);\n"
      "    printf(\"[WorkerTask] Running product_array with %d integers\\n\", count);\n"
      "    long long prod = 1;\n"
      "    for(int i = 0; i < count; i++) {\n"
      "        prod *= arr[i];\n"
      "    }\n"
      "    printf(\"[WorkerTask] Calculated partial product: %lld\\n\", prod);\n"
      "    char *result = malloc(64);\n"
      "    sprintf(result, \"%lld\", prod);\n"
      "    return result;\n"
      "}\n"
      "\n"
      "fn matcher(char *name) {\n"
      "    if (strcmp(name, \"product_array\") == 0) {\n"
      "        return product_array;\n"
      "    }\n"
      "    return NULL;\n"
      "}\n"
      "\n"
      "int main() { return 0; }\n";

  // Build the execution context with custom aggregator
  ParallaxExecutionCtx ctx;
  ctx.expected_node_count = expected_node_count;
  strcpy(ctx.aggregator_name, "mul_add_aggregator");

  execute_fxn(params, 2, "product_array", &ctx,
              worker_code, "mul_add_prog");

  printf("[SubmittedProg] execute_fxn completed successfully!\n");
  return 0;
}
