/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*The init thread for the vmm system*/

#include <stdio.h>
#include <stdint.h>
#include <autoconf.h>
#include <camkes_vmm/gen_config.h>
#include <utils/util.h>
#include <sel4utils/sel4_zf_logif.h>
#include <sel4platsupport/arch/io.h>
#include <sel4utils/vspace.h>
#include <sel4utils/stack.h>
#include <allocman/utspace/split.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <simple/simple_helpers.h>
#include <utils/util.h>
#include <vka/capops.h>
#include <pci/pci.h>

#include <camkes.h>

#include "camkes_vm_interfaces.h"
#include <sel4vm/guest_vm.h>
#include <sel4vm/boot.h>
#include <sel4vm/guest_memory.h>
#include <sel4vm/guest_memory_helpers.h>
#include <sel4vm/guest_ram.h>
#include <sel4vm/guest_iospace.h>
#include <sel4vm/arch/ioports.h>
#include <sel4vm/guest_irq_controller.h>

#include <sel4vmmplatsupport/gen_config.h>
#include <sel4vmmplatsupport/guest_memory_util.h>
#include <sel4vmmplatsupport/ioports.h>
#include <sel4vmmplatsupport/pci_host_bridge.h>
#include <sel4vmmplatsupport/drivers/pci.h>
#include <sel4vmmplatsupport/drivers/pci_helper.h>
#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>
#include <sel4vmmplatsupport/arch/drivers/vmm_pci_helper.h>

#include <sel4vm/arch/vmcall.h>

#include <sel4vmmplatsupport/guest_image.h>
#include <sel4vmmplatsupport/arch/guest_boot_init.h>
#include <sel4vmmplatsupport/arch/ioport_defs.h>


#include "vm.h"
#include "timers.h"
#include "fsclient.h"
#include "virtio_net.h"
#include "virtio_net_vswitch.h"
#include "virtio_con.h"
#include "virtio_vsock.h"

#ifdef CONFIG_VMM_USE_HPET
#include "hpet.h"
#endif

#define BRK_VIRTUAL_SIZE 400000000
#define ALLOCMAN_VIRTUAL_SIZE 400000000
#define CROSS_VM_EVENT_IRQ_NUM 12
#define CROSS_VM_BASE_ADDRESS 0xa0000000
#define X86_GUEST_KERNEL_LOAD_PADDR 0x1000000

extern void *fs_buf;

extern reservation_t muslc_brk_reservation;
extern void *muslc_brk_reservation_start;
extern vspace_t  *muslc_this_vspace;
static sel4utils_res_t muslc_brk_reservation_memory;

seL4_CPtr intready_notification();

static seL4_CPtr get_async_event_notification()
{
    return intready_notification();
}

void camkes_make_simple(simple_t *simple);

static allocman_t *allocman;
static char allocator_mempool[8886080];
static simple_t camkes_simple;
static seL4_Error (*camkes_original_frame_cap)(void *data, void *paddr, int size_bits,
                                               cspacepath_t *path);
static vka_t vka;
static vspace_t vspace;
static sel4utils_alloc_data_t vspace_data;
struct ps_io_ops io_ops;
static vmm_pci_space_t *pci;
static vmm_pci_raw_io_space_t *physical_pci_raw_io;
static vmm_io_port_list_t *io_ports;

vm_t vm;

extern int camkes_get_untyped_page_bits(uintptr_t addr);
int camkes_cross_vm_connections_init(vm_t *vm, vmm_pci_space_t *pci,
                                     seL4_CPtr irq_notification, uintptr_t connection_base_address) WEAK;

bool vmm_guest_detect_physical_pci_host_bridge(vm_t *vm, vmm_pci_host_bridge_t *bridge)
{
    (void)vm;
    if (!bridge) {
        return false;
    }

#ifdef CONFIG_PLAT_PC99
    libpci_device_t *host_bridge = libpci_find_device_bdf(0, 0, 0);
    if (!host_bridge) {
        return false;
    }

    if (host_bridge->vendor_id == 0x8086 && host_bridge->device_id == 0x29c0) {
        vmm_pci_host_bridge_init_qemu_pc_q35(
            bridge,
            (vmm_pci_host_bridge_region_t) {
                .base = 0xb0000000,
                .size = 0x10000000,
            },
            (vmm_pci_host_bridge_region_t) {
                .base = 0xc0000000,
                .size = 0x3ec00000,
            },
            (vmm_pci_host_bridge_region_t) { 0, 0 }
        );
        return true;
    }

    if (host_bridge->vendor_id == 0x8086 && host_bridge->device_id == 0x1237) {
        vmm_pci_host_bridge_init_qemu_pc_i440fx(
            bridge,
            (vmm_pci_host_bridge_region_t) { 0, 0 },
            (vmm_pci_host_bridge_region_t) { 0, 0 }
        );
        return true;
    }
#endif

    return false;
}

static bool physical_q35_pci_uses_structural_host_bridge(vm_t *vm)
{
    vmm_pci_host_bridge_t bridge;
    vmm_pci_host_bridge_init_empty(&bridge);
    if (!vmm_guest_detect_physical_pci_host_bridge(vm, &bridge)) {
        return false;
    }

    return vmm_pci_host_bridge_region_valid(bridge.mcfg_region) &&
           vmm_pci_host_bridge_region_valid(bridge.mem32_region);
}

static bool physical_q35_needs_synthetic_pci_space(vm_t *vm)
{
    if (!physical_q35_pci_uses_structural_host_bridge(vm)) {
        return true;
    }

    if (init_cons_num_connections() > 0) {
        return true;
    }

    if (camkes_cross_vm_connections_init) {
        return true;
    }

    return false;
}

static vmm_pci_config_t make_camkes_pci_config(void);

static int register_physical_pci_device(vm_t *vm, libpci_device_t *device, int irq)
{
    vmm_pci_entry_t entry = vmm_pci_create_passthrough((vmm_pci_address_t) {
        device->bus, device->dev, device->fun
    }, make_camkes_pci_config());

    vmm_pci_bar_t bars[6];
    int num_bars = vmm_pci_helper_map_bars(vm, &device->cfg, bars);
    if (num_bars < 0) {
        ZF_LOGE("Failed to map BARs for passthrough device %02x:%02x.%u",
                device->bus, device->dev, device->fun);
        return num_bars;
    }
    if (num_bars > 0) {
        entry = vmm_pci_create_bar_emulation(entry, num_bars, bars);
    }
    entry = vmm_pci_create_irq_emulation(entry, irq);
    entry = vmm_pci_no_msi_cap_emulation(entry);

    return vmm_pci_add_entry_at(pci, entry, (vmm_pci_address_t) {
        .bus = device->bus, .dev = device->dev, .fun = device->fun
    });
}

