#ifndef MASTER_EXEC_H
#define MASTER_EXEC_H

#include <stddef.h>
#include "../../parallax/parallax_param.h"

/*
 * execute_fxn
 *
 * Distributes a function call across the cluster.
 *
 * params      – array of ParallaxParam descriptors built by the generated
 *               wrapper; each entry describes one original function argument
 *               along with its distribution strategy.
 * param_count – number of elements in params[].
 * fxn_name    – name of the original (un-generated) function, used to look it
 *               up via matcher() on the worker side.
 * node_count  – requested number of worker nodes (capped to what is available).
 * prog_code   – full source of the parsed program (embedded by the parser).
 * prog_name   – logical name of the program / source file.
 */
void execute_fxn(ParallaxParam *params, int param_count,
                 char *fxn_name, int node_count,
                 const char *prog_code, const char *prog_name);

void load_network_interface(char *iface, size_t max_len);
void get_local_ip(char *ip, size_t max_len, const char *iface_name);

#endif /* MASTER_EXEC_H */