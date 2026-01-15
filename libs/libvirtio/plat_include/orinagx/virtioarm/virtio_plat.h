/*
 * Copyright 2026, Unikie
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Virtio platform configuration for NVIDIA Orin AGX (Tegra234)
 *
 * Uses SPI 220 (INTID 252) for virtio interrupts, same as TX2.
 * This SPI is not used by any hardware device on Orin AGX.
 */
#pragma once

#define IRQ_SPI_OFFSET 32
#define VIRTIO_NET_PLAT_INTERRUPT_LINE (220 + IRQ_SPI_OFFSET)
#define VIRTIO_CON_PLAT_INTERRUPT_LINE (220 + IRQ_SPI_OFFSET)
