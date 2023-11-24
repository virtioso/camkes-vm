/*
 * Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <vmlinux.h>
#include <camkes.h>

#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/arch/vpci.h>
#include <sel4vmmplatsupport/drivers/pci_helper.h>

#include <libfdt.h>

vmm_pci_space_t *pci;
vmm_io_port_list_t *io_ports;

static void vpci_init(vm_t *vm, void *cookie)
{
    int err;

    err = vmm_pci_init(&pci);
    if (err) {
        ZF_LOGF("Failed to initialise vmm pci");
        /* no return */
    }

    err = vmm_io_port_init(&io_ports, FREE_IOPORT_START);
    if (err) {
        ZF_LOGF("Failed to initialise VM ioports");
        /* no return */
    }
}

DEFINE_MODULE(vpci_init, NULL, vpci_init)

static void vpci_register_devices(vm_t *vm, void *cookie)
{
}

DEFINE_MODULE(vpci_register_devices, NULL, vpci_register_devices)
DEFINE_MODULE_DEP(vpci_register_devices, vpci_init)

static void vpci_install(vm_t *vm, void *cookie)
{
    int err = vm_install_vpci(vm, io_ports, pci);
    if (err) {
        ZF_LOGF("Failed to install VPCI device");
        /* no return */
    }

    if (!vm_config.generate_dtb) {
        return;
    }

    int gic_offset = fdt_path_offset(fdt_ori, GIC_NODE_PATH);
    if (gic_offset < 0) {
        ZF_LOGF("Failed to find gic node from path: %s", GIC_NODE_PATH);
        /* no return */
    }
    int gic_phandle = fdt_get_phandle(fdt_ori, gic_offset);
    if (0 == gic_phandle) {
        ZF_LOGF("Failed to find phandle in gic node");
        /* no return */
    }
    err = fdt_generate_vpci_node(vm, pci, gen_dtb_buf, gic_phandle);
    if (err) {
        ZF_LOGF("Couldn't generate vPCI interrupt map (%d)", err);
        /* no return */
    }
}

DEFINE_MODULE(vpci_install, NULL, vpci_install)
DEFINE_MODULE_DEP(vpci_install, vpci_register_devices)
