/*
 * Copyright 2026, Unikie
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <camkes/error.h>
#include <stdint.h>
#include <sel4/sel4.h>
#include <stdio.h>

/*- if options.architecture in ['ia32', 'x86_64'] -*/
    /*- set bits_to_frame_type = { 12:seL4_FrameObject, 21:seL4_LargePageObject, 30:seL4_FrameObject } -*/
/*- else -*/
    /*- set bits_to_frame_type = { 12:seL4_FrameObject, 20:seL4_ARM_SectionObject, 21:seL4_ARM_SectionObject, 30:seL4_FrameObject } -*/
/*- endif -*/

/*- set host_bridge = configuration[me.name].get("physical_pci_host_bridge") -*/
/*- if host_bridge is not none -*/
    /*- set host_bridge = host_bridge.strip('"') -*/
/*- endif -*/
/*- set regions = [] -*/
/*- set frames = [] -*/

/*- macro add_region(base, size, page_bits) -*/
    /*- do regions.append((base, size, page_bits)) -*/
    /*- for frame_offset in range(0, size, 2 ** page_bits) -*/
        /*- set frame = base + frame_offset -*/
        /*- set object = alloc_obj('physical_pci_host_bridge_frame_%d' % frame, bits_to_frame_type[page_bits], paddr=frame) -*/
        /*- set cap = alloc_cap('physical_pci_host_bridge_frame_%d' % frame, object, read=true, write=true) -*/
        /*- do frames.append((frame, cap)) -*/
    /*- endfor -*/
/*- endmacro -*/

/*- if host_bridge == 'qemu_pc_q35' -*/
    /*? add_region(0xb0000000, 0x10000000, 21) ?*/
    /*? add_region(0xc0000000, 0x3ec00000, 21) ?*/
/*- elif host_bridge == 'qemu_arm_virt' -*/
    /*? add_region(0x3f000000, 0x1000000, 21) ?*/
    /*? add_region(0x10000000, 0x2eff0000, 21) ?*/
/*- endif -*/

int physical_pci_host_bridge_num_regions(void)
{
    return /*? len(regions) ?*/;
}

int physical_pci_host_bridge_get_region(int num, uintptr_t *base, size_t *size, int *page_bits)
{
    /*- if len(regions) == 0 -*/
        return -1;
    /*- else -*/
        switch (num) {
            /*- for base, size, page_bits in regions -*/
                case /*? loop.index0 ?*/:
                    *base = /*? base ?*/;
                    *size = /*? size ?*/;
                    *page_bits = /*? page_bits ?*/;
                    return 0;
            /*- endfor -*/
            default:
                return -1;
        }
    /*- endif -*/
}

seL4_CPtr physical_pci_host_bridge_get_mem_frame(uintptr_t paddr)
{
    /*- if len(frames) == 0 -*/
        return 0;
    /*- else -*/
            /*- for paddr, cap in frames -*/
                if (paddr >= /*? paddr ?*/ &&
                    paddr < /*? paddr ?*/ + BIT(21)) {
                    return /*? cap ?*/;
                }
            /*- endfor -*/
            return 0;
    /*- endif -*/
}
