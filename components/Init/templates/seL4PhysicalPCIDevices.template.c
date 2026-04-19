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

#define PHYSICAL_PCI_DEVICE_OWNER_NONE   0
#define PHYSICAL_PCI_DEVICE_OWNER_GUEST  1
#define PHYSICAL_PCI_DEVICE_OWNER_NATIVE 2

/*- set config_devices = configuration[me.name].get("physical_pci_devices") -*/
/*- set devices = [] -*/

/*- if config_devices is not none -*/
    /*- for device in config_devices -*/
        /*- set owner_name = device.get('owner', '"guest"').strip('"') -*/
        /*- if owner_name == 'guest' -*/
            /*- set owner = 1 -*/
        /*- elif owner_name == 'native' -*/
            /*- set owner = 2 -*/
        /*- else -*/
            /*- set owner = 0 -*/
        /*- endif -*/
        /*- set interrupt_pin = device.get('interrupt_pin', 1) -*/
        /*- do devices.append((device['name'].strip('"'), device['bus'], device['dev'], device['fun'], owner, interrupt_pin)) -*/
    /*- endfor -*/
/*- endif -*/

int physical_pci_devices_num_devices(void)
{
    return /*? len(devices) ?*/;
}

int physical_pci_devices_get_device(int num, uint8_t *bus, uint8_t *dev, uint8_t *fun, int *owner)
{
    /*- if len(devices) == 0 -*/
        return -1;
    /*- else -*/
        switch (num) {
            /*- for name, bus, dev, fun, owner, interrupt_pin in devices -*/
            case /*? loop.index0 ?*/:
                *bus = /*? bus ?*/;
                *dev = /*? dev ?*/;
                *fun = /*? fun ?*/;
                *owner = /*? owner ?*/;
                return 0;
            /*- endfor -*/
            default:
                return -1;
        }
    /*- endif -*/
}

int physical_pci_devices_get_owner(uint8_t bus, uint8_t dev, uint8_t fun)
{
    /*- if len(devices) == 0 -*/
        return PHYSICAL_PCI_DEVICE_OWNER_NONE;
    /*- else -*/
            /*- for name, device_bus, device_dev, device_fun, owner, interrupt_pin in devices -*/
                if (bus == /*? device_bus ?*/ &&
                    dev == /*? device_dev ?*/ &&
                    fun == /*? device_fun ?*/) {
                    return /*? owner ?*/;
                }
            /*- endfor -*/
            return PHYSICAL_PCI_DEVICE_OWNER_NONE;
    /*- endif -*/
}

int physical_pci_devices_guest_visible(uint8_t bus, uint8_t dev, uint8_t fun)
{
    int owner = physical_pci_devices_get_owner(bus, dev, fun);
    return owner == PHYSICAL_PCI_DEVICE_OWNER_GUEST;
}

int physical_pci_devices_get_interrupt_pin(uint8_t bus, uint8_t dev, uint8_t fun)
{
    /*- if len(devices) == 0 -*/
        return 0;
    /*- else -*/
            /*- for name, device_bus, device_dev, device_fun, owner, interrupt_pin in devices -*/
                if (bus == /*? device_bus ?*/ &&
                    dev == /*? device_dev ?*/ &&
                    fun == /*? device_fun ?*/) {
                    return /*? interrupt_pin ?*/;
                }
            /*- endfor -*/
            return 0;
    /*- endif -*/
}
