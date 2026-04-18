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
/*- set iospace_domain = configuration[me.name].get('iospace_domain') -*/
/*- set devices = [] -*/

/*- macro add_iospace(bus, dev, fun) -*/
    /*- set pciid = bus * 256 + dev * 8 + fun -*/
    /*- set devid = iospace_domain * 65536 + pciid -*/
    /*- set cap = alloc('physical_iospace_%d' % devid, seL4_IA32_IOSpace,
                       domainID=iospace_domain, bus=bus, dev=dev, fun=fun) -*/
    /*- do devices.append((bus, dev, fun, cap)) -*/
/*- endmacro -*/

/*
 * Structural q35 currently exposes two outer physical virtio devices:
 * 00:01.0 virtio-net-pci
 * 00:02.0 virtio-blk-pci
 */
/*- if host_bridge == 'qemu_pc_q35' and iospace_domain is not none -*/
    /*? add_iospace(0x00, 0x01, 0x0) ?*/
    /*? add_iospace(0x00, 0x02, 0x0) ?*/
/*- endif -*/

int physical_pci_iospaces_num_devices(void)
{
    return /*? len(devices) ?*/;
}

int physical_pci_iospaces_get_device(int num, uint8_t *bus, uint8_t *dev, uint8_t *fun, seL4_CPtr *iospace_cap)
{
    /*- if len(devices) == 0 -*/
        return -1;
    /*- else -*/
        switch (num) {
            /*- for bus, dev, fun, cap in devices -*/
            case /*? loop.index0 ?*/:
                *bus = /*? bus ?*/;
                *dev = /*? dev ?*/;
                *fun = /*? fun ?*/;
                *iospace_cap = /*? cap ?*/;
                return 0;
            /*- endfor -*/
            default:
                return -1;
        }
    /*- endif -*/
}
