/*
 * Copyright 2026, Unikie
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <stdbool.h>

#include <camkes/dataport_caps.h>
#include <seL4VMParameters.template.h>
#include <sel4vm/guest_vm.h>
#include <crossvm.h>
#include <sel4vmmplatsupport/drivers/cross_vm_connection.h>
#include <sel4vmmplatsupport/ioports.h>
#include <utils/util.h>

typedef struct vmm_pci_space vmm_pci_space_t;
typedef struct vmm_io_list vmm_io_port_list_t;

extern vka_t _vka;
extern vmm_pci_space_t *pci;
extern vmm_io_port_list_t *io_ports;

typedef struct vmm_module {
    const char *name;
    void *cookie;
    void (*init_module)(vm_t *vm, void *cookie);
    bool initialized;
    struct vmm_module **deps_start;
    struct vmm_module **deps_stop;
} ALIGN(32) vmm_module_t;

#define DEFINE_MODULE(_name, _cookie, _init_module) \
    static USED SECTION("_vmm_module_deps_" #_name) struct {} dummy_dep_##_name; \
    extern vmm_module_t *__start__vmm_module_deps_##_name[]; \
    extern vmm_module_t *__stop__vmm_module_deps_##_name[]; \
    vmm_module_t VMM_MODULE_ ##_name = { \
        .name = #_name, \
        .cookie = _cookie, \
        .init_module = _init_module, \
        .deps_start = __start__vmm_module_deps_##_name, \
        .deps_stop = __stop__vmm_module_deps_##_name, \
    }; \
    USED SECTION("_vmm_module") vmm_module_t *VMM_MODULE_ptr_ ##_name = &VMM_MODULE_ ##_name;

#define DEFINE_MODULE_DEP(_name, _dep) \
    extern vmm_module_t VMM_MODULE_ ##_dep; \
    USED SECTION("_vmm_module_deps_" #_name) vmm_module_t *VMM_MODULE_DEPS_ ## _name ## _ ## _dep = &VMM_MODULE_##_dep;

int get_crossvm_irq_num(void);
int vmm_module_init(vmm_module_t *m, void *cookie);
int vmm_module_init_by_name(const char *name, void *cookie);

typedef int (*async_event_handler_fn_t)(vm_t *vm, void *cookie);
int register_async_event_handler(seL4_Word badge, async_event_handler_fn_t callback, void *cookie);
