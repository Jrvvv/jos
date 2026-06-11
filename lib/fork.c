/* implement fork from user space */

#include <inc/string.h>
#include <inc/lib.h>

/* User-level fork with copy-on-write.
 * Create a child.
 * Lazily copy our address space and page fault handler setup to the child.
 * Then mark the child as runnable and return.
 *
 * Returns: child's envid to the parent, 0 to the child, < 0 on error.
 * It is also OK to panic on error.
 *
 * Hint:
 *   Use sys_map_region, it can perform address space copying in one call
 *   Don't forget to set page fault handler in the child (using sys_env_set_pgfault_upcall()).
 *   Remember to fix "thisenv" in the child process.
 */
envid_t
fork(void) {
    // LAB 9: Your code here.

    envid_t envid = sys_exofork();
    if (envid < 0)
        panic("sys_exofork: %i", envid);

    if (envid == 0) {
        // Child needs to update thisenv
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }

    // Return code for the sys calls
    int r = 0;

    // Parent needs to prepare address space
    if ((r = sys_map_region(0, NULL, envid, NULL, MAX_USER_ADDRESS, PROT_ALL | PROT_LAZY | PROT_COMBINE)) < 0) {
        sys_env_destroy(envid);
        return r;
    }

    // Copy the page fault handler if it exists
    if (thisenv->env_pgfault_upcall) {
        if ((r = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall) < 0)) {
            sys_env_destroy(envid);
            return r;
        }
    }

    // And start the child
    if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0) {
        sys_env_destroy(envid);
        return r;
    }

    return envid;
}

envid_t
sfork() {
    panic("sfork() is not implemented");
}