static void reserve_physical_pci_host_apertures(vm_t *vm)
{
    int generated_regions = physical_pci_host_bridge_num_regions();
    if (generated_regions > 0) {
        for (int i = 0; i < generated_regions; i++) {
            uintptr_t base;
            size_t size;
            int page_bits;
            int err = physical_pci_host_bridge_get_region(i, &base, &size, &page_bits);
            ZF_LOGF_IF(err, "Failed to get generated physical PCI host bridge region %d", i);

            vm_memory_reservation_t *reservation =
                vm_reserve_memory_at(vm, base, size, default_error_fault_callback, NULL);
            ZF_LOGF_IF(!reservation,
                       "Failed to reserve generated physical PCI host bridge region at 0x%lx size 0x%zx",
                       (unsigned long)base, size);
            err = map_ut_alloc_reservation_with_base_paddr(vm, base, reservation);
            ZF_LOGF_IF(err,
                       "Failed to map generated physical PCI host bridge region at 0x%lx size 0x%zx",
                       (unsigned long)base, size);
            ZF_LOGI("Reserved generated physical PCI host bridge region 0x%lx size 0x%zx page_bits=%d",
                    (unsigned long)base, size, page_bits);
        }
        return;
    }

    vmm_pci_host_bridge_t bridge;
    vmm_pci_host_bridge_init_empty(&bridge);
    if (!vmm_guest_detect_physical_pci_host_bridge(vm, &bridge)) {
        return;
    }

    vmm_pci_host_bridge_region_t regions[] = {
        bridge.mcfg_region,
        bridge.mem32_region,
    };
    const char *region_names[] = {
        "config",
        "mem32",
    };

    for (size_t i = 0; i < ARRAY_SIZE(regions); i++) {
        if (!vmm_pci_host_bridge_region_valid(regions[i])) {
            continue;
        }

        int page_bits = camkes_get_untyped_page_bits(regions[i].base);
        if (page_bits == 0) {
            page_bits = seL4_PageBits;
        }
        size_t reserve_size = MIN(regions[i].size, BIT(page_bits));
        vm_memory_reservation_t *reservation =
            vm_reserve_memory_at(vm, regions[i].base, reserve_size,
                                 default_error_fault_callback, NULL);
        ZF_LOGF_IF(!reservation, "Failed to reserve PCI %s aperture at 0x%lx size 0x%zx",
                   region_names[i], (unsigned long)regions[i].base, reserve_size);
        int err = map_ut_alloc_reservation(vm, reservation);
        ZF_LOGF_IF(err, "Failed to map PCI %s aperture at 0x%lx size 0x%zx",
                   region_names[i], (unsigned long)regions[i].base, reserve_size);
        ZF_LOGI("Reserved physical PCI %s aperture 0x%lx size 0x%zx",
                region_names[i], (unsigned long)regions[i].base, reserve_size);
    }
}

int get_crossvm_irq_num(void)
{
    return CROSS_VM_EVENT_IRQ_NUM;
}

static seL4_Error simple_ioport_wrapper(void *data, uint16_t start_port, uint16_t end_port,
                                        seL4_Word root, seL4_Word dest, seL4_Word depth)
{
    seL4_CPtr cap = ioports_get_ioport(start_port, end_port);
    if (cap == seL4_CapNull) {
        return seL4_FailedLookup;
    }
    return seL4_CNode_Copy(root, dest, depth, root, cap, CONFIG_WORD_SIZE, seL4_AllRights);

}

static seL4_Error simple_frame_cap_wrapper(void *data, void *paddr, int size_bits, cspacepath_t *path)
{
    seL4_CPtr cap = pci_devices_get_device_mem_frame((uintptr_t)paddr);
    if (cap != 0) {
        vka_cspace_make_path(&vka, cap, path);
        return 0;
    }

    cap = physical_pci_host_bridge_get_mem_frame((uintptr_t)paddr);
    if (cap != 0) {
        vka_cspace_make_path(&vka, cap, path);
        return 0;
    }

    /* Check whether it is a guest mapped region. */
    cap = guest_mappings_get_mapping_mem_frame((uintptr_t)paddr);
    if (cap != 0) {
        ZF_LOGI("Guest map found at %p\n", paddr);
        vka_cspace_make_path(&vka, cap, path);
        return 0;
    }

    if (camkes_original_frame_cap) {
        return camkes_original_frame_cap(data, paddr, size_bits, path);
    }

    return -1;
}

void pit_pre_init(void);
void rtc_pre_init(void);
void serial_pre_init(void);

void pre_init(void)
{
    int error;

    set_putchar(putchar_putchar);

    /* Camkes adds nothing to our address space, so this array is empty */
    void *existing_frames[] = {
        NULL
    };
    camkes_make_simple(&camkes_simple);
    camkes_simple.arch_simple.IOPort_cap = simple_ioport_wrapper;
    camkes_original_frame_cap = camkes_simple.frame_cap;
    camkes_simple.frame_cap = simple_frame_cap_wrapper;

    /* Initialize allocator */
    allocman = bootstrap_use_current_1level(
                   simple_get_cnode(&camkes_simple),
                   simple_get_cnode_size_bits(&camkes_simple),
                   simple_last_valid_cap(&camkes_simple) + 1,
                   BIT(simple_get_cnode_size_bits(&camkes_simple)),
                   sizeof(allocator_mempool), allocator_mempool
               );
    ZF_LOGF_IF(allocman == NULL, "Failed to create allocman");

    error = allocman_add_simple_untypeds(allocman, &camkes_simple);
    ZF_LOGF_IF(error, "Failed to add untypeds to allocman");

    allocman_make_vka(&vka, allocman);

    /* Initialize the vspace */
    error = sel4utils_bootstrap_vspace(&vspace, &vspace_data,
                                       simple_get_init_cap(&camkes_simple, seL4_CapInitThreadPD), &vka, NULL, NULL, existing_frames);
    ZF_LOGF_IF(error, "Failed to bootstrap vspace");

    /* Create virtual pool */
    reservation_t pool_reservation;
    void *vaddr;
    pool_reservation.res = allocman_mspace_alloc(allocman, sizeof(sel4utils_res_t), &error);
    if (!pool_reservation.res) {
        ZF_LOGF("Failed to allocate reservation");
    }
    error = sel4utils_reserve_range_no_alloc(&vspace, pool_reservation.res,
                                             ALLOCMAN_VIRTUAL_SIZE, seL4_AllRights, 1, &vaddr);
    if (error) {
        ZF_LOGF("Failed to provide virtual memory allocator");
    }
    bootstrap_configure_virtual_pool(allocman, vaddr, ALLOCMAN_VIRTUAL_SIZE, simple_get_init_cap(&camkes_simple,
                                                                                                 seL4_CapInitThreadPD));

    /* Add additional untypeds that make up extra RAM */
    int num = ram_num_untypeds();

    for (int i = 0; i < num; i++) {
        cspacepath_t path;
        seL4_CPtr cap;
        uintptr_t paddr;
        int size_bits;
        size_t sz_size_bits;
        ram_get_untyped(i, &paddr, &size_bits, &cap);
        sz_size_bits = size_bits;
        vka_cspace_make_path(&vka, cap, &path);
        error = allocman_utspace_add_uts(allocman, 1, &path, &sz_size_bits, &paddr, ALLOCMAN_UT_DEV_MEM);
        ZF_LOGF_IF(error, "Failed to add device mem uts to allocman");
    }

    /* add untyped mmios */
    int cnt = simple_get_untyped_count(&camkes_simple);
    ZF_LOGF_IF(cnt < 0, "Failed to get simple untyped count (%d)", cnt);
    for (int i = 0; i < cnt; i++) {
        size_t size;
        uintptr_t paddr;
        bool device;
        seL4_CPtr cap = simple_get_nth_untyped(&camkes_simple, i, &size, &paddr, &device);
        if (device) {
            cspacepath_t path;
            vka_cspace_make_path(&vka, cap, &path);
            error = allocman_utspace_add_uts(allocman, 1, &path, &size, &paddr, ALLOCMAN_UT_DEV);
            ZF_LOGF_IF(error, "Failed to add MMIO uts allocman");
        }
    }

    sel4utils_reserve_range_no_alloc(&vspace, &muslc_brk_reservation_memory, BRK_VIRTUAL_SIZE, seL4_AllRights, 1,
                                     &muslc_brk_reservation_start);
    muslc_this_vspace = &vspace;
    muslc_brk_reservation = (reservation_t) {
        .res = &muslc_brk_reservation_memory
    };

}

