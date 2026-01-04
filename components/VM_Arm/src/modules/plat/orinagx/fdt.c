/*
 * Copyright 2024, Technology Innovation Institute
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Orin AGX platform-specific DTB customization.
 *
 * Adds the TCU (Tegra Combined UART) serial node to the guest DTB
 * with correct phandle reference to HSP AON mailbox provider.
 *
 * The TCU console requires the HSP AON (Hardware Synchronization Primitive)
 * device for mailbox communication. When CAmkES generates the DTB via fdtgen,
 * phandle references can become invalid. This hook adds the TCU node
 * programmatically with the correct phandle lookup at runtime.
 */

#define ZF_LOG_LEVEL ZF_LOG_INFO

#include <utils/util.h>
#include <libfdt.h>
#include <sel4vm/guest_vm.h>

/* Device paths in Tegra234 device tree */
#define GIC_PATH            "/bus@0/interrupt-controller@f400000"
#define HSP_TOP0_PATH       "/bus@0/hsp@3c00000"
#define HSP_AON_PATH        "/bus@0/hsp@c150000"
#define SERIAL_NODE_NAME    "serial"

/* MGBE MAC address - locally administered (bit 1 of first octet = 1)
 * Format: string "xx:xx:xx:xx:xx:xx" for NVIDIA nvethernet driver
 */
#define MGBE0_MAC_ADDR      "02:04:06:08:0a:0c"

/* TCU mailbox parameters
 * TCU requires TWO mailboxes:
 *   - RX from HSP Top0 (shared mailbox 0)
 *   - TX to HSP AON (shared mailbox 1)
 *
 * Type: TEGRA_HSP_MBOX_TYPE_SM = 0x1 (shared mailbox)
 * RX param: TEGRA_HSP_SM_RX(0) = 0x00000000 (RX flag | index 0)
 * TX param: TEGRA_HSP_SM_TX(1) = 0x80000001 (TX flag | index 1)
 * See: include/dt-bindings/mailbox/tegra186-hsp.h
 */
#define TCU_MBOX_TYPE_SM    0x1         /* TEGRA_HSP_MBOX_TYPE_SM */
#define TCU_RX_PARAM        0x00000000  /* TEGRA_HSP_SM_RX(0) */
#define TCU_TX_PARAM        0x80000001  /* TEGRA_HSP_SM_TX(1) */

/**
 * Set interrupt-parent at root level to enable child interrupt routing.
 *
 * Without this, child nodes like HSP cannot resolve their interrupts
 * and their drivers fail to probe.
 */
static int fdt_set_interrupt_parent(void *fdt)
{
    int err;

    /* Find GIC node */
    int gic_off = fdt_path_offset(fdt, GIC_PATH);
    if (gic_off < 0) {
        ZF_LOGE("GIC node not found at %s (err=%d)", GIC_PATH, gic_off);
        return -1;
    }

    /* Get or assign phandle for GIC */
    uint32_t gic_phandle = fdt_get_phandle(fdt, gic_off);
    if (!gic_phandle) {
        gic_phandle = fdt_get_max_phandle(fdt) + 1;
        err = fdt_setprop_u32(fdt, gic_off, "phandle", gic_phandle);
        if (err) {
            ZF_LOGE("Failed to set GIC phandle: %d", err);
            return err;
        }
        ZF_LOGI("Assigned phandle 0x%x to GIC", gic_phandle);
    }

    /* Set interrupt-parent at root level */
    int root = fdt_path_offset(fdt, "/");
    if (root < 0) {
        ZF_LOGE("Root node not found: %d", root);
        return root;
    }

    err = fdt_setprop_u32(fdt, root, "interrupt-parent", gic_phandle);
    if (err) {
        ZF_LOGE("Failed to set interrupt-parent: %d", err);
        return err;
    }

    ZF_LOGI("Set root interrupt-parent to GIC phandle 0x%x", gic_phandle);
    return 0;
}

/**
 * Get or assign phandle for a node.
 */
