/*
 * Copyright 2026, Unikie
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <camkes/error.h>
#include <sel4/sel4.h>
#include <stdint.h>
#include <stdio.h>

/*- set host_bridge = configuration[me.name].get("physical_pci_host_bridge") -*/
/*- if host_bridge is not none -*/
    /*- set host_bridge = host_bridge.strip('"') -*/
/*- endif -*/
/*- set config_devices = configuration[me.name].get("physical_pci_devices") -*/
/*- set irqs = [] -*/
/*- set irqnotification_object = alloc_obj('physical_pci_irq_notification_obj', seL4_NotificationObject) -*/
/*- set irqnotification_object_cap = alloc_cap('physical_pci_irq_notification_obj', irqnotification_object, read=True) -*/

/*- macro add_irq(name, ioapic, source, level_trig, active_low, dest) -*/
    /*- set cap = alloc('physical_pci_irq_%d_%d' % (ioapic, source), seL4_IRQHandler,
                       vector=dest, ioapic=ioapic, ioapic_pin=source,
                       level=level_trig, polarity=active_low,
                       notification=my_cnode[irqnotification_object_cap]) -*/
    /*- do irqs.append((name, ioapic, source, level_trig, active_low, dest, cap)) -*/
/*- endmacro -*/

/*
 * Structural q35 physical PCI passthrough uses the QEMU/ICH9 default legacy
 * INTx routing model from hw/isa/lpc_ich9.c:
 *   PIRQ = (slot + intx) % 4 + 4
 *   GSI  = 16 + PIRQ
 * For INTA-D expressed as PCI interrupt pins 1-4, that becomes:
 *   GSI = 20 + ((slot + pin - 1) % 4)
 */
/*- if config_devices is not none -*/
    /*- for device in config_devices -*/
        /*- set owner_name = device.get('owner', '"guest"').strip('"') -*/
        /*- set irq = device.get('irq') -*/
        /*- set interrupt_pin = device.get('interrupt_pin', 1) -*/
        /*- if owner_name == 'guest' and irq is not none -*/
            /*? add_irq(device['name'].strip('"'), irq['ioapic'], irq['source'],
                       irq['level_trig'], irq['active_low'], irq['dest']) ?*/
        /*- elif owner_name == 'guest' and host_bridge == 'qemu_pc_q35' and interrupt_pin >= 1 and interrupt_pin <= 4 -*/
            /*- set gsi = 20 + ((device['dev'] + interrupt_pin - 1) % 4) -*/
            /*? add_irq(device['name'].strip('"'), 0, gsi, 1, 1, gsi) ?*/
        /*- endif -*/
    /*- endfor -*/
/*- elif host_bridge == 'qemu_pc_q35' -*/
    /*? add_irq('q35-slot1-inta', 0, 21, 1, 1, 21) ?*/
    /*? add_irq('q35-slot2-inta', 0, 22, 1, 1, 22) ?*/
/*- endif -*/

int physical_pci_irqs_num_irqs(void)
{
    return /*? len(irqs) ?*/;
}

int physical_pci_irqs_get_irq(int irq, seL4_CPtr *irq_handler, uint8_t *ioapic, uint8_t *source,
                              int *level_trig, int *active_low, uint8_t *dest)
{
    /*- if len(irqs) == 0 -*/
        return -1;
    /*- else -*/
        switch (irq) {
            /*- for name, ioapic, source, level_trig, active_low, dest, cap in irqs -*/
            case /*? loop.index0 ?*/:
                *irq_handler = /*? cap ?*/;
                *ioapic = /*? ioapic ?*/;
                *source = /*? source ?*/;
                *level_trig = /*? level_trig ?*/;
                *active_low = /*? active_low ?*/;
                *dest = /*? dest ?*/;
                return 0;
            /*- endfor -*/
            default:
                return -1;
        }
    /*- endif -*/
}
