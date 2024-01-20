/*
 * Copyright 2024, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <camkes.h>

/*- set pci_config = configuration[me.name].get('pci_config') -*/
const pci_config_t pci_config = {
/*- if pci_config is not none -*/
    .fdt_msi_node_path = "/*? pci_config.get('fdt_msi_node_path') ?*/",
/*- endif -*/
};