typedef struct memory_range {
    uintptr_t base;
    size_t size;
} memory_range_t;

static memory_range_t guest_ram_regions[] = {
    /* Allocate all the standard low memory areas */
    /* On x86 the BIOS loads the MBR to 0x7c00. But for this VMM,
     * we don't use MBR, so there is no need to exclude the MBR
     * bootstrap code region. Keep the mapped regions page-aligned because
     * guest RAM registration allocates and maps whole 4 KiB frames. */
    {0x1000, 0x80000 - 0x1000},
    {0x80000, 0x9f000 - 0x80000},
};

static memory_range_t guest_fake_devices[] = {
    {0xf0000, 0x10000}, // DMI
    {0xc0000, 0xc8000 - 0xc0000}, // VIDEO BIOS
    {0xc8000, 0xe0000 - 0xc8000}, // Mapped hardware and MISC
};

/* Memory areas we reserve for anonymous allocations */
static memory_range_t free_anonymous_regions[] = {
    {0x100000, 0x10000000 - 0x100000},
    {0x10003000, 0xa0000000 - 0x10003000},
};

typedef struct device_notify {
    uint32_t badge;
    /* the function (as described by the user in the configuration) to call
     * when the message has been received. */
    void (*func)(vm_t *vm);
} device_notify_t;

#define PHYSICAL_PCI_DEVICE_OWNER_NONE   0
#define PHYSICAL_PCI_DEVICE_OWNER_GUEST  1
#define PHYSICAL_PCI_DEVICE_OWNER_NATIVE 2

static bool physical_pci_device_guest_visible(uint8_t bus, uint8_t dev, uint8_t fun)
{
    if (physical_pci_devices_num_devices() == 0) {
        return true;
    }

    int owner = physical_pci_devices_get_owner(bus, dev, fun);
    if (owner == PHYSICAL_PCI_DEVICE_OWNER_NONE) {
        return true;
    }

    return owner == PHYSICAL_PCI_DEVICE_OWNER_GUEST;
}

/* Wrappers for passing PCI config space calls to camkes */
static uint8_t camkes_pci_read8(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    if (!physical_pci_device_guest_visible(addr.bus, addr.dev, addr.fun)) {
        return UINT8_MAX;
    }
    return pci_config_read8(addr.bus, addr.dev, addr.fun, offset);
}
static uint16_t camkes_pci_read16(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    if (!physical_pci_device_guest_visible(addr.bus, addr.dev, addr.fun)) {
        return UINT16_MAX;
    }
    return pci_config_read16(addr.bus, addr.dev, addr.fun, offset);
}
static uint32_t camkes_pci_read32(void *cookie, vmm_pci_address_t addr, unsigned int offset)
{
    if (!physical_pci_device_guest_visible(addr.bus, addr.dev, addr.fun)) {
        return UINT32_MAX;
    }
    return pci_config_read32(addr.bus, addr.dev, addr.fun, offset);
}
static void camkes_pci_write8(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint8_t val)
{
    if (!physical_pci_device_guest_visible(addr.bus, addr.dev, addr.fun)) {
        return;
    }
    pci_config_write8(addr.bus, addr.dev, addr.fun, offset, val);
}
static void camkes_pci_write16(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint16_t val)
{
    if (!physical_pci_device_guest_visible(addr.bus, addr.dev, addr.fun)) {
        return;
    }
    pci_config_write16(addr.bus, addr.dev, addr.fun, offset, val);
}
static void camkes_pci_write32(void *cookie, vmm_pci_address_t addr, unsigned int offset, uint32_t val)
{
    if (!physical_pci_device_guest_visible(addr.bus, addr.dev, addr.fun)) {
        return;
    }
    pci_config_write32(addr.bus, addr.dev, addr.fun, offset, val);
}

vmm_pci_config_t make_camkes_pci_config()
{
    return (vmm_pci_config_t) {
        .cookie = NULL,
        .ioread8 = camkes_pci_read8,
        .ioread16 = camkes_pci_read16,
        .ioread32 = camkes_pci_read32,
        .iowrite8 = camkes_pci_write8,
        .iowrite16 = camkes_pci_write16,
        .iowrite32 = camkes_pci_write32
    };
}

typedef struct ioport_desc {
    uint16_t start_port;
    uint16_t end_port;
    vm_ioport_in_fn port_in;
    vm_ioport_out_fn port_out;
    const char *desc;
} ioport_desc_t;

ioport_fault_result_t i8254_port_in(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                    unsigned int *result);
ioport_fault_result_t i8254_port_out(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                     unsigned int value);
ioport_fault_result_t speaker_port_in(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                      unsigned int *result);
ioport_fault_result_t speaker_port_out(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                       unsigned int value);

ioport_fault_result_t cmos_port_in(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                   unsigned int *result);
ioport_fault_result_t cmos_port_out(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                    unsigned int value);

ioport_fault_result_t serial_port_in(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                     unsigned int *result);
ioport_fault_result_t serial_port_out(vm_vcpu_t *vcpu, void *cookie, unsigned int port_no, unsigned int size,
                                      unsigned int value);

ioport_desc_t ioport_handlers[] = {
    {X86_IO_SERIAL_1_START,   X86_IO_SERIAL_1_END,   serial_port_in, serial_port_out, "COM1 Serial Port"},
//    {X86_IO_SERIAL_3_START,   X86_IO_SERIAL_3_END,   NULL, NULL, "COM3 Serial Port"},
    /* PCI config requires a cookie and is specced dynamically in code */
//    {X86_IO_PCI_CONFIG_START, X86_IO_PCI_CONFIG_END, vmm_pci_io_port_in, vmm_pci_io_port_out, "PCI Configuration"},
    {X86_IO_RTC_START,        X86_IO_RTC_END,        cmos_port_in, cmos_port_out, "CMOS Registers / RTC Real-Time Clock / NMI Interrupts"},
    {X86_IO_PIT_START,        X86_IO_PIT_END,        i8254_port_in, i8254_port_out, "8253/8254 Programmable Interval Timer"},
    {0x61,                    0x61,                  speaker_port_in, speaker_port_out, "PC Speaker / PIT Channel 2 Gate"},
//    {X86_IO_PS2C_START,       X86_IO_PS2C_END,       NULL, NULL, "8042 PS/2 Controller"},
//    {X86_IO_POS_START,        X86_IO_POS_END,        NULL, NULL, "POS Programmable Option Select (PS/2)"},

#if 0
    {0xC000,                  0xF000,                NULL, NULL, "PCI Bus IOPort Mapping Space"},
    {0x1060,                  0x1070,                NULL, NULL, "IDE controller"},
    {0x01F0,                  0x01F8,                NULL, NULL, "Primary IDE controller"},
    {0x0170,                  0x0178,                NULL, NULL, "Secondary IDE controller"},
    {0x3f6,                   0x03f7,                NULL, NULL, "Additional ATA register"},
    {0x376,                   0x0377,                NULL, NULL, "Additional ATA register"},
    {0x3b0,                   0x3df,                 NULL, NULL, "IBM VGA"},

    {0x80,                    0x80,                  NULL, NULL, "DMA IOPort timer"},
#endif

#if 0
    {0x164e,                  0x164f,                NULL, NULL, "Serial Configuration Registers"},
#endif
};

