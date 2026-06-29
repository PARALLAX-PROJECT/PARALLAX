#include "reception.h"
#include "net_utils.h"
#include "network_agent.h"
#include "socket.h"
#include "ms_queue.h"
#include "master_thread.h"
#include "../parallax/state_message.h"
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Forward declaration
void receptionist_handle_master_ip_update(MasterIPResponse* response);
static ReceptionistState g_receptionist;

#ifndef ROLE_RECEPTIONIST
#define ROLE_RECEPTIONIST 4
#endif

static char pending_code[7500] = {0};
static int pending_code_len = 0;
static int has_pending_code = 0;
static pthread_mutex_t pending_code_mutex = PTHREAD_MUTEX_INITIALIZER;

#define HTTP_NODES_TYPE    "HTTP_NODES"
#define HTTP_NODELOG_TYPE  "HTTP_NODELOG"
static map_entry *http_nodes_mq_entry   = NULL;
static map_entry *http_nodelog_mq_entry = NULL;
static pthread_mutex_t http_nodes_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t http_nodelog_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Extract __parallax_prog_name__ from the submitted code, e.g. "find_max" → "find_max.c" */
static void extract_prog_name(const char *code, char *out, size_t out_size) {
    const char *marker = "__parallax_prog_name__ = \"";
    const char *p = strstr(code, marker);
    if (p) {
        p += strlen(marker);
        const char *end = strchr(p, '"');
        if (end) {
            size_t len = (size_t)(end - p);
            if (len >= out_size - 3) len = out_size - 4;
            strncpy(out, p, len);
            out[len] = '\0';
            strncat(out, ".c", out_size - strlen(out) - 1);
            return;
        }
    }
    strncpy(out, "submitted_prog.c", out_size - 1);
    out[out_size - 1] = '\0';
}

