/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/elf.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/monitor.h>
#include <kern/sched.h>
#include <kern/kdebug.h>
#include <kern/macro.h>
#include <kern/pmap.h>
#include <kern/traceopt.h>

/* Currently active environment */
struct Env *curenv = NULL;

#ifdef CONFIG_KSPACE
/* All environments */
struct Env env_array[NENV];
struct Env *envs = env_array;
#else
/* All environments */
struct Env *envs = NULL;
#endif

/* Free environment list
 * (linked by Env->env_link) */
static struct Env *env_free_list;


/* NOTE: Should be at least LOGNENV */
#define ENVGENSHIFT 12

/* Converts an envid to an env pointer.
 * If checkperm is set, the specified environment must be either the
 * current environment or an immediate child of the current environment.
 *
 * RETURNS
 *     0 on success, -E_BAD_ENV on error.
 *   On success, sets *env_store to the environment.
 *   On error, sets *env_store to NULL. */
int
envid2env(envid_t envid, struct Env **env_store, bool need_check_perm) {
    struct Env *env;

    /* If envid is zero, return the current environment. */
    if (!envid) {
        *env_store = curenv;
        return 0;
    }

    /* Look up the Env structure via the index part of the envid,
     * then check the env_id field in that struct Env
     * to ensure that the envid is not stale
     * (i.e., does not refer to a _previous_ environment
     * that used the same slot in the envs[] array). */
    env = &envs[ENVX(envid)];
    if (env->env_status == ENV_FREE || env->env_id != envid) {
        *env_store = NULL;
        return -E_BAD_ENV;
    }

    /* Check that the calling environment has legitimate permission
     * to manipulate the specified environment.
     * If checkperm is set, the specified environment
     * must be either the current environment
     * or an immediate child of the current environment. */
    if (need_check_perm && env != curenv && env->env_parent_id != curenv->env_id) {
        *env_store = NULL;
        return -E_BAD_ENV;
    }

    *env_store = env;
    return 0;
}

/* Mark all environments in 'envs' as free, set their env_ids to 0,
 * and insert them into the env_free_list.
 * Make sure the environments are in the free list in the same order
 * they are in the envs array (i.e., so that the first call to
 * env_alloc() returns envs[0]).
 */
void
env_init(void) {
    /* kzalloc_region only works with current_space != NULL */

    /* Allocate envs array with kzalloc_region().
     * Don't forget about rounding.
     * kzalloc_region() only works with current_space != NULL */
    // LAB 8: Your code here

    /* Map envs to UENVS read-only,
     * but user-accessible (with PROT_USER_ set) */
    // LAB 8: Your code here

    for (int i = 0; i < NENV; i++) {
        envs[i].env_id = 0;
        envs[i].env_status = ENV_FREE;

        if (i == NENV - 1) {
            envs[i].env_link = NULL;
        } else {
            envs[i].env_link = &envs[i + 1];
        }
    }

    env_free_list = envs;
}

/* Allocates and initializes a new environment.
 * On success, the new environment is stored in *newenv_store.
 *
 * Returns
 *     0 on success, < 0 on failure.
 * Errors
 *    -E_NO_FREE_ENV if all NENVS environments are allocated
 *    -E_NO_MEM on memory exhaustion
 */