int pci_config_io_in(void *cookie, uint32_t port, int io_size, uint32_t *result)
{
    uint32_t *conf_port_addr = (uint32_t *)cookie;
    uint8_t offset;
    if (port >= PCI_CONF_PORT_ADDR && port < PCI_CONF_PORT_ADDR_END) {
        offset = port - PCI_CONF_PORT_ADDR;
        /* Emulate read addr */
        *result = 0;
        memcpy(result, ((char *)conf_port_addr) + offset, io_size);
        return 0;
    }
    if (port < PCI_CONF_PORT_DATA || port + io_size > PCI_CONF_PORT_DATA_END) {
        return -1;
    }
    offset = port - PCI_CONF_PORT_DATA;
    /* Make an address out of the config */
    uint8_t bus, dev, fun, reg;
    bus = ((*conf_port_addr) >> 16) & MASK(8);
    dev = ((*conf_port_addr) >> 11) & MASK(5);
    fun = ((*conf_port_addr) >> 8) & MASK(3);
    reg = (*conf_port_addr) & MASK(8);
    reg += offset;
    /* Read the real config */
    switch (io_size) {
    case 1:
        *result = pci_config_read8(bus, dev, fun, reg);
        break;
    case 2:
        *result = pci_config_read16(bus, dev, fun, reg);
        break;
    case 4:
        *result = pci_config_read32(bus, dev, fun, reg);
        break;
    default:
        ZF_LOGF("Invalid size");
        return -1;
    }
    return 0;
}

int pci_config_io_out(void *cookie, uint32_t port, int io_size, uint32_t val)
{
    uint32_t *conf_port_addr = (uint32_t *)cookie;
    uint8_t offset;
    if (port >= PCI_CONF_PORT_ADDR && port < PCI_CONF_PORT_ADDR_END) {
        offset = port - PCI_CONF_PORT_ADDR;
        /* Emulate read addr */
        val &= ~MASK(2);
        memcpy(((char *)conf_port_addr) + offset, &val, io_size);
        return 0;
    }
    if (port < PCI_CONF_PORT_DATA || port + io_size > PCI_CONF_PORT_DATA_END) {
        return -1;
    }
    offset = port - PCI_CONF_PORT_DATA;
    /* Make an address out of the config */
    uint8_t bus, dev, fun, reg;
    bus = ((*conf_port_addr) >> 16) & MASK(8);
    dev = ((*conf_port_addr) >> 11) & MASK(5);
    fun = ((*conf_port_addr) >> 8) & MASK(3);
    reg = (*conf_port_addr) & MASK(8);
    reg += offset;
    /* Read the real config */
    switch (io_size) {
    case 1:
        pci_config_write8(bus, dev, fun, reg, val);
        break;
    case 2:
        pci_config_write16(bus, dev, fun, reg, val);
        break;
    case 4:
        pci_config_write32(bus, dev, fun, reg, val);
        break;
    default:
        ZF_LOGF("Invalid size");
        return -1;
    }
    return 0;
}

ps_io_port_ops_t make_pci_io_ops()
{
    return (ps_io_port_ops_t) {
        .cookie = malloc(sizeof(uint32_t)),
        .io_port_in_fn = pci_config_io_in,
        .io_port_out_fn = pci_config_io_out
    };
}

static int device_notify_list_len = 0;
static device_notify_t *device_notify_list = NULL;

void pit_timer_interrupt(void);
void rtc_timer_interrupt(uint32_t);
void serial_timer_interrupt(uint32_t);

#ifdef CONFIG_VMM_USE_HPET
void hpet_timer_interrupt(uint32_t);
#endif

static seL4_Word irq_badges[16] = {
    VM_PIC_BADGE_IRQ_0,
    VM_PIC_BADGE_IRQ_1,
    VM_PIC_BADGE_IRQ_2,
    VM_PIC_BADGE_IRQ_3,
    VM_PIC_BADGE_IRQ_4,
    VM_PIC_BADGE_IRQ_5,
    VM_PIC_BADGE_IRQ_6,
    VM_PIC_BADGE_IRQ_7,
    VM_PIC_BADGE_IRQ_8,
    VM_PIC_BADGE_IRQ_9,
    VM_PIC_BADGE_IRQ_10,
    VM_PIC_BADGE_IRQ_11,
    VM_PIC_BADGE_IRQ_12,
    VM_PIC_BADGE_IRQ_13,
    VM_PIC_BADGE_IRQ_14,
    VM_PIC_BADGE_IRQ_15
};

void serial_character_interrupt(void);

/* These symbols are generated by the timer and serial connections to provide
 * the badge number notifications will have when received from either the timer
 * or serial.
 */
extern seL4_Word init_timer_notification_badge(void);
extern seL4_Word serial_getchar_notification_badge(void);
static unsigned timer_badge_log_count;
static unsigned host_irq_log_count;
static unsigned host_irq_ack_log_count;

static int handle_async_event(vm_t *vm, seL4_Word badge, UNUSED seL4_MessageInfo_t tag, void *cookie)
{
    if (badge & BIT(27)) {
        if ((badge & init_timer_notification_badge()) == init_timer_notification_badge()) {
            uint32_t completed = init_timer_completed();
            if (timer_badge_log_count < 32) {
                ZF_LOGE("Init timer badge completed=0x%x", completed);
                timer_badge_log_count++;
            }
            if (completed & BIT(TIMER_PIT)) {
                pit_timer_interrupt();
            }
            if (completed & (BIT(TIMER_PERIODIC_TIMER) | BIT(TIMER_COALESCED_TIMER) | BIT(TIMER_SECOND_TIMER) | BIT(
                                 TIMER_SECOND_TIMER2))) {
                rtc_timer_interrupt(completed);
            }
            if (completed & (BIT(TIMER_FIFO_TIMEOUT) | BIT(TIMER_TRANSMIT_TIMER) | BIT(TIMER_MODEM_STATUS_TIMER) | BIT(
                                 TIMER_MORE_CHARS))) {
                serial_timer_interrupt(completed);
            }
#ifdef CONFIG_VMM_USE_HPET
            if (completed & (BIT(TIMER_HPET0) | BIT(TIMER_HPET1) | BIT(TIMER_HPET2))) {
                hpet_timer_interrupt(completed);
            }
#endif
        }
        if ((badge & serial_getchar_notification_badge()) == serial_getchar_notification_badge()) {
            serial_character_interrupt();
        }
        for (int i = 0; i < 16; i++) {
            if ((badge & irq_badges[i]) == irq_badges[i]) {
                if (host_irq_log_count < 32) {
                    ZF_LOGE("External IRQ badge irq=%d", i);
                    host_irq_log_count++;
                }
                vm_inject_irq(vm->vcpus[BOOT_VCPU], i);
            }
        }
        for (int i = 0; i < device_notify_list_len; i++) {
            uint32_t device_badge = device_notify_list[i].badge;
            if ((badge & device_badge) == device_badge) {
                ZF_LOGF_IF(device_notify_list[i].func == NULL, "Undefined notify func");
                device_notify_list[i].func(vm);
            }
        }
    }
    return 0;
}

