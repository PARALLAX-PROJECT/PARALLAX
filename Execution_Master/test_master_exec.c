#include "ms_queue.h"
#include "network_agent.h"
#include "parallax/parallax_param.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Defined in master_exec.c
extern void execute_fxn(ParallaxParam *params, int param_count, char *fxn_name,
                        int node_count, const char *prog_code,
                        const char *prog_name);

// Global for master_exec.c to connect to Controller
char controller_ip[16] = "192.168.1.199";

int main() {
  printf("[TestExec] Starting network agent on port 9005...\n");
  static network_agent_config cfg = {9005, "master_out"};
  pthread_t net_thread;
  pthread_create(&net_thread, NULL, network_thread_run, &cfg);
  usleep(500000); // give it time to start

  // Create the message queue for receiving NODES responses from controller
  create_mq("NODES_TEST", 0);
  map_entry *node_mq = find_by_msg_type("NODES_TEST");
  printf("NODES mq created with id %d\n", node_mq->queue_id);

  // Create sample dataset (array of integers)
  int payload[100];
  for (int i = 0; i < 100; i++) {
    payload[i] = i + 1; // Sum should be 5050
  }

  int expected_node_count = 2;

  // Build the ParallaxParam parameters list
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
      "    ParallaxParam *p = (ParallaxParam *)arg;\n"
      "    int *arr = (int *)p[0].data;\n"
      "    size_t total_size = *(size_t *)p[1].data;\n"
      "    int count = total_size / sizeof(int);\n"
      "    printf(\"[WorkerTask] Running sum_array with %d integers\\n\", "
      "count);\n"
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

  execute_fxn(params, 2, "sum_array", expected_node_count, worker_code,
              "test_prog4");

  printf("\n[TestExec] execute_fxn completed successfully!\n");

  return 0;
}
