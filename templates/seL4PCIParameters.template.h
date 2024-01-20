/*
 * Copyright 2024, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdbool.h>

typedef struct {
    bool use_ecam;
    char const *fdt_msi_node_path;
} pci_config_t;

extern const pci_config_t pci_config;
