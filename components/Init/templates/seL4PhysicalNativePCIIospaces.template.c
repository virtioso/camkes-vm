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

/*- set iospace_domain = configuration[me.name].get('iospace_domain') -*/
/*- set config_devices = configuration[me.name].get("physical_pci_devices") -*/
/*- set devices = [] -*/

/*- macro add_iospace(bus, dev, fun) -*/
    /*- set pciid = bus * 256 + dev * 8 + fun -*/
    /*- set devid = iospace_domain * 65536 + pciid -*/
    /*- set cap = alloc('native_physical_iospace_%d' % devid, seL4_IA32_IOSpace,
                       domainID=iospace_domain, bus=bus, dev=dev, fun=fun) -*/
    /*- do devices.append((bus, dev, fun, cap)) -*/
/*- endmacro -*/

/*- if config_devices is not none and iospace_domain is not none -*/
    /*- for device in config_devices -*/
        /*- set owner_name = device.get('owner', '"guest"').strip('"') -*/
        /*- if owner_name == 'native' and device.get('iospace', False) -*/
            /*? add_iospace(device['bus'], device['dev'], device['fun']) ?*/
        /*- endif -*/
    /*- endfor -*/
/*- endif -*/

int native_physical_pci_iospaces_num_devices(void)
{
    return /*? len(devices) ?*/;
}

int native_physical_pci_iospaces_get_device(int num, uint8_t *bus, uint8_t *dev, uint8_t *fun,
                                            seL4_CPtr *iospace_cap)
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
