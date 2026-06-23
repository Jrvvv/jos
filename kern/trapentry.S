/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>
#include <kern/macro.h>

#include <kern/picirq.h>

#ifdef CONFIG_KSPACE

.globl clock_thdlr
.type clock_thdlr, @function
clock_thdlr:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + IRQ_CLOCK), 136(%rsp)
    call trap
    jmp .

.globl timer_thdlr
.type timer_thdlr, @function
timer_thdlr:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + IRQ_TIMER), 136(%rsp)
    call trap
    jmp .

#else

# TRAPHANDLER defines a globally-visible function for handling a trap.
# It pushes a trap number onto the stack, then jumps to _alltraps.
# Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
#
# You shouldn't call a TRAPHANDLER function from C, but you may
# need to _declare_ one in C (for instance, to get a function pointer
# during IDT setup).  You can declare the function with
#   void NAME();
# where NAME is the argument passed to TRAPHANDLER.

#define TRAPHANDLER(name, num) \
.globl name;                   \
.type name, @function;         \
.align 2;                      \
name:                          \
    pushq $(num);              \
    jmp _alltraps

# Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
# It pushes a 0 in place of the error code, so the trap frame has the same
# format in either case.

#define TRAPHANDLER_NOEC(name, num) \
.globl name;                        \
.type name, @function;              \
.align 2;                           \
name:                               \
    pushq $0;                       \
    pushq $(num);                   \
    jmp _alltraps

.text

.globl _alltraps
.type _alltraps, @function;
.align 2
_alltraps:
    # LAB 8: Your code here
    # Complete `struct Trapframe' on stack
    # NOTE: Members after tf_paddind4 already on stack.
    #       For struct PushRegs you can use PUSHA macro.
    pushw $0 # padding 4
    pushw $0 # padding 4
    pushw $0 # padding 3
    pushw $GD_KD # ds
    pushw $0 # padding 2
    pushw $0 # padding 2
    pushw $0 # padding 1
    pushw $GD_KD # es

    PUSHA

    # LAB 8: Your code here
    # Invoke `trap' with a pointer to struct trap as an argument.
    # xor %ebp, %ebp
    movq %rsp, %rdi
    call trap

    jmp .

# LAB 8: Your code here
# Use TRAPHANDLER or TRAPHANDLER_NOEC to setup
# all trap handlers' entry points
# (Look up their list in inc/trap.h)

TRAPHANDLER_NOEC(divide_thndlr, T_DIVIDE)
TRAPHANDLER_NOEC(debug_thndlr, T_DEBUG)
TRAPHANDLER_NOEC(nmi_thndlr, T_NMI)
TRAPHANDLER_NOEC(brkpt_thndlr, T_BRKPT)
TRAPHANDLER_NOEC(oflow_thndlr, T_OFLOW)
TRAPHANDLER_NOEC(bound_thndlr, T_BOUND)
TRAPHANDLER_NOEC(illop_thndlr, T_ILLOP)
TRAPHANDLER_NOEC(device_thndlr, T_DEVICE)
TRAPHANDLER(dblflt_thndlr, T_DBLFLT)
# T_COPROC - not generated
TRAPHANDLER(tss_thndlr, T_TSS)
TRAPHANDLER(segnp_thndlr, T_SEGNP)
TRAPHANDLER(stack_thndlr, T_STACK)
TRAPHANDLER(gpflt_thndlr, T_GPFLT)
TRAPHANDLER(pgflt_thndlr, T_PGFLT)
# T_RES - reserved
TRAPHANDLER_NOEC(fperr_thndlr, T_FPERR)
TRAPHANDLER     (align_thndlr, T_ALIGN)
TRAPHANDLER_NOEC(mchk_thndlr, T_MCHK)
TRAPHANDLER_NOEC(simderr_thndlr, T_SIMDERR)

TRAPHANDLER_NOEC(timer_thdlr, IRQ_OFFSET + IRQ_TIMER)
TRAPHANDLER_NOEC(clock_thdlr, IRQ_OFFSET + IRQ_CLOCK)
TRAPHANDLER_NOEC(kbd_thdlr, IRQ_OFFSET + IRQ_KBD)
TRAPHANDLER_NOEC(serial_thdlr, IRQ_OFFSET + IRQ_SERIAL)

TRAPHANDLER_NOEC(syscall_thndlr, T_SYSCALL)

#endif