static uint32_t fdt_ensure_phandle(void *fdt, int node_off, const char *name)
{
    uint32_t phandle = fdt_get_phandle(fdt, node_off);
    if (!phandle) {
        phandle = fdt_get_max_phandle(fdt) + 1;
        int err = fdt_setprop_u32(fdt, node_off, "phandle", phandle);
        if (err) {
            ZF_LOGE("Failed to set %s phandle: %d", name, err);
            return 0;
        }
        ZF_LOGI("Assigned phandle 0x%x to %s", phandle, name);
    } else {
        ZF_LOGI("%s has phandle 0x%x", name, phandle);
    }
    return phandle;
}

/**
 * Add TCU serial node with correct HSP phandle references.
 *
 * The TCU driver requires TWO mailboxes:
 *   mboxes = <&hsp_top0 TYPE RX_PARAM>, <&hsp_aon TYPE TX_PARAM>
 *   mbox-names = "rx", "tx"
 *
 * This function looks up both HSP nodes' phandles and creates the
 * serial node with correct mboxes references.
 */
static int fdt_generate_tcu_node(void *fdt)
{
    int err;

    /* Find HSP Top0 node (for RX mailbox) */
    int hsp_top0_off = fdt_path_offset(fdt, HSP_TOP0_PATH);
    if (hsp_top0_off < 0) {
        ZF_LOGE("HSP Top0 node not found at %s (err=%d)", HSP_TOP0_PATH, hsp_top0_off);
        return -1;
    }
    uint32_t hsp_top0_phandle = fdt_ensure_phandle(fdt, hsp_top0_off, "HSP Top0");
    if (!hsp_top0_phandle) return -1;

    /* Find HSP AON node (for TX mailbox) */
    int hsp_aon_off = fdt_path_offset(fdt, HSP_AON_PATH);
    if (hsp_aon_off < 0) {
        ZF_LOGE("HSP AON node not found at %s (err=%d)", HSP_AON_PATH, hsp_aon_off);
        return -1;
    }
    uint32_t hsp_aon_phandle = fdt_ensure_phandle(fdt, hsp_aon_off, "HSP AON");
    if (!hsp_aon_phandle) return -1;

    /* Add serial node at root */
    int root = fdt_path_offset(fdt, "/");
    if (root < 0) {
        ZF_LOGE("Root node not found: %d", root);
        return root;
    }

    int tcu_off = fdt_add_subnode(fdt, root, SERIAL_NODE_NAME);
    if (tcu_off < 0) {
        if (tcu_off == -FDT_ERR_EXISTS) {
            ZF_LOGI("/%s node already exists, skipping", SERIAL_NODE_NAME);
            return 0;
        }
        ZF_LOGE("Failed to add /%s node: %d", SERIAL_NODE_NAME, tcu_off);
        return tcu_off;
    }

    /* Set compatible property - two strings, null-separated */
    const char compatible[] = "nvidia,tegra234-tcu\0nvidia,tegra194-tcu";
    err = fdt_setprop(fdt, tcu_off, "compatible", compatible, sizeof(compatible));
    if (err) {
        ZF_LOGE("Failed to set compatible: %d", err);
        return err;
    }

    /* Set mboxes property: <rx_phandle type param> <tx_phandle type param> */
    uint32_t mboxes[6] = {
        cpu_to_fdt32(hsp_top0_phandle),   /* RX: HSP Top0 */
        cpu_to_fdt32(TCU_MBOX_TYPE_SM),
        cpu_to_fdt32(TCU_RX_PARAM),
        cpu_to_fdt32(hsp_aon_phandle),    /* TX: HSP AON */
        cpu_to_fdt32(TCU_MBOX_TYPE_SM),
        cpu_to_fdt32(TCU_TX_PARAM)
    };
    err = fdt_setprop(fdt, tcu_off, "mboxes", mboxes, sizeof(mboxes));
    if (err) {
        ZF_LOGE("Failed to set mboxes: %d", err);
        return err;
    }

    /* Set mbox-names property - two strings, null-separated */
    const char mbox_names[] = "rx\0tx";
    err = fdt_setprop(fdt, tcu_off, "mbox-names", mbox_names, sizeof(mbox_names));
    if (err) {
        ZF_LOGE("Failed to set mbox-names: %d", err);
        return err;
    }

    /* Set status property */
    err = fdt_setprop_string(fdt, tcu_off, "status", "okay");
    if (err) {
        ZF_LOGE("Failed to set status: %d", err);
        return err;
    }

    ZF_LOGI("Added /%s node with RX(HSP Top0 0x%x) TX(HSP AON 0x%x)",
            SERIAL_NODE_NAME, hsp_top0_phandle, hsp_aon_phandle);
    return 0;
}