int
env_alloc(struct Env **newenv_store, envid_t parent_id, enum EnvType type) {

    struct Env *env;
    if (!(env = env_free_list))
        return -E_NO_FREE_ENV;

    /* Allocate and set up the page directory for this environment. */
    int res = init_address_space(&env->address_space);
    if (res < 0) return res;

    /* Generate an env_id for this environment */
    int32_t generation = (env->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
    /* Don't create a negative env_id */
    if (generation <= 0) generation = 1 << ENVGENSHIFT;
    env->env_id = generation | (env - envs);

    /* Set the basic status variables */
    env->env_parent_id = parent_id;
#ifdef CONFIG_KSPACE
    env->env_type = ENV_TYPE_KERNEL;
#else
    env->env_type = type;
#endif
    env->env_status = ENV_RUNNABLE;
    env->env_runs = 0;

    /* Clear out all the saved register state,
     * to prevent the register values
     * of a prior environment inhabiting this Env structure
     * from "leaking" into our new environment */
    memset(&env->env_tf, 0, sizeof(env->env_tf));

    /* Set up appropriate initial values for the segment registers.
     * GD_UD is the user data (KD - kernel data) segment selector in the GDT, and
     * GD_UT is the user text (KT - kernel text) segment selector (see inc/memlayout.h).
     * The low 2 bits of each segment register contains the
     * Requestor Privilege Level (RPL); 3 means user mode, 0 - kernel mode.  When
     * we switch privilege levels, the hardware does various
     * checks involving the RPL and the Descriptor Privilege Level
     * (DPL) stored in the descriptors themselves */

#ifdef CONFIG_KSPACE
    env->env_tf.tf_ds = GD_KD;
    env->env_tf.tf_es = GD_KD;
    env->env_tf.tf_ss = GD_KD;
    env->env_tf.tf_cs = GD_KT;

    static uintptr_t stack_top = 0x2000000;
    int env_index = env - envs;
    env->env_tf.tf_rsp = stack_top + env_index * PROG_STACK_SIZE;
#else
    env->env_tf.tf_ds = GD_UD | 3;
    env->env_tf.tf_es = GD_UD | 3;
    env->env_tf.tf_ss = GD_UD | 3;
    env->env_tf.tf_cs = GD_UT | 3;
    env->env_tf.tf_rsp = USER_STACK_TOP;
#endif

    /* For now init trapframe with IF set */
    env->env_tf.tf_rflags = FL_IF;

    /* Commit the allocation */
    env_free_list = env->env_link;
    *newenv_store = env;

    if (trace_envs) cprintf("[%08x] new env %08x\n", curenv ? curenv->env_id : 0, env->env_id);
    return 0;
}

/* Pass the original ELF image to binary/size and bind all the symbols within
 * its loaded address space specified by image_start/image_end.
 * Make sure you understand why you need to check that each binding
 * must be performed within the image_start/image_end range.
 */
static int
bind_functions(struct Env *env, uint8_t *binary, size_t size, uintptr_t image_start, uintptr_t image_end) {
    /* NOTE: find_function from kdebug.c should be used */
    struct Elf* header_ptr = (struct Elf*)binary;
    struct Secthdr* section_headers = (struct Secthdr*)((uint8_t*)binary + header_ptr->e_shoff);
    
    char* string_names_table = (char*)binary + section_headers[header_ptr->e_shstrndx].sh_offset;
    
    uintptr_t sym_base = 0, sym_limit = 0;
    uintptr_t str_base = 0, str_limit = 0;

    for (UINT32 idx = 0; idx < header_ptr->e_shnum; ++idx) {
        const char* s_name = string_names_table + section_headers[idx].sh_name;
        
        if (strcmp(s_name, ".symtab") == 0) {
            sym_base = (uintptr_t)binary + section_headers[idx].sh_offset;
            sym_limit = sym_base + section_headers[idx].sh_size;
        } else if (strcmp(s_name, ".strtab") == 0) {
            str_base = (uintptr_t)binary + section_headers[idx].sh_offset;
            str_limit = str_base + section_headers[idx].sh_size;
        }

        if (sym_base && str_base) break;
    }

    struct Elf64_Sym* symbol_entry = (struct Elf64_Sym*)sym_base;
    const char* str_pool = (const char*)str_base;
    const size_t pool_capacity = (size_t)(str_limit - str_base);

    for (; (uintptr_t)symbol_entry < sym_limit; symbol_entry++) {
        if ((symbol_entry->st_info & 0xf) != STT_OBJECT) 
            continue;

        uint32_t offset = symbol_entry->st_name;
        
        if (offset < pool_capacity) {
            const char* sym_name = str_pool + offset;
            uintptr_t resolved_addr = find_function(sym_name);
            
            if (resolved_addr != 0) {
                *(uintptr_t*)(symbol_entry->st_value) = resolved_addr;
            }
        }
    }

    return 0;
}

/* Set up the initial program binary, stack, and processor flags
 * for a user process.
 * This function is ONLY called during kernel initialization,
 * before running the first environment.
 *
 * This function loads all loadable segments from the ELF binary image
 * into the environment's user memory, starting at the appropriate
 * virtual addresses indicated in the ELF program header.
 * At the same time it clears to zero any portions of these segments
 * that are marked in the program header as being mapped
 * but not actually present in the ELF file - i.e., the program's bss section.
 *
 * All this is very similar to what our boot loader does, except the boot
 * loader also needs to read the code from disk.  Take a look at
 * LoaderPkg/Loader/Bootloader.c to get ideas.
 *
 * Finally, this function maps one page for the program's initial stack.
 *
 * load_icode returns -E_INVALID_EXE if it encounters problems.
 *  - How might load_icode fail?  What might be wrong with the given input?
 *
 * Hints:
 *   Load each program segment into memory
 *   at the address specified in the ELF section header.
 *   You should only load segments with ph->p_type == ELF_PROG_LOAD.
 *   Each segment's address can be found in ph->p_va
 *   and its size in memory can be found in ph->p_memsz.
 *   The ph->p_filesz bytes from the ELF binary, starting at
 *   'binary + ph->p_offset', should be copied to address
 *   ph->p_va.  Any remaining memory bytes should be cleared to zero.
 *   (The ELF header should have ph->p_filesz <= ph->p_memsz.)
 *
 *   All page protection bits should be user read/write for now.
 *   ELF segments are not necessarily page-aligned, but you can
 *   assume for this function that no two segments will touch
 *   the same page.
 *
 *   You must also do something with the program's entry point,
 *   to make sure that the environment starts executing there.
 *   What?  (See env_run() and env_pop_tf() below.) */
static int
load_icode(struct Env *env, uint8_t *binary, size_t size) {
    struct Proghdr *ph;

    struct Elf *elf = (struct Elf*)binary;
    // Check if input parameters are valid
    if (binary == NULL || size == 0) {
        cprintf("load_icode: invalid binary or size\n");
        return -E_INVALID_EXE;
    }

    // Check if file is large enough to contain ELF header
    if (size < sizeof(struct Elf)) {
        cprintf("load_icode: binary too small for ELF header\n");
        return -E_INVALID_EXE;
    }

    // Verify ELF magic number
    if (elf->e_magic != ELF_MAGIC) {
        cprintf("load_icode: ELF magic number mismatch: %08x vs %08x\n",
                elf->e_magic, ELF_MAGIC);
        return -E_INVALID_EXE;
    }

    // Verify ELF header size (e_ehsize)
    if (elf->e_ehsize < sizeof(struct Elf)) {
        cprintf("load_icode: ELF header size too small: %d\n", elf->e_ehsize);
        return -E_INVALID_EXE;
    }

    // Verify ELF file type (e_type): exec or dyn_lib/PIC
    if (elf->e_type != ET_EXEC && elf->e_type != ET_DYN) {
        cprintf("load_icode: unsupported ELF type %d\n", elf->e_type);
        return -E_INVALID_EXE;
    }

    // Verify target architecture (e_machine)
    if (elf->e_machine != EM_X86_64) {
        cprintf("load_icode: unsupported machine type %d\n", elf->e_machine);
        return -E_INVALID_EXE;
    }

    // Verify ELF version (e_version)
    if (elf->e_version != 1 /* EV_CURRENT */) {
        cprintf("load_icode: unsupported ELF version %d\n", elf->e_version);
        return -E_INVALID_EXE;
    }

    // Check if program headers are present (e_phnum)
    if (elf->e_phnum == 0) {
        cprintf("load_icode: no program headers\n");
        return -E_INVALID_EXE;
    }

    // Verify program header entry size (e_phentsize)
    if (elf->e_phentsize != sizeof(struct Proghdr)) {
        cprintf("load_icode: invalid program header entry size: %d\n",
                elf->e_phentsize);
        return -E_INVALID_EXE;
    }

    // Check if program header offset is 8-byte aligned (required for 64-bit)
    if (elf->e_phoff & 0x7) {
        cprintf("load_icode: program header offset not aligned to 8 bytes\n");
        return -E_INVALID_EXE;
    }

    // Check for integer overflow when computing program header table size
    if (elf->e_phnum > (UINT64_MAX - elf->e_phoff) / elf->e_phentsize) {
        cprintf("load_icode: program header table size overflow\n");
        return -E_INVALID_EXE;
    }

    // Check if program header table fits within file boundaries
    if (elf->e_phoff + (uint64_t)elf->e_phnum * elf->e_phentsize > size) {
        cprintf("load_icode: program header table extends beyond binary size\n");
        return -E_INVALID_EXE;
    }

    // Verify section headers if present (consistency checks)
    if (elf->e_shnum > 0) {
        if (elf->e_shentsize != sizeof(struct Secthdr)) {
            cprintf("load_icode: invalid section header entry size: %d\n",
                    elf->e_shentsize);
            return -E_INVALID_EXE;
        }
        if (elf->e_shoff + (uint64_t)elf->e_shnum * elf->e_shentsize > size) {
            cprintf("load_icode: section header table extends beyond file\n");
            return -E_INVALID_EXE;
        }
        if (elf->e_shstrndx >= elf->e_shnum) {
            cprintf("load_icode: invalid section name string table index\n");
            return -E_INVALID_EXE;
        }
    }

    // Check if at least one PT_LOAD segment exists
    bool has_load_segment = false;
    for (int i = 0; i < elf->e_phnum; i++) {
        ph = (struct Proghdr*)(binary + elf->e_phoff + i * elf->e_phentsize);
        if (ph->p_type == PT_LOAD) {
            has_load_segment = true;
            break;
        }
    }
    if (!has_load_segment) {
        cprintf("load_icode: no loadable segments found\n");
        return -E_INVALID_EXE;
    }

    uintptr_t image_start = ~0UL;
    uintptr_t image_end = 0;

    bool entry_in_load = false;

    ph = (struct Proghdr*)(binary + elf->e_phoff);
    for (int i = 0; i < elf->e_phnum; i++) {
        if (ph[i].p_type == ELF_PROG_LOAD) {
            // Check p_align field (must be power of two, non-zero for loadable segments)
            if (ph[i].p_align == 0 || (ph[i].p_align & (ph[i].p_align - 1)) != 0) {
                cprintf("load_icode: invalid alignment 0x%lx for load segment %d\n",
                        ph[i].p_align, i);
                return -E_INVALID_EXE;
            }

            // Verify address alignment according to p_align for loadable segments
            if (ph[i].p_align > 1) {
                if ((ph[i].p_offset % ph[i].p_align) != (ph[i].p_va % ph[i].p_align)) {
                    cprintf("load_icode: p_offset and p_va have different modulo p_align\n");
                    return -E_INVALID_EXE;
                }
            }

            // Check if segment is within binary bounds
            if (ph[i].p_offset + ph[i].p_filesz > size) {
                cprintf("load_icode: segment %d extends beyond binary size\n", i);
                return -E_INVALID_EXE;
            }

            // Verify that file size does not exceed memory size
            if (ph[i].p_filesz > ph[i].p_memsz) {
                cprintf("load_icode: segment %d has filesz > memsz\n", i);
                return -E_INVALID_EXE;
            }

            // Check that loadable segments have non-zero memory size
            if (ph[i].p_memsz == 0) {
                cprintf("load_icode: loadable segment %d has zero memory size\n", i);
                return -E_INVALID_EXE;
            }

            // Verify virtual address range does not overflow or exceed user space limits
            // Assume maximum user address is UTOP (defined in memlayout.h)
            if (ph[i].p_va > MAX_USER_ADDRESS - 1 || ph[i].p_va + ph[i].p_memsz > MAX_USER_ADDRESS) {
                cprintf("load_icode: segment %d virtual address range [%lx, %lx) exceeds UTOP\n",
                        i, ph->p_va, ph->p_va + ph->p_memsz);
                return -E_INVALID_EXE;
            }

            if ((ph[i].p_flags & ~(ELF_PROG_FLAG_EXEC |
                                   ELF_PROG_FLAG_WRITE |
                                   ELF_PROG_FLAG_READ)) != 0) {
                cprintf("load_icode: segment %d has invalid flags 0x%x\n", i, ph[i].p_flags);
                return -E_INVALID_EXE;
            }

            if (elf->e_entry >= ph[i].p_va && elf->e_entry < ph[i].p_va + ph[i].p_memsz) {
                entry_in_load = true;
            }

            // Allocate memory for the segment
            // For now, we'll assume the memory is already mapped and accessible
            // In a real implementation, we would need to map the pages here

            // Copy the segment data from the binary
            memcpy((void*)ph[i].p_va, binary + ph[i].p_offset, ph[i].p_filesz);
            // Clear the remaining part of the segment (BSS section)
            memset((void*)(ph[i].p_va + ph[i].p_filesz), 0, ph[i].p_memsz - ph[i].p_filesz);

            if (ph[i].p_va < image_start)
                image_start = ph[i].p_va;
            if (ph[i].p_va + ph[i].p_memsz > image_end)
                image_end = ph[i].p_va + ph[i].p_memsz;
        }
    }

    // Verify entry point address
    if (elf->e_entry == 0) {
        cprintf("load_icode: entry point is NULL\n");
        return -E_INVALID_EXE;
    }

    // Check if entry point falls within any loadable segment
    if (!entry_in_load) {
        cprintf("load_icode: entry point 0x%lx not within any loadable segment\n",
                elf->e_entry);
        return -E_INVALID_EXE;
    }

    if (bind_functions(env, binary, size, image_start, image_end) < 0) {
        return -E_INVALID_EXE;
    }
    // Set the entry point in the trap frame
    env->env_tf.tf_rip = elf->e_entry;

    return 0;
}

/* Allocates a new env with env_alloc, loads the named elf
 * binary into it with load_icode, and sets its env_type.
 * This function is ONLY called during kernel initialization,
 * before running the first user-mode environment.
 * The new env's parent ID is set to 0.
 */
void
env_create(uint8_t *binary, size_t size, enum EnvType type) {
    struct Env *env;
    int r;

    // Allocate a new environment
    if ((r = env_alloc(&env, 0, type)) < 0) {
        panic("env_create: env_alloc failed: %i", r);
    }

    // Load the ELF binary into the environment
    if ((r = load_icode(env, binary, size)) < 0) {
        panic("env_create: load_icode failed: %i", r);
    }
}


/* Frees env and all memory it uses */
void
env_free(struct Env *env) {

    /* Note the environment's demise. */
    if (trace_envs) cprintf("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, env->env_id);

#ifndef CONFIG_KSPACE
    /* If freeing the current environment, switch to kern_pgdir
     * before freeing the page directory, just in case the page
     * gets reused. */
    if (&env->address_space == current_space)
        switch_address_space(&kspace);

    static_assert(MAX_USER_ADDRESS % HUGE_PAGE_SIZE == 0, "Misaligned MAX_USER_ADDRESS");
    release_address_space(&env->address_space);
#endif

    /* Return the environment to the free list */
    env->env_status = ENV_FREE;
    env->env_link = env_free_list;
    env_free_list = env;
}

/* Frees environment env
 *
 * If env was the current one, then runs a new environment
 * (and does not return to the caller)
 */
void
env_destroy(struct Env *env) {
    /* If env is currently running on other CPUs, we change its state to
     * ENV_DYING. A zombie environment will be freed the next time
     * it traps to the kernel. */

    if ((env->env_status != ENV_FREE) && (env->env_status != ENV_DYING)) {
        if (env == curenv) {
            env_free(env);
            sched_yield();
        } else {
            env_free(env);
        }
    }
}

#ifdef CONFIG_KSPACE
void
csys_exit(void) {
    if (!curenv) panic("curenv = NULL");
    env_destroy(curenv);
}

void
csys_yield(struct Trapframe *tf) {
    memcpy(&curenv->env_tf, tf, sizeof(struct Trapframe));
    sched_yield();
}
#endif

/* Restores the register values in the Trapframe with the 'ret' instruction.
 * This exits the kernel and starts executing some environment's code.
 *
 * This function does not return.
 */

_Noreturn void
env_pop_tf(struct Trapframe *tf) {
    asm volatile(
            "movq %0, %%rsp\n"
            "movq 0(%%rsp), %%r15\n"
            "movq 8(%%rsp), %%r14\n"
            "movq 16(%%rsp), %%r13\n"
            "movq 24(%%rsp), %%r12\n"
            "movq 32(%%rsp), %%r11\n"
            "movq 40(%%rsp), %%r10\n"
            "movq 48(%%rsp), %%r9\n"
            "movq 56(%%rsp), %%r8\n"
            "movq 64(%%rsp), %%rsi\n"
            "movq 72(%%rsp), %%rdi\n"
            "movq 80(%%rsp), %%rbp\n"
            "movq 88(%%rsp), %%rdx\n"
            "movq 96(%%rsp), %%rcx\n"
            "movq 104(%%rsp), %%rbx\n"
            "movq 112(%%rsp), %%rax\n"
            "movw 120(%%rsp), %%es\n"
            "movw 128(%%rsp), %%ds\n"
            "addq $152,%%rsp\n" /* skip tf_trapno and tf_errcode */
            "iretq" ::"g"(tf)
            : "memory");

    /* Mostly to placate the compiler */
    panic("Reached unrecheble\n");
}

/* Context switch from curenv to env.
 * This function does not return.
 *
 * Step 1: If this is a context switch (a new environment is running):
 *       1. Set the current environment (if any) back to
 *          ENV_RUNNABLE if it is ENV_RUNNING (think about
 *          what other states it can be in),
 *       2. Set 'curenv' to the new environment,
 *       3. Set its status to ENV_RUNNING,
 *       4. Update its 'env_runs' counter,
 * Step 2: Use env_pop_tf() to restore the environment's
 *       registers and starting execution of process.

 * Hints:
 *    If this is the first call to env_run, curenv is NULL.
 *
 *    This function loads the new environment's state from
 *    env->env_tf.  Go back through the code you wrote above
 *    and make sure you have set the relevant parts of
 *    env->env_tf to sensible values.
 */
_Noreturn void
env_run(struct Env *env) {
    assert(env);

    if (trace_envs_more) {
        const char *state[] = {"FREE", "DYING", "RUNNABLE", "RUNNING", "NOT_RUNNABLE"};
        if (curenv) cprintf("[%08X] env stopped: %s\n", curenv->env_id, state[curenv->env_status]);
        cprintf("[%08X] env started: %s\n", env->env_id, state[env->env_status]);
    }

    // Step 1: Handle context switch
    // If there's a current environment, set it back to RUNNABLE if it's RUNNING
    if (curenv && curenv->env_status == ENV_RUNNING) {
        curenv->env_status = ENV_RUNNABLE;
    }

    // Set the new environment as current
    curenv = env;

    // Set its status to RUNNING
    env->env_status = ENV_RUNNING;

    // Update its runs counter
    env->env_runs++;

    // Step 2: Restore the environment's registers and start execution
    env_pop_tf(&env->env_tf);

    // This function should never return
    panic("env_run returned unexpectedly");
}