static seL4_CPtr create_async_event_notification_cap(vm_t *vm, seL4_Word badge)
{

    if (!(badge & BIT(27))) {
        ZF_LOGE("Invalid badge");
        return seL4_CapNull;
    }

    // notification cap
    seL4_CPtr ntfn = intready_notification();

    // path to notification cap slot
    cspacepath_t ntfn_path;
    vka_cspace_make_path(vm->vka, ntfn, &ntfn_path);

    // allocate slot to store copy
    cspacepath_t minted_ntfn_path = {};
    vka_cspace_alloc_path(vm->vka, &minted_ntfn_path);

    // mint the notification cap
    int error = vka_cnode_mint(&minted_ntfn_path, &ntfn_path, seL4_AllRights, badge);

    if (error != seL4_NoError) {
        ZF_LOGE("Failed to mint notification cap");
        return seL4_CapNull;
    }

    return minted_ntfn_path.capPtr;
}

static void irq_ack_hw_irq_handler(vm_vcpu_t *vcpu, int irq, void *cookie)
{
    seL4_CPtr handler = (seL4_CPtr) cookie;
    if (host_irq_ack_log_count < 32) {
        ZF_LOGE("IRQ ack handler irq=%d cap=%lu", irq, (unsigned long)handler);
        host_irq_ack_log_count++;
    }
    int UNUSED error = seL4_IRQHandler_Ack(handler);
    assert(!error);
}

typedef struct runtime_irq_binding {
    bool in_use;
    uint8_t ioapic;
    uint8_t source;
    uint8_t dest;
    seL4_CPtr irq_handler;
} runtime_irq_binding_t;

static runtime_irq_binding_t runtime_irq_bindings[PCI_MAX_DEVICES];
static size_t runtime_irq_bindings_len;

static int bind_vm_irq_handler(vm_t *vm, seL4_CPtr irq_handler, uint8_t dest)
{
    int error;
    cspacepath_t badge_path;
    cspacepath_t async_path;

    if (dest >= ARRAY_SIZE(irq_badges)) {
        ZF_LOGE("IRQ dest %u is out of supported badge range", dest);
        return -1;
    }

    vka_cspace_make_path(&vka, intready_notification(), &async_path);
    error = vka_cspace_alloc_path(&vka, &badge_path);
    ZF_LOGF_IF(error, "Failed to alloc cspace path");

    error = vka_cnode_mint(&badge_path, &async_path, seL4_AllRights, irq_badges[dest]);
    ZF_LOGF_IF(error, "Failed to mint cnode");
    error = seL4_IRQHandler_SetNotification(irq_handler, badge_path.capPtr);
    ZF_LOGF_IF(error, "Failed to set notification for irq handler");
    error = seL4_IRQHandler_Ack(irq_handler);
    ZF_LOGF_IF(error, "Failed to ack irq handler");
    error = vm_register_irq(vm->vcpus[BOOT_VCPU], dest, irq_ack_hw_irq_handler, (void *)irq_handler);
    ZF_LOGF_IF(error, "Failed to register irq ack handler");

    return 0;
}

static void init_irqs(vm_t *vm)
{
    int error UNUSED;

    int num_irqs = irqs_num_irqs();

    if (camkes_cross_vm_connections_init && num_irqs > get_crossvm_irq_num()) {
        ZF_LOGE("Cross vm event irq number not available");
    }

    for (int i = 0; i < num_irqs; i++) {
        seL4_CPtr irq_handler;
        uint8_t ioapic;
        uint8_t source;
        int level_trig;
        int active_low;
        uint8_t dest;
        irqs_get_irq(i, &irq_handler, &ioapic, &source, &level_trig, &active_low, &dest);
        error = bind_vm_irq_handler(vm, irq_handler, dest);
        ZF_LOGF_IF(error, "Failed to bind vm irq handler");
    }
}

static bool pci_device_is_configured(uint8_t bus, uint8_t dev, uint8_t fun)
{
    for (int i = 0; i < pci_devices_num_devices(); i++) {
        uint8_t cfg_bus;
        uint8_t cfg_dev;
        uint8_t cfg_fun;
        seL4_CPtr iospace_cap UNUSED;
        pci_devices_get_device(i, &cfg_bus, &cfg_dev, &cfg_fun, &iospace_cap);
        if (cfg_bus == bus && cfg_dev == dev && cfg_fun == fun) {
            return true;
        }
    }
    return false;
}

static int ensure_runtime_ioapic_irq(vm_t *vm, uint8_t ioapic, uint8_t source,
                                     int level_trig, int active_low, uint8_t dest)
{
    for (int i = 0; i < irqs_num_irqs(); i++) {
        seL4_CPtr irq_handler UNUSED;
        uint8_t cfg_ioapic;
        uint8_t cfg_source;
        int cfg_level_trig;
        int cfg_active_low;
        uint8_t cfg_dest;
        irqs_get_irq(i, &irq_handler, &cfg_ioapic, &cfg_source, &cfg_level_trig, &cfg_active_low, &cfg_dest);
        if (cfg_ioapic == ioapic && cfg_source == source && cfg_dest == dest) {
            return 0;
        }
    }

    for (size_t i = 0; i < runtime_irq_bindings_len; i++) {
        runtime_irq_binding_t *binding = &runtime_irq_bindings[i];
        if (binding->in_use && binding->ioapic == ioapic &&
            binding->source == source && binding->dest == dest) {
            return 0;
        }
    }

    for (int i = 0; i < physical_pci_irqs_num_irqs(); i++) {
        seL4_CPtr irq_handler;
        uint8_t cfg_ioapic;
        uint8_t cfg_source;
        int cfg_level_trig;
        int cfg_active_low;
        uint8_t cfg_dest;
        if (physical_pci_irqs_get_irq(i, &irq_handler, &cfg_ioapic, &cfg_source,
                                      &cfg_level_trig, &cfg_active_low, &cfg_dest) != 0) {
            continue;
        }
        if (cfg_ioapic == ioapic && cfg_source == source && cfg_dest == dest) {
            if (runtime_irq_bindings_len >= ARRAY_SIZE(runtime_irq_bindings)) {
                ZF_LOGE("Out of runtime IRQ binding slots");
                return -1;
            }
            int error = bind_vm_irq_handler(vm, irq_handler, dest);
            if (error) {
                ZF_LOGE("Failed to bind generated physical PCI irq ioapic=%u pin=%u dest=%u",
                        ioapic, source, dest);
                return error;
            }
            runtime_irq_bindings[runtime_irq_bindings_len++] = (runtime_irq_binding_t) {
                .in_use = true,
                .ioapic = ioapic,
                .source = source,
                .dest = dest,
                .irq_handler = irq_handler,
            };
            return 0;
        }
    }

    if (runtime_irq_bindings_len >= ARRAY_SIZE(runtime_irq_bindings)) {
        ZF_LOGE("Out of runtime IRQ binding slots");
        return -1;
    }

    cspacepath_t irq_path;
    int error = vka_cspace_alloc_path(&vka, &irq_path);
    if (error) {
        ZF_LOGE("Failed to alloc cspace path for runtime IOAPIC irq");
        return error;
    }

    if (camkes_simple.arch_simple.ioapic) {
        error = arch_simple_get_ioapic(&camkes_simple.arch_simple, irq_path, ioapic, source,
                                       level_trig, active_low, dest);
    } else {
        error = simple_get_IRQ_handler(&camkes_simple, source, irq_path);
        if (!error) {
            ZF_LOGE("Falling back to legacy IRQ handler for runtime PCI irq source=%u dest=%u", source, dest);
        }
    }
    if (error) {
        ZF_LOGE("Failed to allocate runtime IRQ ioapic=%u pin=%u source=%u dest=%u", ioapic, source, source, dest);
        vka_cspace_free_path(&vka, irq_path);
        return error;
    }

    error = bind_vm_irq_handler(vm, irq_path.capPtr, dest);
    if (error) {
        ZF_LOGE("Failed to bind runtime IOAPIC irq ioapic=%u pin=%u dest=%u", ioapic, source, dest);
        return error;
    }

    runtime_irq_bindings[runtime_irq_bindings_len++] = (runtime_irq_binding_t) {
        .in_use = true,
        .ioapic = ioapic,
        .source = source,
        .dest = dest,
        .irq_handler = irq_path.capPtr,
    };

    return 0;
}

