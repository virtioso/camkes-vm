/*
 * Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <crossvm.h>
#include <sel4vm/guest_vm.h>
#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>
#include <sel4vmmplatsupport/drivers/pci_helper.h>

extern int get_crossvm_irq_num(void);
extern vmm_pci_space_t *pci;
extern seL4_CPtr create_async_event_notification_cap(vm_t *vm, seL4_Word badge);

static void crossvm_early_puts(const char *s)
{
    while (*s != '\0') {
        seL4_DebugPutChar(*s++);
    }
}

static void crossvm_early_putint(int value)
{
    char buf[12];
    unsigned int pos = 0;
    unsigned int magnitude;

    if (value < 0) {
        seL4_DebugPutChar('-');
        magnitude = (unsigned int)(-value);
    } else {
        magnitude = (unsigned int)value;
    }

    if (magnitude == 0) {
        seL4_DebugPutChar('0');
        return;
    }

    while (magnitude != 0 && pos < sizeof(buf)) {
        buf[pos++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    }
    while (pos != 0) {
        seL4_DebugPutChar(buf[--pos]);
    }
}

int cross_vm_connections_init(vm_t *vm, uintptr_t connection_base_addr, struct camkes_crossvm_connection *connections,
                              int num_connections)
{
    crossvm_early_puts("[vmm-early] crossvm init enter\n");
    seL4_CPtr irq_notification = create_async_event_notification_cap(vm, BIT(27) | BIT(get_crossvm_irq_num()));
    if (irq_notification == seL4_CapNull) {
        ZF_LOGE("Failed to create cross-vm async event notification cap");
        return -1;
    }
    crossvm_early_puts("[vmm-early] crossvm after irq cap\n");

    crossvm_handle_t *crossvm_connections = calloc(num_connections, sizeof(crossvm_handle_t));
    if (!crossvm_connections) {
        return -1;
    }
    crossvm_early_puts("[vmm-early] crossvm after connection alloc\n");
    for (int i = 0; i < num_connections; i++) {
        crossvm_early_puts("[vmm-early] crossvm connection setup\n");
        /* Initialise crossvm dataport handle */
        crossvm_dataport_handle_t *data_dp_handle = calloc(1, sizeof(crossvm_dataport_handle_t));
        if (!data_dp_handle) {
            ZF_LOGE("Failed to initialse cross vm connection dataport %d", i);
            return -1;
        }
        dataport_caps_handle_t *handle = connections[i].handle;
        data_dp_handle->frame_size_bits = handle->get_frame_size_bits();
        data_dp_handle->num_frames = handle->get_num_frame_caps();
        data_dp_handle->frames = handle->get_frame_caps();

        /* Initialise crossvm connection */
        crossvm_connections[i].dataport = data_dp_handle;
        crossvm_connections[i].control_dataport = NULL;
        crossvm_connections[i].emit_fn = connections[i].emit_fn;
        crossvm_connections[i].consume_id = connections[i].consume_badge;

        if (connections[i].control_handle) {
            crossvm_early_puts("[vmm-early] crossvm control setup\n");
            crossvm_dataport_handle_t *control_dp_handle = calloc(1, sizeof(crossvm_dataport_handle_t));
            if (!control_dp_handle) {
                ZF_LOGE("Failed to initialise control dataport %d", i);
                free(data_dp_handle);
                return -1;
            }
            dataport_caps_handle_t *control_handle = connections[i].control_handle;
            control_dp_handle->frame_size_bits = control_handle->get_frame_size_bits();
            control_dp_handle->num_frames = control_handle->get_num_frame_caps();
            control_dp_handle->frames = control_handle->get_frame_caps();
            crossvm_connections[i].control_dataport = control_dp_handle;
        }
    }

    crossvm_early_puts("[vmm-early] crossvm before common\n");
    int ret = cross_vm_connections_init_common(vm, connection_base_addr, crossvm_connections, num_connections,
                                               pci, get_crossvm_irq_num);
    crossvm_early_puts("[vmm-early] crossvm after common ret=");
    crossvm_early_putint(ret);
    crossvm_early_puts("\n");
    free(crossvm_connections);
    crossvm_early_puts("[vmm-early] crossvm done\n");
    return ret;
}
