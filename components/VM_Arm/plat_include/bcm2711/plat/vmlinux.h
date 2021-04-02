/*
 * Copyright 2021, 2022, 2023, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

/*
 * BCM2711 TRM
 * section 6.3.   GIC-400: SPI IDs start from 32
 */
#define IRQ_SPI_OFFSET 32
#define GIC_NODE_PATH "/soc/interrupt-controller@40041000"

static const int linux_pt_irqs[] = {};

/*
 * BCM2711 TRM
 * section 6.2.3. ARMC interrupts: Software interrupts 0-7: 8-15
 * section 6.3.   GIC-400 - ARMC peripheral IRQs: 64-79
 * => Software interrupt 0 = 72
 *    Software interrupt 1 = 73
 *    Software interrupt 2 = 74
 *    Software interrupt 3 = 75
 *    Software interrupt 4 = 76
 *    Software interrupt 5 = 77
 *    Software interrupt 6 = 78
 *    Software interrupt 7 = 79
 */
static const int free_plat_interrupts[] =  { 72, 73, 74, 75, 76, 77, 78, 79 };
static const char *plat_keep_devices[] = {
    "/timer",
    GIC_NODE_PATH,
};
static const char *plat_keep_device_and_disable[] = {};
static const char *plat_keep_device_and_subtree[] = {};
static const char *plat_keep_device_and_subtree_and_disable[] = {};