static void forward_code_to_master(void) {
    if (!has_pending_code) return;
    if (g_receptionist.master_port == 0 || strcmp(g_receptionist.master_ip, "NONE") == 0) return;

    char prog_name[64];
    extract_prog_name(pending_code, prog_name, sizeof(prog_name));

    printf("[RECEPTIONIST] Forwarding '%s' to master at %s:%d\n",
           prog_name, g_receptionist.master_ip, g_receptionist.master_port);

    size_t pkt_size = sizeof(message_t) + sizeof(program_message_t);
    message_t *pkt = (message_t *)malloc(pkt_size);
    if (pkt) {
        pkt->mq_type = 1;
        strcpy(pkt->type, "PROG");
        pkt->size = sizeof(program_message_t);

        program_message_t *prog = (program_message_t *)pkt->data;
        strncpy(prog->program_name, prog_name, sizeof(prog->program_name) - 1);
        prog->code_size = pending_code_len;
        memset(prog->code, 0, sizeof(prog->code));
        memcpy(prog->code, pending_code, pending_code_len);

        send_msg(g_receptionist.master_ip, g_receptionist.master_port, "receptionist_out", pkt);
        free(pkt);

        has_pending_code = 0; // Reset after sending
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════════════

static atomic_int receptionist_running = 0;
static char receptionist_uuid[37] = {0};

// ═══════════════════════════════════════════════════════════════════════════
//  UUID GENERATION & LOADING
// ═══════════════════════════════════════════════════════════════════════════

static void generate_uuid(char *uuid) {
    unsigned char bytes[16];
    FILE *f = fopen("/dev/urandom", "r");
    if (f) {
        fread(bytes, 1, 16, f);
        fclose(f);
    } else {
        srand((unsigned int)time(NULL));
        for (int i = 0; i < 16; i++)
            bytes[i] = rand() % 256;
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant RFC 4122
    sprintf(uuid,
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
            bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
            bytes[14], bytes[15]);
}

const char *get_agent_uuid(void) {
    if (receptionist_uuid[0] == '\0') {
        FILE *f = fopen(".receptionist_uuid", "r");
        if (f) {
            if (fscanf(f, "%36s", receptionist_uuid) != 1) {
                receptionist_uuid[0] = '\0';
            }
            fclose(f);
        }
        if (receptionist_uuid[0] == '\0') {
            generate_uuid(receptionist_uuid);
            f = fopen(".receptionist_uuid", "w");
            if (f) {
                fprintf(f, "%s", receptionist_uuid);
                fclose(f);
            }
        }
    }
    return receptionist_uuid;
}

// ═══════════════════════════════════════════════════════════════════════════
//  THREAD 1: RECEPTIONIST QUERY & RESPONSE LISTENER THREAD
//  Periodically queries the controller and polls for responses in a single thread
// ═══════════════════════════════════════════════════════════════════════════

void* receptionist_thread(void* arg) {
    (void)arg;
    
    create_mq(PROVIDE_MASTER_IP_TYPE, NETWORK_AGENT_MAX_DATA);
    map_entry *entry = find_by_msg_type(PROVIDE_MASTER_IP_TYPE);
    if (!entry) {
        printf("[RECEPTIONIST] ERROR: Failed to find PROVIDE_MASTER_IP queue\n");
        return NULL;
    }
    
    while (atomic_load(&receptionist_running)) {
        // Create request message

        
        MasterIPRequest req;
        memset(&req, 0, sizeof(MasterIPRequest));
        strncpy(req.receptionist_uuid, get_agent_uuid(), sizeof(req.receptionist_uuid) - 1);
        
        // Get our IP for controller to reply to
        char iface[16] = {0};
        load_network_interface(iface, sizeof(iface));
        get_local_ip(req.receptionist_ip, sizeof(req.receptionist_ip), iface);
        req.receptionist_port = 9008;
        
        // Send request to controller
        message_t *pkt = (message_t *)malloc(sizeof(message_t) + sizeof(MasterIPRequest));
        if (pkt) {
            pkt->mq_type = 1;
            strcpy(pkt->type, REQUEST_MASTER_IP_TYPE);
            pkt->size = sizeof(MasterIPRequest);
            memcpy(pkt->data, &req, sizeof(MasterIPRequest));
            
            send_msg(g_receptionist.controller_ip, 9000, "receptionist_out", pkt);
            free(pkt);
        }
        
        // Poll for response for up to 5 seconds (50 * 100ms)
        int polled = 0;
        queued_message qmsg;
        while (polled < 50 && atomic_load(&receptionist_running)) {
            ssize_t ret = msgrcv(entry->queue_id, &qmsg, sizeof(qmsg) - sizeof(long),
                                 1L, IPC_NOWAIT);
            if (ret >= 0) {
                MasterIPResponse *response = (MasterIPResponse *)qmsg.data;
                receptionist_handle_master_ip_update(response);
                // Sleep for the remainder of the 5 seconds interval
                int remaining_us = (50 - polled) * 100000;
                if (remaining_us > 0) {
                    usleep(remaining_us);
                }
                break;
            }
            usleep(100000); // 100ms
            polled++;
        }
    }
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
//  HTTP HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static void http_send_response(int fd, int code, const char *ctype,
                                const char *body, size_t blen) {
    const char *reason = (code == 200) ? "OK"
                       : (code == 404) ? "Not Found" : "Internal Server Error";
    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        code, reason, ctype, blen);
    write(fd, hdr, hlen);
    if (body && blen > 0)
        write(fd, body, blen);
}

static void handle_get_nodes(int client_fd) {
    if (!http_nodes_mq_entry || strcmp(g_receptionist.controller_ip, "NONE") == 0
            || g_receptionist.controller_ip[0] == '\0') {
        const char *err = "{\"error\":\"not connected to controller\"}";
        http_send_response(client_fd, 200, "application/json", err, strlen(err));
        return;
    }

    pthread_mutex_lock(&http_nodes_mutex);

    message_t *pkt = calloc(1, sizeof(message_t));
    if (pkt) {
        pkt->mq_type = 1;
        strcpy(pkt->type, "ALL_NODES");
        strcpy(pkt->recv_type, HTTP_NODES_TYPE);
        pkt->size = 0;
        /* Must set sender_ip — if left empty the network agent defaults to "127.0.0.1"
           and the controller would reply to its own localhost instead of us. */
        char iface[16] = {0};
        char my_ip[16] = {0};
        load_network_interface(iface, sizeof(iface));
        get_local_ip(my_ip, sizeof(my_ip), iface);
        strncpy(pkt->sender_ip, my_ip, sizeof(pkt->sender_ip) - 1);
        pkt->sender_port = 9008;
        send_msg(g_receptionist.controller_ip, 9000, "receptionist_out", pkt);
        free(pkt);
    }

    queued_message qmsg;
    memset(&qmsg, 0, sizeof(qmsg));
    int got = 0;
    for (int i = 0; i < 50 && !got; i++) {
        ssize_t ret = msgrcv(http_nodes_mq_entry->queue_id, &qmsg,
                             sizeof(qmsg) - sizeof(long), 1L, IPC_NOWAIT);
        if (ret >= 0) got = 1;
        else usleep(100000);
    }

    pthread_mutex_unlock(&http_nodes_mutex);

    if (!got) {
        const char *err = "{\"error\":\"timeout waiting for controller\"}";
        http_send_response(client_fd, 200, "application/json", err, strlen(err));
        return;
    }

    MachineMetrics *metrics = (MachineMetrics *)qmsg.data;
    int max_nodes = (int)(NETWORK_AGENT_MAX_DATA / sizeof(MachineMetrics));

    char *json = malloc(32768);
    if (!json) {
        http_send_response(client_fd, 500, "application/json", "{\"error\":\"oom\"}", 14);
        return;
    }
    int pos = 0;
    pos += snprintf(json + pos, 32768 - pos, "[");
    int first = 1;
    for (int i = 0; i < max_nodes && pos < 31000; i++) {
        if (strlen(metrics[i].uuid) == 0) break;
        const char *rname;
        switch (metrics[i].role) {
            case 1:  rname = "worker";       break;
            case 2:  rname = "controller";   break;
            case 3:  rname = "master";       break;
            case 4:  rname = "receptionist"; break;
            default: rname = "unknown";
        }
        /* active_connections is repurposed to carry NodeStatus (0=active,1=suspect,2=failed) */
        const char *sname;
        switch (metrics[i].active_connections) {
            case 0:  sname = "active";       break;
            case 1:  sname = "suspect";      break;
            case 2:  sname = "failed";       break;
            case 3:  sname = "overloaded";   break;
            default: sname = "maintenance";
        }
        pos += snprintf(json + pos, 32768 - pos,
            "%s{\"uuid\":\"%s\",\"ip\":\"%s\",\"port\":%d,"
            "\"role\":\"%s\",\"status\":\"%s\","
            "\"cpu\":%.2f,\"ram\":%.2f,"
            "\"score\":%.2f,\"cores\":%d,\"model\":\"%s\"}",
            first ? "" : ",",
            metrics[i].uuid, metrics[i].ip, metrics[i].port,
            rname, sname, metrics[i].cpu_usage, metrics[i].mem_usage,
            metrics[i].score, metrics[i].cpu_cores, metrics[i].cpu_model);
        first = 0;
    }
    pos += snprintf(json + pos, 32768 - pos, "]");
    http_send_response(client_fd, 200, "application/json", json, pos);
    free(json);
}

static void handle_get_logs(int client_fd, const char *path) {
    const char *sub = path + 5; /* skip "/logs" */

    if (*sub == '\0' || strcmp(sub, "/") == 0) {
        DIR *d = opendir("logs");
        if (!d) {
            http_send_response(client_fd, 200, "application/json", "[]", 2);
            return;
        }
        char *json = malloc(8192);
        if (!json) { closedir(d); return; }
        int pos = 0;
        pos += snprintf(json + pos, 8192 - pos, "[");
        int first = 1;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && pos < 7500) {
            if (ent->d_name[0] == '.') continue;
            char fpath[512];
            snprintf(fpath, sizeof(fpath), "logs/%s", ent->d_name);
            struct stat st;
            long fsize = 0;
            if (stat(fpath, &st) == 0) fsize = st.st_size;
            pos += snprintf(json + pos, 8192 - pos,
                "%s{\"name\":\"%s\",\"size\":%ld}",
                first ? "" : ",", ent->d_name, fsize);
            first = 0;
        }
        closedir(d);
        pos += snprintf(json + pos, 8192 - pos, "]");
        http_send_response(client_fd, 200, "application/json", json, pos);
        free(json);
    } else {
        if (sub[0] == '/') sub++;
        /* Reject path traversal */
        if (strstr(sub, "..") || strchr(sub, '/')) {
            http_send_response(client_fd, 404, "text/plain", "Not found", 9);
            return;
        }
        char fpath[512];
        snprintf(fpath, sizeof(fpath), "logs/%s", sub);
        FILE *f = fopen(fpath, "r");
        if (!f) {
            http_send_response(client_fd, 404, "text/plain", "Not found", 9);
            return;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        rewind(f);
        char *content = malloc(fsize + 1);
        if (!content) { fclose(f); return; }
        size_t nr = fread(content, 1, fsize, f);
        fclose(f);
        content[nr] = '\0';
        http_send_response(client_fd, 200, "text/plain", content, nr);
        free(content);
    }
}

static void handle_get_node_logs(int client_fd, const char *path) {
    /* path is "/node-logs/<uuid>" */
    const char *sub = path + 10; /* skip "/node-logs" */
    if (*sub == '/') sub++;

    if (*sub == '\0') {
        const char *err = "{\"error\":\"provide a uuid: /node-logs/<uuid>\"}";
        http_send_response(client_fd, 200, "application/json", err, strlen(err));
        return;
    }

    if (!http_nodelog_mq_entry || g_receptionist.controller_ip[0] == '\0') {
        const char *err = "{\"error\":\"not connected to controller\"}";
        http_send_response(client_fd, 200, "application/json", err, strlen(err));
        return;
    }

    pthread_mutex_lock(&http_nodelog_mutex);

    size_t pkt_size = sizeof(message_t) + sizeof(get_node_log_req_t);
    message_t *pkt = calloc(1, pkt_size);
    if (pkt) {
        pkt->mq_type = 1;
        strcpy(pkt->type, GET_NODE_LOG_TYPE);
        strcpy(pkt->recv_type, HTTP_NODELOG_TYPE);
        pkt->size = sizeof(get_node_log_req_t);
        char iface[16] = {0}, my_ip[16] = {0};
        load_network_interface(iface, sizeof(iface));
        get_local_ip(my_ip, sizeof(my_ip), iface);
        strncpy(pkt->sender_ip, my_ip, sizeof(pkt->sender_ip) - 1);
        pkt->sender_port = 9008;
        get_node_log_req_t *req = (get_node_log_req_t *)pkt->data;
        strncpy(req->node_uuid, sub, sizeof(req->node_uuid) - 1);
        send_msg(g_receptionist.controller_ip, 9000, "receptionist_out", pkt);
        free(pkt);
    }

    queued_message qmsg;
    memset(&qmsg, 0, sizeof(qmsg));
    int got = 0;
    for (int i = 0; i < 50 && !got; i++) {
        ssize_t ret = msgrcv(http_nodelog_mq_entry->queue_id, &qmsg,
                             sizeof(qmsg) - sizeof(long), 1L, IPC_NOWAIT);
        if (ret >= 0) got = 1;
        else usleep(100000);
    }

    pthread_mutex_unlock(&http_nodelog_mutex);

    if (!got) {
        const char *err = "timeout waiting for controller";
        http_send_response(client_fd, 200, "text/plain", err, strlen(err));
        return;
    }

    node_log_t *log = (node_log_t *)qmsg.data;
    if (log->log_size == 0) {
        const char *err = "no log available for this node yet";
        http_send_response(client_fd, 404, "text/plain", err, strlen(err));
        return;
    }
    http_send_response(client_fd, 200, "text/plain", log->log_content, log->log_size);
}

// ═══════════════════════════════════════════════════════════════════════════
//  THREAD 2: CODE SUBMISSION PROCESSOR THREAD
//  Listens as a basic HTTP server on port 9010 and prints the body
// ═══════════════════════════════════════════════════════════════════════════

void* code_submission_listener_thread(void* arg) {
    (void)arg;

    /* Set up reply queues before accepting connections */
    create_mq(HTTP_NODES_TYPE, NETWORK_AGENT_MAX_DATA);
    http_nodes_mq_entry = find_by_msg_type(HTTP_NODES_TYPE);
    if (!http_nodes_mq_entry)
        printf("[RECEPTIONIST] WARNING: Could not create HTTP_NODES queue\n");

    create_mq(HTTP_NODELOG_TYPE, NETWORK_AGENT_MAX_DATA);
    http_nodelog_mq_entry = find_by_msg_type(HTTP_NODELOG_TYPE);
    if (!http_nodelog_mq_entry)
        printf("[RECEPTIONIST] WARNING: Could not create HTTP_NODELOG queue\n");

    connection *listener = create_listener("0.0.0.0", RECEPTIONIST_LISTENING_PORT, 5);
    if (!listener) {
        printf("[RECEPTIONIST] ERROR: Failed to start HTTP server on port %d\n", RECEPTIONIST_LISTENING_PORT);
        return NULL;
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(listener->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    printf("[RECEPTIONIST] HTTP server listening on port %d\n", RECEPTIONIST_LISTENING_PORT);

    while (atomic_load(&receptionist_running)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listener->sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            usleep(10000);
            continue;
        }

        char buffer[8192];
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            char method[16] = {0}, path[256] = {0};
            sscanf(buffer, "%15s %255s", method, path);

            if (strcmp(method, "GET") == 0 && strcmp(path, "/nodes") == 0) {
                handle_get_nodes(client_fd);
            } else if (strcmp(method, "GET") == 0 && strncmp(path, "/node-logs", 10) == 0) {
                handle_get_node_logs(client_fd, path);
            } else if (strcmp(method, "GET") == 0 && strncmp(path, "/logs", 5) == 0) {
                handle_get_logs(client_fd, path);
            } else {
                /* Code submission: extract body after \r\n\r\n */
                char *body_start = strstr(buffer, "\r\n\r\n");
                if (body_start) {
                    body_start += 4;
                    pthread_mutex_lock(&pending_code_mutex);
                    strncpy(pending_code, body_start, sizeof(pending_code) - 1);
                    pending_code[sizeof(pending_code) - 1] = '\0';
                    pending_code_len = strlen(pending_code);
                    has_pending_code = 1;
                    forward_code_to_master();
                    pthread_mutex_unlock(&pending_code_mutex);
                }
                const char *resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 2\r\n"
                    "Connection: close\r\n\r\n"
                    "OK";
                write(client_fd, resp, strlen(resp));
            }
        }
        close(client_fd);
    }

    close(listener->sockfd);
    free(listener);
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CONTROLLER INTERFACE
// ═══════════════════════════════════════════════════════════════════════════

void receptionist_handle_master_ip_update(MasterIPResponse* response) {
    if (!response) return;
    
    pthread_mutex_lock(&pending_code_mutex);
    if (strcmp(g_receptionist.master_ip, response->master_ip) != 0 || g_receptionist.master_port != response->master_port) {
        printf("[RECEPTIONIST] Master IP updated to %s:%d\n", response->master_ip, response->master_port);
        strncpy(g_receptionist.master_ip, response->master_ip, sizeof(g_receptionist.master_ip) - 1);
        g_receptionist.master_port = response->master_port;
        
        forward_code_to_master();
    }
    pthread_mutex_unlock(&pending_code_mutex);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISCOVERY PHASE
// ═══════════════════════════════════════════════════════════════════════════

static int discover_controller(void) {
    MachineMetrics msg;
    memset(&msg, 0, sizeof(MachineMetrics));

    // Ensure UUID is set
    strncpy(msg.uuid, get_agent_uuid(), sizeof(msg.uuid) - 1);
    msg.type = MSG_HELLO;
    msg.timestamp = time(NULL);
    msg.role = ROLE_RECEPTIONIST;

    // Set IP and port dynamically
    load_network_interface(msg.network_iface, sizeof(msg.network_iface));
    get_local_ip(msg.ip, sizeof(msg.ip), msg.network_iface);
    msg.port = 9008; // Receptionist listening port

    printf("[RECEPTIONIST] Waiting for controller's IP reply on port 9009 via message queue...\n");
    map_entry *mq = find_by_msg_type(HELLO_TYPE);
    if (!mq) {
        if (create_mq(HELLO_TYPE, NETWORK_AGENT_MAX_DATA) != NULL) {
            mq = find_by_msg_type(HELLO_TYPE);
        }
    }

    if (!mq) {
        printf("[RECEPTIONIST] Failed to find or create HELLO_TYPE queue!\n");
        return 0;
    }

    int response_received = 0;
    queued_message item;

    while (!response_received) {
        message_t *pkt =
            (message_t *)malloc(sizeof(message_t) + sizeof(MachineMetrics));
        if (pkt) {
            strcpy(pkt->type, HELLO_TYPE);
            pkt->size = sizeof(MachineMetrics);
            memcpy(pkt->data, &msg, sizeof(MachineMetrics));

            // Broadcast on port 9001, since that's what controller listens to for HELLO broadcast
            send_broadcast(9001, pkt);
            free(pkt);
            printf("[RECEPTIONIST] HELLO sent: uuid=%s\n", msg.uuid);
        }

        // Wait up to 5 seconds for a reply
        for (int i = 0; i < 50; i++) {
            ssize_t received =
                msgrcv(mq->queue_id, &item, sizeof(item) - sizeof(long),
                       NETWORK_AGENT_MTYPE, IPC_NOWAIT);
            if (received > 0) {
                if (strncmp(item.data, "IP:", 3) == 0) {
                    printf("\n--- [RECEPTIONIST] Controller IP Received ---\n");
                    printf("Message Type: %s\n", item.type);
                    printf("Controller IP: %s\n", item.data + 3);
                    strncpy(g_receptionist.controller_ip, item.data + 3, 15);
                    g_receptionist.controller_ip[15] = '\0';
                    response_received = 1;
                    break;
                }
            }
            usleep(100000); // 100ms
        }
    }
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUBLIC API
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
//  THREAD 3: EXECUTION LOG RECEIVER
//  Reads PROG_LOG messages from the IPC queue and writes each to a file.
// ═══════════════════════════════════════════════════════════════════════════

void *log_receiver_thread(void *arg) {
    (void)arg;

    create_mq(PROG_LOG_TYPE, 0);
    map_entry *log_entry = find_by_msg_type(PROG_LOG_TYPE);
    if (!log_entry) {
        printf("[RECEPTIONIST] ERROR: Could not create PROG_LOG queue\n");
        return NULL;
    }

    mkdir("logs", 0777);

    queued_message qmsg;
    while (atomic_load(&receptionist_running)) {
        ssize_t ret = msgrcv(log_entry->queue_id, &qmsg,
                             sizeof(qmsg) - sizeof(long), 1L, IPC_NOWAIT);
        if (ret == -1) {
            usleep(100000);
            continue;
        }

        prog_log_t *log = (prog_log_t *)qmsg.data;

        char log_path[256];
        snprintf(log_path, sizeof(log_path), "logs/%s.log", log->prog_name);

        FILE *f = fopen(log_path, "w");
        if (f) {
            fwrite(log->log_content, 1, log->log_size, f);
            fclose(f);
            printf("[RECEPTIONIST] Log received for '%s' (%u bytes) -> %s\n",
                   log->prog_name, log->log_size, log_path);
        } else {
            perror("[RECEPTIONIST] fopen log file");
        }
    }
    return NULL;
}

void receptionist_init(void) {
    memset(&g_receptionist, 0, sizeof(ReceptionistState));
    strncpy(g_receptionist.uuid, get_agent_uuid(), sizeof(g_receptionist.uuid) - 1);

    if (!discover_controller()) {
        printf("[RECEPTIONIST] ERROR: Failed to discover controller\n");
        return;
    }

    atomic_store(&receptionist_running, 1);

    pthread_t query_thread;
    pthread_create(&query_thread, NULL, receptionist_thread, NULL);
    pthread_detach(query_thread);

    pthread_t listener_thread;
    pthread_create(&listener_thread, NULL, code_submission_listener_thread, NULL);
    pthread_detach(listener_thread);

    pthread_t log_thread;
    pthread_create(&log_thread, NULL, log_receiver_thread, NULL);
    pthread_detach(log_thread);

    printf("[RECEPTIONIST] Started\n");
}

void receptionist_stop(void) {
    atomic_store(&receptionist_running, 0);
    sleep(1);  // Give threads time to exit
}

void receptionist_get_master(char *ip, int *port) {
    if (!ip || !port) return;
    strncpy(ip, g_receptionist.master_ip, 16);
    *port = g_receptionist.master_port;
}

ReceptionistState* receptionist_get_state(void) {
    return &g_receptionist;
}

// ═══════════════════════════════════════════════════════════════════════════
//  STANDALONE MAIN ENTRY POINT
// ═══════════════════════════════════════════════════════════════════════════

static volatile int keep_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Launch receptionist's own network thread on port 9008
    static network_agent_config net_cfg;
    net_cfg.port = 9008;
    strcpy(net_cfg.queue_name, "receptionist_out");
    
    pthread_t net_thread;
    if (pthread_create(&net_thread, NULL, network_thread_run, &net_cfg) != 0) {
        fprintf(stderr, "[RECEPTIONIST] Failed to start network agent\n");
        return 1;
    }
    
    usleep(500000);
    
    receptionist_init();
    
    while (keep_running) {
        sleep(1);
    }
    
    receptionist_stop();
    network_stop();
    return 0;
}
