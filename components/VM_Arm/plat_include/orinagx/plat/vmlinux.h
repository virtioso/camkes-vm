/*
 * Copyright 2024, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Platform definitions for NVIDIA Orin AGX (Tegra234)
 */
#pragma once

#include <assert.h>

/* GIC SPI interrupt base (after SGI and PPI) */
#define GIC_SPI_INTID_BASE      (32)

/* ARM generic timer PPI */
#define ORINAGX_IRQ_PPI_VTIMER  (27)

typedef enum IRQConstants {
    /* Minimal IRQ definitions for vm_minimal */
    ORINAGX_HSP_TOP0_DOORBELL = GIC_SPI_INTID_BASE + 176,
    ORINAGX_HSP_AON_SHARED1 = GIC_SPI_INTID_BASE + 133,
    ORINAGX_UARTI = GIC_SPI_INTID_BASE + 146,
    maxIRQ = GIC_SPI_INTID_BASE + 480
} platform_interrupt_t;

#define MAX_IRQ maxIRQ

/*
 * Platform IRQs to pass through to guest.
 *
 * NOTE: IRQs are also specified in devices.camkes via dtb_irqs.
 * Avoid duplicates - IRQs should only be listed in ONE place.
 * Prefer dtb_irqs in devices.camkes for platform-specific configuration.
 *
 * Currently empty - all IRQs come from vm0.dtb_irqs in devices.camkes.
 */
static const int linux_pt_irqs[] = {};

static const int free_plat_interrupts[] = { 400 + GIC_SPI_INTID_BASE };

#define GIC_NODE_PATH "/bus@0/interrupt-controller@f400000"

/* Devices to keep in guest device tree */
static const char *plat_keep_devices[] = {
    "/timer",
    "/psci",
    GIC_NODE_PATH,
};

static const char *plat_keep_device_and_disable[] = {
};

/* Devices to keep with full subtree.
 * NOTE: /serial (TCU) is NOT listed here - it's added programmatically by
 * fdt_plat_customize() in src/modules/plat/orinagx/fdt.c with correct
 * HSP AON phandle reference for the mboxes property. */
static const char *plat_keep_device_and_subtree[] = {
    "/bus@0",                   /* simple-bus parent - required for child probing */
    "/bus@0/misc@100000",       /* APB MISC - for tegra_is_silicon() */
    "/bus@0/serial@31d0000",    /* UARTI - PL011 UART (earlycon/backup) */
    "/bus@0/hsp@c150000",       /* HSP AON - for TCU mailbox */
    "/bus@0/hsp@3c00000",       /* HSP Top0 - BPMP doorbell */
    "/sram@40000000",           /* CPU-BPMP shared memory */
    "/bpmp",                    /* BPMP for clocks/resets/power */
    "/reserved-memory",
    "/firmware",
};

static const char *plat_keep_device_and_subtree_and_disable[] = {
};
