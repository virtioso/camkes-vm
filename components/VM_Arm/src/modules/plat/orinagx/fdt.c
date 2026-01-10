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
#define BUS_PATH            "/bus@0"
#define SERIAL_NODE_NAME    "serial"

#define MGBE0_MAC_ADDR      "48:B0:2D:7F:0C:2A"

/*
 * PMC node location - GPIO driver uses of_find_matching_node() to find any
 * node matching tegra186_pmc_of_match compatible strings, so the location
 * doesn't matter. We put it in /bus@0/ alongside other tegra devices.
 */
#define PMC_NODE_NAME       "pmc"

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
 *
 * @param fdt          Device tree blob
 * @param gic_phandle  Output: GIC phandle for use by other functions
 * @return 0 on success, negative on error
 */
static int fdt_set_interrupt_parent(void *fdt, uint32_t *gic_phandle_out)
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

    if (gic_phandle_out) {
        *gic_phandle_out = gic_phandle;
    }
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
 * Add PMC (Power Management Controller) node for GPIO hierarchical IRQ mode.
 *
 * The Tegra GPIO driver (gpio-tegra186.c) looks for a PMC node to enable
 * hierarchical IRQ domain mode. In this mode, the GPIO driver's translate()
 * callback properly converts DT GPIO numbers (e.g., TEGRA234_MAIN_GPIO(Y,3)=147)
 * to linear hwirq values (e.g., 125).
 *
 * Without PMC, GPIO uses simple IRQ domain with 1:1 xlate, causing hwirq
 * mismatch between IRQ request time (147) and interrupt delivery time (125).
 *
 * The actual PMC functionality (wake events, power management) is not needed
 * in the VM - we just need the IRQ domain to exist. The pmc-irq-domain
 * kernel module provides a dummy IRQ domain that disconnects from hierarchy,
 * allowing GPIO's parent_handler to handle interrupts directly via GIC SPIs.
 *
 * GPIO driver lookup code:
 *   np = of_find_matching_node(NULL, tegra186_pmc_of_match);
 *   if (np && of_device_is_available(np))
 *       irq->parent_domain = irq_find_host(np);  // Defers if NULL
 */
static int fdt_add_pmc_node(void *fdt, uint32_t gic_phandle)
{
    int err;

    /* Find /bus@0 node */
    int bus_off = fdt_path_offset(fdt, BUS_PATH);
    if (bus_off < 0) {
        ZF_LOGE("Bus node not found at %s (err=%d)", BUS_PATH, bus_off);
        return -1;
    }

    /* Add PMC node under /bus@0 */
    int pmc_off = fdt_add_subnode(fdt, bus_off, PMC_NODE_NAME);
    if (pmc_off < 0) {
        if (pmc_off == -FDT_ERR_EXISTS) {
            ZF_LOGI("%s/%s node already exists, skipping", BUS_PATH, PMC_NODE_NAME);
            return 0;
        }
        ZF_LOGE("Failed to add %s/%s node: %d", BUS_PATH, PMC_NODE_NAME, pmc_off);
        return pmc_off;
    }

    /* Set compatible - tegra234-pmc is what GPIO driver looks for on Orin */
    const char compatible[] = "nvidia,tegra234-pmc";
    err = fdt_setprop(fdt, pmc_off, "compatible", compatible, sizeof(compatible));
    if (err) {
        ZF_LOGE("Failed to set PMC compatible: %d", err);
        return err;
    }

    /* Mark as interrupt controller so irq_find_host() creates domain mapping */
    err = fdt_setprop(fdt, pmc_off, "interrupt-controller", NULL, 0);
    if (err) {
        ZF_LOGE("Failed to set PMC interrupt-controller: %d", err);
        return err;
    }

    /* Set #interrupt-cells - PMC accepts 2-cell format from GPIO */
    err = fdt_setprop_u32(fdt, pmc_off, "#interrupt-cells", 2);
    if (err) {
        ZF_LOGE("Failed to set PMC #interrupt-cells: %d", err);
        return err;
    }

    /* Set interrupt-parent to GIC for hierarchical IRQ domain */
    err = fdt_setprop_u32(fdt, pmc_off, "interrupt-parent", gic_phandle);
    if (err) {
        ZF_LOGE("Failed to set PMC interrupt-parent: %d", err);
        return err;
    }

    /* Set status */
    err = fdt_setprop_string(fdt, pmc_off, "status", "okay");
    if (err) {
        ZF_LOGE("Failed to set PMC status: %d", err);
        return err;
    }

    ZF_LOGI("Added %s/%s node for GPIO hierarchical IRQ mode (interrupt-parent=0x%x)",
            BUS_PATH, PMC_NODE_NAME, gic_phandle);
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

/**
 * Platform-specific DTB customization for Orin AGX.
 *
 * Called after CAmkES generates the base DTB from fdtgen but before fdt_pack().
 * This allows us to add nodes with correct phandle references.
 */
int fdt_plat_customize(vm_t *vm, void *dtb_buf)
{
    int err;
    uint32_t gic_phandle = 0;

    /* Set interrupt-parent at root for child interrupt routing */
    err = fdt_set_interrupt_parent(dtb_buf, &gic_phandle);
    if (err) {
        ZF_LOGE("Cannot set interrupt-parent (%d)", err);
        return -1;
    }

    /* Add PMC node for GPIO hierarchical IRQ mode */
    err = fdt_add_pmc_node(dtb_buf, gic_phandle);
    if (err) {
        ZF_LOGE("Cannot add PMC node (%d)", err);
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

    return 0;
}
