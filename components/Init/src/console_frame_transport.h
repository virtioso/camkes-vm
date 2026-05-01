#pragma once

#include <stdint.h>

typedef struct vmm_console_transport_stats {
    uint64_t diag_calls;
    uint64_t diag_payload_bytes;
    uint64_t diag_wire_bytes;
    uint64_t diag_cycles;
    uint64_t debug_calls;
    uint64_t debug_payload_bytes;
    uint64_t debug_wire_bytes;
    uint64_t debug_cycles;
    uint64_t guest_calls;
    uint64_t guest_payload_bytes;
    uint64_t guest_wire_bytes;
    uint64_t guest_cycles;
} vmm_console_transport_stats_t;

void vmm_console_diag_putchar(int c);
void vmm_console_debug_putchar(int c);
void vmm_console_guest_putchar(int c);
void vmm_console_transport_get_stats(vmm_console_transport_stats_t *stats);
