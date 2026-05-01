#include "console_frame_transport.h"

#include <camkes.h>
#include <stdint.h>
#include <platsupport/arch/tsc.h>

static vmm_console_transport_stats_t vmm_console_transport_stats;

static inline uint64_t console_transport_cycles_now(void)
{
    return rdtsc_pure();
}

static void console_transport_emit_debug_raw(uint8_t byte)
{
    putchar_putchar(byte);
}

static void console_transport_emit_guest_raw(uint8_t byte)
{
    guest_putchar_putchar(byte);
}

void vmm_console_transport_get_stats(vmm_console_transport_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    *stats = vmm_console_transport_stats;
}

void vmm_console_diag_putchar(int c)
{
#ifdef VMM_CONSOLE_DROP_DIAG_OUTPUT
    (void)c;
    return;
#else
    uint64_t start = console_transport_cycles_now();
    console_transport_emit_debug_raw((uint8_t)c);
    vmm_console_transport_stats.diag_calls++;
    vmm_console_transport_stats.diag_payload_bytes++;
    vmm_console_transport_stats.diag_wire_bytes++;
    vmm_console_transport_stats.diag_cycles += console_transport_cycles_now() - start;
#endif
}

void vmm_console_debug_putchar(int c)
{
    uint64_t start = console_transport_cycles_now();
    console_transport_emit_debug_raw((uint8_t)c);
    vmm_console_transport_stats.debug_calls++;
    vmm_console_transport_stats.debug_payload_bytes++;
    vmm_console_transport_stats.debug_wire_bytes++;
    vmm_console_transport_stats.debug_cycles += console_transport_cycles_now() - start;
}

void vmm_console_guest_putchar(int c)
{
    uint64_t start = console_transport_cycles_now();
    console_transport_emit_guest_raw((uint8_t)c);
    vmm_console_transport_stats.guest_calls++;
    vmm_console_transport_stats.guest_payload_bytes++;
    vmm_console_transport_stats.guest_wire_bytes++;
    vmm_console_transport_stats.guest_cycles += console_transport_cycles_now() - start;
}
