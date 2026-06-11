/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/console.h>
#include <kern/env.h>
#include <kern/kclock.h>
#include <kern/pmap.h>
#include <kern/sched.h>
#include <kern/syscall.h>
#include <kern/trap.h>
#include <kern/traceopt.h>

/* Print a string to the system console.
 * The string is exactly 'len' characters long.
 * Destroys the environment on memory errors. */
static int
sys_cputs(const char *s, size_t len) {
    // LAB 8: Your code here

    /* Check that the user has permission to read memory [s, s+len).
     * Destroy the environment if not. */
    user_mem_assert(curenv, s, len, PROT_R);

    /* Due to ASAN we can't just use this memory, and we can't unpoison it, because it's in user space.
     * Print it part by part, copying to the small kernel space stack buffer */
#define TMP_BUFFER_SIZE 256
    char tmp_buffer[TMP_BUFFER_SIZE];
    const char* cur = s;
    size_t remaining_length = len;
    while (remaining_length != 0) {
        size_t current_size = MIN(TMP_BUFFER_SIZE, remaining_length);
        remaining_length -= current_size;
        nosan_memcpy(tmp_buffer, (void*)cur, current_size);
        cur += current_size;
        cprintf("%.*s", (int)current_size, tmp_buffer);
    }
#undef TMP_BUFFER_SIZE
    return 0;
}

/* Read a character from the system console without blocking.
 * Returns the character, or 0 if there is no input waiting. */
static int
sys_cgetc(void) {
    // LAB 8: Your code here
    return cons_getc();
}

/* Returns the current environment's envid. */
static envid_t
sys_getenvid(void) {
    // LAB 8: Your code here
    return curenv->env_id;
}

/* Destroy a given environment (possibly the currently running environment).
 *
 *  Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid. */
static int
sys_env_destroy(envid_t envid) {
    // LAB 8: Your code here.
    struct Env* env = envs;
    if (envid == 0) {
        env = curenv;
    } else {
        int res = envid2env(envid, &env, true);
        if (res) return res;
    }

#if 1 /* TIP: Use this snippet to log required for passing grade tests info */
    if (trace_envs) {
        cprintf(env == curenv ?
                        "[%08x] exiting gracefully\n" :
                        "[%08x] destroying %08x\n",
                curenv->env_id, env->env_id);
    }
#endif
    env_destroy(env);
    return 0;
}

/* Dispatches to the correct kernel function, passing the arguments. */
uintptr_t
syscall(uintptr_t syscallno, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    /* Call the function corresponding to the 'syscallno' parameter.
     * Return any appropriate return value. */

    // LAB 8: Your code here
    switch (syscallno) {
        case SYS_cputs:
            return sys_cputs((const char*)a1, a2);
        case SYS_cgetc:
            return sys_cgetc();
        case SYS_getenvid:
            return sys_getenvid();
        case SYS_env_destroy:
            return sys_env_destroy(a1);
        default:
            return -E_NO_SYS;
    }

    return -E_NO_SYS;
}
