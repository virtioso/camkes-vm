/*
 * Copyright 2026, Unikie
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Platform definitions for NVIDIA Orin AGX (Tegra234)
 */
#pragma once

#include <assert.h>

/* GIC SPI interrupt base (after SGI and PPI) */
#define GIC_SPI_INTID_BASE      (32)
#define IRQ_SPI_OFFSET          GIC_SPI_INTID_BASE

/* ARM generic timer PPI */
#define ORINAGX_IRQ_PPI_VTIMER  (27)

typedef enum IRQConstants {
    /* Minimal IRQ definitions for vm_minimal */
    ORINAGX_HSP_TOP0_DOORBELL = GIC_SPI_INTID_BASE + 176,
    ORINAGX_HSP_AON_SHARED1 = GIC_SPI_INTID_BASE + 133,
    ORINAGX_UARTI = GIC_SPI_INTID_BASE + 285,
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

/*
 * Cross-VM connector IRQ reserve.
 *
 * Keep this in 8-bit range (<=255) because legacy PCI interrupt_line is 8-bit.
 * Current selection avoids VM PCI INTx lines used by vPCI devices.
 */
static const int free_plat_interrupts[] = { 236 };

#define GIC_NODE_PATH "/bus@0/interrupt-controller@f400000"

/* Devices to keep in guest device tree */
static const char *plat_keep_devices[] = {
    "/reserved-memory",
    "/timer",
    "/psci",
    GIC_NODE_PATH,
};

static const char *plat_keep_device_and_disable[] = {
};

/* Devices to keep with full subtree.
 *
 * IMPORTANT: Only list truly EMULATED devices here (not passthrough).
 * Passthrough devices with hardware access should be listed in devices.camkes
 * dtb() queries, which handles both DTB generation AND device frame allocation.
 *
 * Emulated devices (no hardware access, handled by VMM):
 * - /timer, /psci, GIC are in plat_keep_devices above
 *
 * These nodes are needed for Linux boot but have no hardware MMIO:
 */
static const char *plat_keep_device_and_subtree[] = {
};

static const char *plat_keep_device_and_subtree_and_disable[] = {
};