/**
 * Add MGBE VM IRQ configuration node.
 *
 * NVIDIA's nvethernet driver requires nvidia,vm-irq-config phandle
 * pointing to a node with DMA channel to IRQ mapping.
 * Despite "VM" name, this is required even on bare metal.
 *
 * Creates /mgbe-vm-irq-config with one child (vm_irq0) for single
 * DMA channel configuration, and updates ethernet node's phandle reference.
 */
static int fdt_add_mgbe_vm_irq_config(void *fdt)
{
    int err;
    int root, eth_off, vm_cfg_off, vm_irq_off;
    uint32_t vm_cfg_phandle;

    /* Find ethernet node */
    eth_off = fdt_path_offset(fdt, "/bus@0/ethernet@6800000");
    if (eth_off < 0) {
        ZF_LOGI("Ethernet node not found, skipping VM IRQ config");
        return 0;  /* Not an error - ethernet might not be enabled */
    }

    /* Find root */
    root = fdt_path_offset(fdt, "/");
    if (root < 0) {
        ZF_LOGE("Root node not found: %d", root);
        return root;
    }

    /* Create /mgbe-vm-irq-config node */
    vm_cfg_off = fdt_add_subnode(fdt, root, "mgbe-vm-irq-config");
    if (vm_cfg_off < 0) {
        if (vm_cfg_off == -FDT_ERR_EXISTS) {
            vm_cfg_off = fdt_path_offset(fdt, "/mgbe-vm-irq-config");
        } else {
            ZF_LOGE("Failed to add mgbe-vm-irq-config: %d", vm_cfg_off);
            return vm_cfg_off;
        }
    }

    /* Assign phandle to vm-irq-config node */
    vm_cfg_phandle = fdt_get_max_phandle(fdt) + 1;
    err = fdt_setprop_u32(fdt, vm_cfg_off, "phandle", vm_cfg_phandle);
    if (err) {
        ZF_LOGE("Failed to set vm-irq-config phandle: %d", err);
        return err;
    }

    /* Set nvidia,num-vm-irqs = 1 */
    err = fdt_setprop_u32(fdt, vm_cfg_off, "nvidia,num-vm-irqs", 1);
    if (err) {
        ZF_LOGE("Failed to set num-vm-irqs: %d", err);
        return err;
    }

    /* Create child node vm_irq0 */
    vm_irq_off = fdt_add_subnode(fdt, vm_cfg_off, "vm_irq0");
    if (vm_irq_off < 0) {
        ZF_LOGE("Failed to add vm_irq0: %d", vm_irq_off);
        return vm_irq_off;
    }

    /* Set vm_irq0 properties */
    err = fdt_setprop_u32(fdt, vm_irq_off, "nvidia,vm-irq-id", 0);
    if (err) return err;
    err = fdt_setprop_u32(fdt, vm_irq_off, "nvidia,vm-num", 0);
    if (err) return err;
    err = fdt_setprop_u32(fdt, vm_irq_off, "nvidia,num-vm-channels", 1);
    if (err) return err;

    /* nvidia,vm-channels = <0> (single channel) */
    uint32_t channels[1] = { cpu_to_fdt32(0) };
    err = fdt_setprop(fdt, vm_irq_off, "nvidia,vm-channels", channels, sizeof(channels));
    if (err) {
        ZF_LOGE("Failed to set vm-channels: %d", err);
        return err;
    }

    /* Re-lookup ethernet node (offset may have changed after adding nodes) */
    eth_off = fdt_path_offset(fdt, "/bus@0/ethernet@6800000");
    if (eth_off < 0) {
        ZF_LOGE("Ethernet node disappeared: %d", eth_off);
        return eth_off;
    }

    /* Update ethernet node's nvidia,vm-irq-config phandle */
    err = fdt_setprop_u32(fdt, eth_off, "nvidia,vm-irq-config", vm_cfg_phandle);
    if (err) {
        ZF_LOGE("Failed to set ethernet vm-irq-config phandle: %d", err);
        return err;
    }

    ZF_LOGI("Added mgbe-vm-irq-config (phandle 0x%x) with vm_irq0", vm_cfg_phandle);
    return 0;
}

