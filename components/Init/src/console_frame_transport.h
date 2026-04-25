#pragma once

#include <stdint.h>

enum vmm_console_stream_id {
    VMM_CONSOLE_STREAM_DRIVER_VM = 1,
    VMM_CONSOLE_STREAM_DRIVER_VM_CONTROL = 2,
    VMM_CONSOLE_STREAM_VMM_MUX_CONTROL = 3,
    VMM_CONSOLE_STREAM_NESTED_QEMU_CONTROL = 4,
    VMM_CONSOLE_STREAM_USER_VM = 5,
    VMM_CONSOLE_STREAM_TRACE_CONTROL = 6,
    VMM_CONSOLE_STREAM_VMM_DEBUG = 7,
};

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
