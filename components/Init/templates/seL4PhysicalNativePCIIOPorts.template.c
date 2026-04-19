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

/*- set config_devices = configuration[me.name].get("physical_pci_devices") -*/
/*- set ranges = [] -*/

/*- macro add_ioport(bus, dev, fun, start, end) -*/
    /*- set cap = alloc('native_physical_iport_%d_%d_%d_%d_%d' % (bus, dev, fun, start, end),
                       seL4_IA32_IOPort, start_port=start, end_port=end) -*/
    /*- do ranges.append((bus, dev, fun, start, end, cap)) -*/
/*- endmacro -*/

/*- if config_devices is not none -*/
    /*- for device in config_devices -*/
        /*- set owner_name = device.get('owner', '"guest"').strip('"') -*/
        /*- if owner_name == 'native' and device.get('native_generated_ioports', False) -*/
            /*- for ioport in device.get('ioports', []) -*/
                /*? add_ioport(device['bus'], device['dev'], device['fun'], ioport['start'], ioport['end']) ?*/
            /*- endfor -*/
        /*- endif -*/
    /*- endfor -*/
/*- endif -*/

int native_physical_pci_ioports_num_ranges(void)
{
    return /*? len(ranges) ?*/;
}

int native_physical_pci_ioports_get_range(int num, uint8_t *bus, uint8_t *dev, uint8_t *fun,
                                          seL4_CPtr *cap, uint16_t *start, uint16_t *end)
{
    /*- if len(ranges) == 0 -*/
        return -1;
    /*- else -*/
        switch (num) {
            /*- for bus, dev, fun, start, end, cap in ranges -*/
            case /*? loop.index0 ?*/:
                *bus = /*? bus ?*/;
                *dev = /*? dev ?*/;
                *fun = /*? fun ?*/;
                *cap = /*? cap ?*/;
                *start = /*? start ?*/;
                *end = /*? end ?*/;
                return 0;
            /*- endfor -*/
            default:
                return -1;
        }
    /*- endif -*/
}
