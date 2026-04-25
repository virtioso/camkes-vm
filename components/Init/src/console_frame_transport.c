#include "console_frame_transport.h"

#include <camkes.h>
#include <stdint.h>
#include <platsupport/arch/tsc.h>
#include <string.h>

#define CONSOLE_FRAME_MAGIC_0 'C'
#define CONSOLE_FRAME_MAGIC_1 'F'
#define CONSOLE_FRAME_VERSION 1
#define CONSOLE_FRAME_DIRECTION_RX 1
#define CONSOLE_FRAME_FLAGS 0

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

static uint8_t console_transport_guest_stream_id(void)
{
    const char *instance_name = get_instance_name();

    if (instance_name != NULL && strcmp(instance_name, "vm1") == 0) {
        return VMM_CONSOLE_STREAM_USER_VM;
    }
    return VMM_CONSOLE_STREAM_DRIVER_VM;
}

void vmm_console_transport_get_stats(vmm_console_transport_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    *stats = vmm_console_transport_stats;
}

#ifdef VMM_CONSOLE_FRAMED_OUTPUT

static void console_transport_emit_frame_byte(
    void (*emit_raw)(uint8_t),
    uint8_t stream_id,
    uint8_t byte
)
{
    emit_raw(CONSOLE_FRAME_MAGIC_0);
    emit_raw(CONSOLE_FRAME_MAGIC_1);
    emit_raw(CONSOLE_FRAME_VERSION);
    emit_raw(stream_id);
    emit_raw(CONSOLE_FRAME_DIRECTION_RX);
    emit_raw(CONSOLE_FRAME_FLAGS);
    emit_raw(0);
    emit_raw(0);
    emit_raw(0);
    emit_raw(1);
    emit_raw(byte);
}

void vmm_console_diag_putchar(int c)
{
    uint64_t start = console_transport_cycles_now();
    console_transport_emit_frame_byte(
        console_transport_emit_debug_raw,
        VMM_CONSOLE_STREAM_VMM_MUX_CONTROL,
        (uint8_t)c
    );
    vmm_console_transport_stats.diag_calls++;
    vmm_console_transport_stats.diag_payload_bytes++;
    vmm_console_transport_stats.diag_wire_bytes += 11;
    vmm_console_transport_stats.diag_cycles += console_transport_cycles_now() - start;
}

void vmm_console_debug_putchar(int c)
{
    uint64_t start = console_transport_cycles_now();
    console_transport_emit_frame_byte(
        console_transport_emit_debug_raw,
        VMM_CONSOLE_STREAM_VMM_DEBUG,
        (uint8_t)c
    );
    vmm_console_transport_stats.debug_calls++;
    vmm_console_transport_stats.debug_payload_bytes++;
    vmm_console_transport_stats.debug_wire_bytes += 11;
    vmm_console_transport_stats.debug_cycles += console_transport_cycles_now() - start;
}

void vmm_console_guest_putchar(int c)
{
    uint64_t start = console_transport_cycles_now();
    console_transport_emit_frame_byte(
        console_transport_emit_guest_raw,
        console_transport_guest_stream_id(),
        (uint8_t)c
    );
    vmm_console_transport_stats.guest_calls++;
    vmm_console_transport_stats.guest_payload_bytes++;
    vmm_console_transport_stats.guest_wire_bytes += 11;
    vmm_console_transport_stats.guest_cycles += console_transport_cycles_now() - start;
}

#else

void vmm_console_diag_putchar(int c)
{
    uint64_t start = console_transport_cycles_now();
    console_transport_emit_debug_raw((uint8_t)c);
    vmm_console_transport_stats.diag_calls++;
    vmm_console_transport_stats.diag_payload_bytes++;
    vmm_console_transport_stats.diag_wire_bytes++;
    vmm_console_transport_stats.diag_cycles += console_transport_cycles_now() - start;
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

#endif
