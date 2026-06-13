#include "reception.h"
#include "net_utils.h"
#include "network_agent.h"
#include "socket.h"
#include "ms_queue.h"
#include "master_thread.h"
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

static char pending_code[7500] = {0};
static int pending_code_len = 0;
static int has_pending_code = 0;
static pthread_mutex_t pending_code_mutex = PTHREAD_MUTEX_INITIALIZER;

static void forward_code_to_master(void) {
    if (!has_pending_code) return;
    if (g_receptionist.master_port == 0 || strcmp(g_receptionist.master_ip, "NONE") == 0) return;

    printf("[RECEPTIONIST] Forwarding code to master at %s:%d\n", g_receptionist.master_ip, g_receptionist.master_port);

    size_t pkt_size = sizeof(message_t) + sizeof(program_message_t);
    message_t *pkt = (message_t *)malloc(pkt_size);
    if (pkt) {
        pkt->mq_type = 1;
        strcpy(pkt->type, "PROG");
        pkt->size = sizeof(program_message_t);

        program_message_t *prog = (program_message_t *)pkt->data;
        strcpy(prog->program_name, "submitted_prog.c");
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
//  THREAD 2: CODE SUBMISSION PROCESSOR THREAD
//  Listens as a basic HTTP server on port 9010 and prints the body
// ═══════════════════════════════════════════════════════════════════════════

void* code_submission_listener_thread(void* arg) {
    (void)arg;
    
    connection *listener = create_listener("0.0.0.0", RECEPTIONIST_LISTENING_PORT, 5);
    if (!listener) {
        printf("[RECEPTIONIST] ERROR: Failed to start HTTP server on port %d\n", RECEPTIONIST_LISTENING_PORT);
        return NULL;
    }
    
    // Set receive timeout to prevent accept from blocking indefinitely during stop
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(listener->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    while (atomic_load(&receptionist_running)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listener->sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0) {
            char buffer[8192];
            memset(buffer, 0, sizeof(buffer));
            ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                // Find HTTP body (after \r\n\r\n)
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
                
                // Send simple HTTP 200 OK response
                const char *resp = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 2\r\n"
                    "Connection: close\r\n\r\n"
                    "OK";
                write(client_fd, resp, strlen(resp));
            }
            close(client_fd);
        } else {
            usleep(10000); // 10ms
        }
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
    strcpy(g_receptionist.controller_ip, "127.0.0.1");
    return 1;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PUBLIC API
// ═══════════════════════════════════════════════════════════════════════════

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
