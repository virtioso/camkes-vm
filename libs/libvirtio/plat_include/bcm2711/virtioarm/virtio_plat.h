/*
 * Copyright 2021, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

/*
 * BCM2711 TRM
 * section 6.3.   GIC-400: SPI IDs start from 32
 *
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

#define IRQ_SPI_OFFSET 32
#define VIRTIO_NET_PLAT_INTERRUPT_LINE (72)
#define VIRTIO_CON_PLAT_INTERRUPT_LINE (72)