static bool qemu_auto_passthrough_candidate(const libpci_device_t *device)
{
#ifdef CONFIG_PLAT_PC99
    return device->vendor_id == 0x1af4;
#else
    return false;
#endif
}

static int auto_register_qemu_pci_passthrough(vm_t *vm)
{
    for (uint32_t pci_idx = 0; pci_idx < libpci_num_devices; pci_idx++) {
        libpci_device_t *device = &libpci_device_list[pci_idx];
        if (!qemu_auto_passthrough_candidate(device)) {
            continue;
        }
        if (pci_device_is_configured(device->bus, device->dev, device->fun)) {
            continue;
        }
        if (device->interrupt_pin == 0 || device->interrupt_line == 0xff) {
            ZF_LOGE("Skipping auto passthrough for %02x:%02x.%u without usable INTx",
                    device->bus, device->dev, device->fun);
            continue;
        }

        uint8_t dest = device->interrupt_line;
        int error = ensure_runtime_ioapic_irq(vm, 0, device->interrupt_line, 1, 1, dest);
        if (error) {
            return error;
        }

        error = register_physical_pci_device(vm, device, dest);
        if (error) {
            ZF_LOGE("Failed to add auto passthrough device %02x:%02x.%u",
                    device->bus, device->dev, device->fun);
            return error;
        }

        ZF_LOGE("Auto passthrough PCI device bdf=%02x:%02x.%u vid=%04x did=%04x irq=%u mode=%s",
                device->bus, device->dev, device->fun,
                device->vendor_id, device->device_id, dest, "synthetic");
    }

    return 0;
}

static int bind_generated_physical_pci_irqs(vm_t *vm)
{
    for (int i = 0; i < physical_pci_irqs_num_irqs(); i++) {
        seL4_CPtr irq_handler;
        uint8_t ioapic;
        uint8_t source;
        int level_trig;
        int active_low;
        uint8_t dest;
        if (physical_pci_irqs_get_irq(i, &irq_handler, &ioapic, &source,
                                      &level_trig, &active_low, &dest) != 0) {
            continue;
        }

        int error = ensure_runtime_ioapic_irq(vm, ioapic, source, level_trig, active_low, dest);
        if (error) {
            ZF_LOGE("Failed to bind generated physical PCI irq ioapic=%u pin=%u dest=%u",
                    ioapic, source, dest);
            return error;
        }
    }

    return 0;
}


void init_con_irq_init(void)
{
    int i;
    int irqs = 0;
    uintptr_t badge;
    uintptr_t fun;
    for (i = 0; i < init_cons_num_connections(); i++) {
        if (init_cons_has_interrupt(i, &badge, &fun)) {
            irqs++;
        }
    }
    device_notify_list_len = irqs;
    device_notify_list = malloc(sizeof(*device_notify_list) * irqs);
    ZF_LOGF_IF(device_notify_list == NULL, "Malloc failed");

    int notify_idx = 0;
    for (i = 0; i < init_cons_num_connections(); i++) {
        if (init_cons_has_interrupt(i, &badge, &fun)) {
            device_notify_list[notify_idx].badge = badge;
            device_notify_list[notify_idx].func = (void (*)(vm_t *))fun;
            notify_idx++;
        }
    }
    assert(notify_idx == irqs);
}

ioport_fault_result_t ioport_callback_handler(vm_vcpu_t *vcpu, unsigned int port_no, bool is_in, unsigned int *value,
                                              size_t size, void *cookie)
{
    ioport_fault_result_t result;
    int res = emulate_io_handler(io_ports, port_no, is_in, size, value);
    switch (res) {
    case 0:
        result = IO_FAULT_HANDLED;
        break;
    case 1:
        result = IO_FAULT_UNHANDLED;
        break;
    default: /*-1*/
        result = IO_FAULT_ERROR;
    }
    return result;
}