/**
 * Add MAC address to /chosen node for NVIDIA nvethernet driver.
 *
 * The NVIDIA nvethernet driver looks for MAC address at:
 *   /chosen/nvidia,ether-mac<N> where N is the mac-addr-idx from device node.
 *
 * For MGBE0 (mac-addr-idx=0), it looks for /chosen/nvidia,ether-mac0.
 * The property value is a string: "xx:xx:xx:xx:xx:xx"
 */
static int fdt_add_mgbe_mac_address(void *fdt)
{
    int err;
    int chosen_off;

    /* Find or create /chosen node */
    chosen_off = fdt_path_offset(fdt, "/chosen");
    if (chosen_off < 0) {
        /* Create /chosen if it doesn't exist */
        int root = fdt_path_offset(fdt, "/");
        if (root < 0) {
            ZF_LOGE("Root node not found: %d", root);
            return root;
        }
        chosen_off = fdt_add_subnode(fdt, root, "chosen");
        if (chosen_off < 0) {
            ZF_LOGE("Failed to add /chosen node: %d", chosen_off);
            return chosen_off;
        }
        ZF_LOGI("Created /chosen node");
    }

    /* Add nvidia,ether-mac0 property */
    err = fdt_setprop_string(fdt, chosen_off, "nvidia,ether-mac0", MGBE0_MAC_ADDR);
    if (err) {
        ZF_LOGE("Failed to set nvidia,ether-mac0: %d", err);
        return err;
    }

    ZF_LOGI("Set /chosen/nvidia,ether-mac0 = %s", MGBE0_MAC_ADDR);
    return 0;
}

/* Buffer size for DTB expansion - must match DTB_BUFFER_SIZE in main.c (320KB) */
#define FDT_EXTRA_SPACE (128 * 1024)  /* 128KB extra for our additions */

/**
 * Platform-specific DTB customization for Orin AGX.
 *
 * Called after CAmkES generates the base DTB from fdtgen but before fdt_pack().
 * This allows us to add nodes with correct phandle references.
 */
int fdt_plat_customize(vm_t *vm, void *dtb_buf)
{
    int err;
    int current_size = fdt_totalsize(dtb_buf);
    int new_size = current_size + FDT_EXTRA_SPACE;

    /* Expand FDT to make room for new nodes/properties */
    err = fdt_open_into(dtb_buf, dtb_buf, new_size);
    if (err) {
        ZF_LOGE("fdt_open_into failed: %d", err);
        return err;
    }
    ZF_LOGI("Expanded FDT from %d to %d bytes", current_size, new_size);

    /* Set interrupt-parent at root for child interrupt routing */
    err = fdt_set_interrupt_parent(dtb_buf);
    if (err) {
        ZF_LOGE("Cannot set interrupt-parent (%d)", err);
        return -1;
    }

    /* Add TCU serial node with correct HSP AON phandle */
    err = fdt_generate_tcu_node(dtb_buf);
    if (err) {
        ZF_LOGE("Cannot generate TCU serial node (%d)", err);
        return -1;
    }

    /* Add MAC address to /chosen for MGBE0 (nvethernet) */
    err = fdt_add_mgbe_mac_address(dtb_buf);
    if (err) {
        ZF_LOGE("Cannot add MGBE MAC address (%d)", err);
        return -1;
    }

    /* Add MGBE VM IRQ configuration (required by nvethernet driver) */
    err = fdt_add_mgbe_vm_irq_config(dtb_buf);
    if (err) {
        ZF_LOGE("Cannot add MGBE VM IRQ config (%d)", err);
        return -1;
    }

    return 0;
}
