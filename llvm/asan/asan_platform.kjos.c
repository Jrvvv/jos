/*
 * Institute for System Programming of the Russian Academy of Sciences
 * Copyright (C) 2017 ISPRAS
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, Version 3.
 *
 * This program is distributed in the hope # that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License version 3 for more details.
 */

#include "asan.h"
#include "asan_internal.h"
#include "asan_memintrinsics.h"
#include "inc/memlayout.h"

#if !defined(SANITIZE_SHADOW_BASE) || !defined(SANITIZE_SHADOW_SIZE) || !defined(SANITIZE_SHADOW_OFF)
#error "You are to define SANITIZE_SHADOW_BASE and SANITIZE_SHADOW_SIZE for shadow memory support!"
#endif

/* FIXME: We need to wrap all the custom allocators we use and track the allocated memory.
 * There should probably be more of them (especially in the kernel). */

void
        NORETURN
        _panic(const char *file, int line, const char *fmt, ...);

void
platform_abort() {
    _panic("asan", 0, "platform_abort");
}

void
platform_asan_init() {
    asan_internal_shadow_start = (uint8_t *)SANITIZE_SHADOW_BASE - 0x40000000;
    asan_internal_shadow_end = (uint8_t *)(SANITIZE_SHADOW_SIZE + SANITIZE_SHADOW_BASE);
    asan_internal_shadow_off = (uint8_t *)SANITIZE_SHADOW_OFF;

    extern char end[];
    extern size_t max_memory_map_addr;

    /* Fill memory shadow memory of kernel itself with 0x00s, */
    asan_internal_fill_range(KERN_BASE_ADDR + KERN_START_OFFSET, (uintptr_t)end - (KERN_BASE_ADDR + KERN_START_OFFSET), 0);

    /* and the rest of preallocated shadow memory within BOOT_MEM_SIZE with 0xFFs */
    if (KERN_BASE_ADDR + MIN(BOOT_MEM_SIZE, max_memory_map_addr) > (uintptr_t)end)
        asan_internal_fill_range((uintptr_t)end, MIN(BOOT_MEM_SIZE, max_memory_map_addr) + KERN_BASE_ADDR - (uintptr_t)end, 0x00);

    /* Unpoison kernel main stack and #PF stack */
    asan_internal_fill_range((uintptr_t)(KERN_STACK_TOP - KERN_STACK_SIZE), KERN_STACK_SIZE, 0);
    asan_internal_fill_range((uintptr_t)(KERN_PF_STACK_TOP - KERN_PF_STACK_SIZE), KERN_PF_STACK_SIZE, 0);
}

void
platform_asan_unpoison(void *addr, size_t size) {
    asan_internal_fill_range((uptr)addr, size, 0);
}

void
platform_asan_poison(void *addr, size_t size) {
    asan_internal_fill_range((uptr)addr, size, ASAN_GLOBAL_RZ);
}

void
platform_asan_fatal(const char *msg, uptr p, size_t width, unsigned access_type) {
    const char* access_type_str = NULL;
    switch (access_type) {
        case TYPE_LOAD:
            access_type_str = "load";
            break;
        case TYPE_STORE:
            access_type_str = "store";
            break;
        case TYPE_KFREE:
            access_type_str = "kfree";
            break;
        case TYPE_ZFREE:
            access_type_str = "zfree";
            break;
        case TYPE_FSFREE:
            access_type_str = "fsfree";
            break;
        case TYPE_MEMLD:
            access_type_str = "memld";
            break;
        case TYPE_MEMSTR:
            access_type_str = "memstr";
            break;
        case TYPE_STRINGLD:
            access_type_str = "stringld";
            break;
        case TYPE_STRINGSTR:
            access_type_str = "stringstr";
            break;
        default:
            access_type_str = "unknown";
            break;
    }

    ASAN_LOG("Fatal error: %s (addr 0x%lx within i/o size 0x%lx of type %s), tracing:",
             msg, (long)p, (long)width, access_type_str);

    ASAN_DEBUG_BREAK();

    DUMP_STACK_AT_LEVEL(p, 0);
    DUMP_STACK_AT_LEVEL(p, 1);
    DUMP_STACK_AT_LEVEL(p, 2);
    DUMP_STACK_AT_LEVEL(p, 3);
    DUMP_STACK_AT_LEVEL(p, 4);
    DUMP_STACK_AT_LEVEL(p, 5);
    DUMP_STACK_AT_LEVEL(p, 6);
    DUMP_STACK_AT_LEVEL(p, 7);
    DUMP_STACK_AT_LEVEL(p, 8);
    DUMP_STACK_AT_LEVEL(p, 9);
    DUMP_STACK_AT_LEVEL(p, 10);
    DUMP_STACK_AT_LEVEL(p, 11);
    DUMP_STACK_AT_LEVEL(p, 12);
    DUMP_STACK_AT_LEVEL(p, 13);
    DUMP_STACK_AT_LEVEL(p, 14);
    DUMP_STACK_AT_LEVEL(p, 15);

    ASAN_ABORT();
}

bool
platform_asan_fakestack_enter(uint32_t *thread_id) {
    // TODO: implement!
    return true;
}

void
platform_asan_fakestack_leave() {
    // TODO: implement!
}
