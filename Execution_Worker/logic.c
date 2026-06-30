#include "logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "parallax/parallax_param.h"

__asm__(
    ".global worker_entry\n"
    "worker_entry:\n"
    "mov (%rsp), %edi\n"      // argc -> rdi (1st arg)
    "lea 8(%rsp), %rsi\n"     // argv -> rsi (2nd arg)
    "call worker_main\n"
);

void worker_main(int argc, char **argv) {
    printf("[WorkerBinary] Coming from logic\n");

    if (argc < 2) {
        printf("[WorkerBinary] No function name provided\n");
        _exit(1);
    }

    char *fxn_name = argv[1];
    printf("[WorkerBinary] Function to execute: %s\n", fxn_name);

    ParallaxParam *params = NULL;
    int param_count = 0;

    if (argc > 2) {
        char *data_val = argv[2];
        printf("[WorkerBinary] Loading parameters from: %s\n", data_val);

        FILE *fp = fopen(data_val, "rb");
        if (fp) {
            // 1. Read param_count
            if (fread(&param_count, sizeof(int), 1, fp) == 1) {
                printf("[WorkerBinary] Deserializing %d parameters...\n", param_count);
                params = malloc(sizeof(ParallaxParam) * param_count);
                if (params) {
                    for (int i = 0; i < param_count; i++) {
                        // 2. Read each ParallaxParam metadata struct
                        if (fread(&params[i], sizeof(ParallaxParam), 1, fp) == 1) {
                            if (params[i].size > 0) {
                                // 3. Read data payload for this param
                                params[i].data = malloc(params[i].size);
                                if (params[i].data) {
                                    if (fread(params[i].data, 1, params[i].size, fp) != params[i].size) {
                                        fprintf(stderr, "[WorkerBinary] Error reading payload for param %d\n", i);
                                    }
                                }
                            } else {
                                params[i].data = NULL;
                            }
                        }
                    }
                }
            }
            fclose(fp);
            // Delete the temp file to keep scratch clean
            unlink(data_val);
        } else {
            perror("[WorkerBinary] fopen data_val");
        }
    }

    void *ret = NULL;
    fn fxn = matcher(fxn_name);
    if (fxn) {
        printf("[WorkerBinary] Executing function stub: %s\n", fxn_name);
        ret = fxn(params);
    } else {
        printf("[WorkerBinary] Function %s not found\n", fxn_name);
    }

    // Free deserialized parameters
    if (params) {
        for (int i = 0; i < param_count; i++) {
            if (params[i].data) {
                free(params[i].data);
            }
        }
        free(params);
    }

    if (argc > 3) {
        int fd = 0;
        sscanf(argv[3], "%d", &fd);
        if (fd > 0) {
            if (ret) {
                write(fd, ret, strlen((char*)ret) + 1);
                free(ret); // The returned string from sum_worker/sum is malloc'd, we free it after sending
            } else {
                write(fd, "NULL", 5);
            }
            close(fd);
        }
    }

    _exit(0);
}

// Dummy execute_fxn to satisfy the linker for dispatch stubs in parsed files
void execute_fxn(ParallaxParam *params, int param_count,
                 char *fxn_name, ParallaxExecutionCtx *ctx,
                 const char *prog_code, const char *prog_name) {
    (void)params; (void)param_count; (void)fxn_name; (void)ctx;
    (void)prog_code; (void)prog_name;
}