void *main_continued(void *arg)
{
    int error;
    int i;
    int have_initrd = 0;
    ps_io_port_ops_t pci_io_ops;

    rtc_time_date_t time_date = system_rtc_time_date();
    ZF_LOGI("Starting VM %s at: %04d:%02d:%02d %02d:%02d:%02d\n", get_instance_name(), time_date.year, time_date.month,
            time_date.day, time_date.hour, time_date.minute, time_date.second);

    pci_io_ops = make_pci_io_ops();

    ZF_LOGI("PCI scan");
    libpci_scan(pci_io_ops);
    for (uint32_t pci_idx = 0; pci_idx < libpci_num_devices; pci_idx++) {
        libpci_device_t *device = &libpci_device_list[pci_idx];
        ZF_LOGE(
            "PCI device idx=%u bdf=%02x:%02x.%u vid=%04x did=%04x line=%u pin=%u",
            pci_idx,
            device->bus,
            device->dev,
            device->fun,
            device->vendor_id,
            device->device_id,
            device->interrupt_line,
            device->interrupt_pin
        );
        for (int bar_idx = 0; bar_idx < 6; bar_idx++) {
            if (!device->cfg.base_addr_size[bar_idx]) {
                continue;
            }
            ZF_LOGE(
                "PCI BAR idx=%u bar=%d addr=0x%lx size=0x%lx io=%u type=%u",
                pci_idx,
                bar_idx,
                (unsigned long)device->cfg.base_addr[bar_idx],
                (unsigned long)device->cfg.base_addr_size[bar_idx],
                (unsigned int)(device->cfg.base_addr_space[bar_idx] == PCI_BASE_ADDRESS_SPACE_IO),
                (unsigned int)device->cfg.base_addr_type[bar_idx]
            );
        }
    }

    /* install custom open/close/read implementations to redirect I/O from the VMM to
     * our file server */
    install_fileserver(FILE_SERVER_INTERFACE(fs));

    error = ps_new_stdlib_malloc_ops(&io_ops.malloc_ops);
    ZF_LOGF_IF(error, "malloc ops init failed");

    seL4_CPtr ready_notification_cap = intready_notification();
    /* Construct a new VM */
    ZF_LOGI("VMM init");
    error = vm_init(&vm, &vka, &camkes_simple, vspace, &io_ops, ready_notification_cap, "X86 VM");
    ZF_LOGF_IF(error, "VMM init failed");

#ifdef CONFIG_CAMKES_VM_GUEST_DMA_IOMMU
    /* Do early device discovery and find any relevant PCI busses that
     * need to get added */
    ZF_LOGI("PCI early device discovery");
    for (i = 0; i < pci_devices_num_devices(); i++) {
        uint8_t bus;
        uint8_t dev;
        uint8_t fun;
        seL4_CPtr iospace_cap;
        pci_devices_get_device(i, &bus, &dev, &fun, &iospace_cap);
        error = vm_guest_add_iospace(&vm, &vm.mem.vmm_vspace, iospace_cap);
        ZF_LOGF_IF(error, "failed to add iospace to vspace");
    }

    for (i = 0; i < physical_pci_iospaces_num_devices(); i++) {
        uint8_t bus;
        uint8_t dev;
        uint8_t fun;
        seL4_CPtr iospace_cap;
        error = physical_pci_iospaces_get_device(i, &bus, &dev, &fun, &iospace_cap);
        ZF_LOGF_IF(error, "failed to get physical pci iospace");
        error = vm_guest_add_iospace(&vm, &vm.mem.vmm_vspace, iospace_cap);
        ZF_LOGF_IF(error, "failed to add physical pci iospace to vspace");
    }
#endif

    vm_vcpu_t *vm_vcpu;
    vm_vcpu = vm_create_vcpu(&vm, 0);
    assert(vm_vcpu);

    error = vm_register_notification_callback(&vm, handle_async_event, NULL);
    assert(!error);

    error = vm_register_unhandled_ioport_callback(&vm, ioport_callback_handler, NULL);
    assert(!error);

    /* Initialize the init device badges and notification functions */
    ZF_LOGI("Init device badges and notification functions");
    init_con_irq_init();

    have_initrd = !(strcmp(initrd_image, "") == 0);

    ZF_LOGI("Init irqs");
    init_irqs(&vm);

    ZF_LOGI("IRQ controller init");
    error = vm_create_default_irq_controller(&vm);
    ZF_LOGF_IF(error, "IRQ Controller init failed");

    ZF_LOGI("serial pre init");
    serial_pre_init();

    uint64_t tsc_frequency = init_timer_tsc_frequency();
    ZF_LOGE("Init timer TSC frequency: %llu", (unsigned long long)tsc_frequency);

    ZF_LOGI("Pit pre init");
    pit_pre_init();

    ZF_LOGI("RTC pre init");
    rtc_pre_init();

#ifdef CONFIG_VMM_USE_HPET
    ZF_LOGI("HPET pre init\n");
    hpet_pre_init(tsc_frequency,
                  TIMER_HPET0,
                  init_timer_oneshot_relative,
                  init_timer_stop);
    vm_create_hpet(&vm);
#endif

    error = vmm_io_port_init(&io_ports, FREE_IOPORT_START);
    if (error) {
        ZF_LOGF_IF(error, "Failed to initialise VMM ioport management");
    }

    /* Do we need to do any early reservations of guest address space? */
    for (i = 0; i < ARRAY_SIZE(guest_ram_regions); i++) {
        error = vm_ram_register_at(&vm, guest_ram_regions[i].base, guest_ram_regions[i].size, false);
        ZF_LOGF_IF(error, "Failed to alloc guest ram at %p", (void *)guest_ram_regions[i].base);
    }

    for (i = 0; i < ARRAY_SIZE(guest_fake_devices); i++) {
        vm_memory_reservation_t *reservation = vm_reserve_memory_at(&vm, guest_fake_devices[i].base, guest_fake_devices[i].size,
                                                                    default_error_fault_callback, (void *)NULL);
        ZF_LOGF_IF(!reservation, "Failed to create guest device reservation at %p", (void *)guest_fake_devices[i].base);
        error = map_frame_alloc_reservation(&vm, reservation);
        ZF_LOGF_IF(error, "Failed to map guest device reservation at %p", (void *)guest_fake_devices[i].base);
    }

    reserve_physical_pci_host_apertures(&vm);

    /* Add in the device mappings specified by the guest. */
    for (i = 0; i < guest_mappings_num_guestmaps(); i++) {
        uint64_t frame_paddr;
        uint64_t size;
        error = guest_mappings_get_guest_map(i, &frame_paddr, &size);
        ZF_LOGF_IF(!error, "Failed to get guest map at %d\n", i);
    }

    // Remove any guest physical addresses that are defined as unavailable
    for (i = 0; i < exclude_paddr_num_regions(); i++) {
        uintptr_t base;
        size_t bytes;
        exclude_paddr_get_region(i, &base, &bytes);
        vm_memory_reservation_t *reservation = vm_reserve_memory_at(&vm, base, bytes,
                                                                    default_error_fault_callback, (void *)NULL);
        ZF_LOGF_IF(!reservation, "Failed to reserve guest physical address range %p - %p\n",
                   (void *)base, (void *)(base + bytes));
    }

    for (int i = 0; i < ARRAY_SIZE(free_anonymous_regions); i++) {
        error = vm_memory_make_anon(&vm, free_anonymous_regions[i].base,
                                    free_anonymous_regions[i].size);
        ZF_LOGF_IF(error, "Failed to create anonymous region %p - %p",
                   (void *)free_anonymous_regions[i].base,
                   (void *)(free_anonymous_regions[i].base +  free_anonymous_regions[i].size));
    }

    /* Allocate guest ram. This is the main memory that the guest will actually get
     * told exists. Other memory may get allocated and mapped into the guest */
    bool paddr_is_vaddr;
    paddr_is_vaddr = false;
    // allocate guest ram in 512MiB chunks. This prevents extreme fragmentation of the
    // physical address space when a large amount of guest RAM has been reuqested.
    // An important side affect is that if the requested RAM is large, and there are
    // devices or other regions in the lower 4GiB of the guest address space then we will
    // still allocate some RAM in the lower 4GiB, which a guest may require to run correctly.
    size_t remaining = MiB_TO_BYTES(guest_ram_mb);
    while (remaining > 0) {
        size_t allocate = MIN(remaining, MiB_TO_BYTES(512));
        uintptr_t res_addr = vm_ram_register(&vm, allocate);
        ZF_LOGF_IF(!res_addr, "Failed to allocate %lu bytes of guest ram. Already allocated %lu.",
                   (long)allocate, (long)(MiB_TO_BYTES(guest_ram_mb) - remaining));
        remaining -= allocate;
    }

    if (!physical_q35_needs_synthetic_pci_space(&vm)) {
        pci = NULL;
    } else if (physical_q35_pci_uses_structural_host_bridge(&vm)) {
        error = vmm_pci_init_empty(&pci, 0);
    } else {
        error = vmm_pci_init(&pci, 0);
    }
    if (physical_q35_needs_synthetic_pci_space(&vm) && error) {
        ZF_LOGF_IF(error, "Failed to initialise VMM PCI");
    }

    if (physical_q35_pci_uses_structural_host_bridge(&vm)) {
        physical_pci_raw_io = calloc(1, sizeof(*physical_pci_raw_io));
        ZF_LOGF_IF(!physical_pci_raw_io, "Failed to allocate physical pci raw io state");
        vmm_pci_raw_io_space_init(physical_pci_raw_io, make_camkes_pci_config());
    }

    if (physical_q35_pci_uses_structural_host_bridge(&vm)) {
        error = bind_generated_physical_pci_irqs(&vm);
        ZF_LOGF_IF(error, "Failed to bind generated physical pci irqs");
    } else {
        /* Perform device discovery and give passthrough device information */
        ZF_LOGI("PCI device discovery");
        for (i = 0; i < pci_devices_num_devices(); i++) {
            uint8_t bus;
            uint8_t dev;
            uint8_t fun;
            const char *irq_name;
            int irq = -1;
            seL4_CPtr iospace_cap;
            pci_devices_get_device(i, &bus, &dev, &fun, &iospace_cap);
            irq_name = pci_devices_get_device_irq(i);
            /* search for the irq */
            for (int j = 0; j < irqs_num_irqs(); j++) {
                seL4_CPtr cap;
                uint8_t ioapic;
                uint8_t source;
                int level_trig;
                int active_low;
                uint8_t dest;
                const char *this_name;
                this_name = irqs_get_irq(j, &cap, &ioapic, &source, &level_trig, &active_low, &dest);
                if (strcmp(irq_name, this_name) == 0) {
                    irq = dest;
                    break;
                }
            }
            assert(irq != -1);
            libpci_device_t *device = libpci_find_device_bdf(bus, dev, fun);
            if (!device) {
                LOG_ERROR("Failed to find device %02x:%02x.%d\n", bus, dev, fun);
                return NULL;
            }
            error = register_physical_pci_device(&vm, device, irq);
            assert(!error);
        }

        error = auto_register_qemu_pci_passthrough(&vm);
        ZF_LOGF_IF(error, "Failed to auto-register qemu pci passthrough devices");
    }

    /* Initialize any extra init devices */
    ZF_LOGI("Init extra devices");
    for (i = 0; i < init_cons_num_connections(); i++) {
        void (*proc)(vm_t *, vmm_pci_space_t *, vmm_io_port_list_t *) = (void (*)(vm_t *, vmm_pci_space_t *,
                                                                                  vmm_io_port_list_t *))init_cons_init_function(i);
        proc(&vm, pci, io_ports);
    }

    /* Add any IO ports */
    ZF_LOGI("Adding IO ports");
    for (i = 0; i < ARRAY_SIZE(ioport_handlers); i++) {
        if (ioport_handlers[i].port_in) {
            vm_ioport_range_t config_range = {ioport_handlers[i].start_port, ioport_handlers[i].end_port};
            vm_ioport_interface_t config_interface = {NULL, ioport_handlers[i].port_in, ioport_handlers[i].port_out,
                                                      ioport_handlers[i].desc
                                                     };
            error = vm_io_port_add_handler(&vm, config_range, config_interface);
            assert(!error);
        } else {
            error = vm_enable_passthrough_ioport(vm_vcpu, ioport_handlers[i].start_port,
                                                 ioport_handlers[i].end_port);
            assert(!error);
        }
    }
    for (i = 0; i < ioports_num_nonpci_ioports(); i++) {
        uint16_t start;
        uint16_t end;
        const char *desc;
        seL4_CPtr cap;
        desc = ioports_get_nonpci_ioport(i, &cap, &start, &end);
        error = vm_enable_passthrough_ioport(vm_vcpu, start, end);
        assert(!error);
    }
    /* config start and end encomposes both addr and data ports */
    vm_ioport_range_t pci_config_range = {X86_IO_PCI_CONFIG_START, X86_IO_PCI_CONFIG_END};
    vm_ioport_interface_t pci_config_interface = physical_q35_pci_uses_structural_host_bridge(&vm) ?
                                                 (vm_ioport_interface_t) {
                                                     physical_pci_raw_io, vmm_pci_raw_bus0_io_port_in, vmm_pci_raw_bus0_io_port_out,
                                                     "PCI Configuration Space"
                                                 } :
                                                 (vm_ioport_interface_t) {
                                                     pci, vmm_pci_io_port_in, vmm_pci_io_port_out,
                                                     "PCI Configuration Space"
                                                 };
    error = vm_io_port_add_handler(&vm, pci_config_range, pci_config_interface);
    assert(!error);

    uintptr_t kernel_load_addr;
    uintptr_t kernel_region_size;
    error = vm_ram_find_largest_free_region(&vm, &kernel_load_addr, &kernel_region_size);
    ZF_LOGF_IF(error, "Unable to find ram region for loading kernel image");
#ifdef CONFIG_ARCH_X86
    ZF_LOGF_IF(kernel_region_size < X86_GUEST_KERNEL_LOAD_PADDR,
               "Not enough low guest RAM to place kernel at 0x%x", X86_GUEST_KERNEL_LOAD_PADDR);
    kernel_load_addr = MAX(kernel_load_addr, (uintptr_t)X86_GUEST_KERNEL_LOAD_PADDR);
#endif
    guest_kernel_image_t guest_kernel_image;
    guest_kernel_image.kernel_image_arch.is_reloc_enabled = !(strcmp(kernel_relocs, "") == 0);
    guest_kernel_image.kernel_image_arch.relocs_file = kernel_relocs;
    error =  vm_load_guest_kernel(&vm, kernel_image, kernel_load_addr, BIT(PAGE_BITS_4M), &guest_kernel_image);
    ZF_LOGF_IF(error, "Failed to load guest kernel file");

    /* Add a boot module */
    guest_image_t guest_boot_image;
    if (have_initrd) {
        uintptr_t initrd_load_addr;
        uintptr_t initrd_region_size;
        error = vm_ram_find_largest_free_region(&vm, &initrd_load_addr, &initrd_region_size);
        ZF_LOGF_IF(error, "Unable to find ram region for loading initrd image");
        error = vm_load_guest_module(&vm, initrd_image, initrd_load_addr, 0, &guest_boot_image);
        ZF_LOGF_IF(error, "Failed to load boot module");
    }
    uintptr_t guest_boot_info_structure_addr;
    error = vmm_plat_init_guest_boot_structure(&vm, kernel_cmdline,
                                               guest_kernel_image, guest_boot_image,
                                               &guest_boot_info_structure_addr);
    ZF_LOGF_IF(error, "Failed to init guest boot structure");

    if (camkes_cross_vm_connections_init) {
        seL4_CPtr irq_notification = create_async_event_notification_cap(&vm, irq_badges[get_crossvm_irq_num()]);
        ZF_LOGF_IF(irq_notification == seL4_CapNull,
                   "Failed to create async event notification cap");
        error = camkes_cross_vm_connections_init(&vm, pci, irq_notification, CROSS_VM_BASE_ADDRESS);
        assert(!error);
    }

    /* Final VMM setup now that everything is defined and loaded */
    ZF_LOGI("Finalising VMM");
    error = vmm_plat_init_guest_thread_state(vm_vcpu,
                                             guest_kernel_image.kernel_image_arch.entry,
                                             guest_boot_info_structure_addr);
    ZF_LOGF_IF(error, "Failed to finalise VMM");

    vcpu_start(vm_vcpu);

    /* Now go run the event loop */
    vm_run(&vm);

    return NULL;
}

int run(void)
{
    sel4utils_run_on_stack(&vspace, main_continued, NULL, NULL);
    assert(!"Should not get here");

    return 0;
}
