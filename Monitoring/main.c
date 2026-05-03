#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "Monitoring.h"

int main() {
    pthread_t mon_thread;
    pthread_create(&mon_thread, NULL, monitoring_thread_run, NULL);

    for (int i = 0; i < 3; i++) {
        sleep(6); // attendre un cycle complet
        MachineMetrics m = monitoring_get_latest();
        printf("=== Snapshot %d ===\n", i + 1);
        printf("CPU usage     : %.2f%%\n", m.cpu_usage);
        printf("Load avg      : %.2f %.2f %.2f\n", m.load_avg[0], m.load_avg[1], m.load_avg[2]);
        printf("RAM total     : %.2f GB\n", m.mem_total_gb);
        printf("RAM used      : %.2f GB\n", m.mem_used_gb);
        printf("RAM free      : %.2f GB\n", m.mem_free_gb);
        printf("RAM available : %.2f GB\n", m.mem_available_gb);
        printf("Disk total    : %.2f GB\n", m.disk_total_gb);
        printf("Disk free     : %.2f GB\n", m.disk_free_gb);
        printf("Disk IO       : %.2f%%\n", m.disk_usage_percent);
        printf("Network BW    : %.2f MB/s\n", m.network_bandwidth_mbps);
        printf("Connections   : %d\n", m.active_connections);
        printf("Processes     : %d\n", m.active_processes);
        printf("Uptime        : %ld s\n", m.uptime_seconds);
        printf("Context sw/s  : %.2f\n", m.context_switch_rate);
        printf("Is overloaded : %s\n", m.is_overloaded ? "YES" : "NO");
        printf("Timestamp     : %ld\n", m.timestamp);
        printf("\n");
    }

    monitoring_stop();
    pthread_join(mon_thread, NULL);
    return 0;
}