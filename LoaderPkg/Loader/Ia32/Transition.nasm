; @file
; Copyright (c) 2020, ISP RAS. All rights reserved.
; SPDX-License-Identifier: BSD-3-Clause

bits 32

SECTION .data
    align 4

%macro GDT_DESC 2
    dw 0xFFFF, 0
    db 0, %1, %2, 0
%endmacro

GDT_BASE:
    dq  0x0             ; NULL segment
LINEAR_CODE_SEL:        equ $ - GDT_BASE
    GDT_DESC 0x9A, 0xCF
LINEAR_DATA_SEL:        equ $ - GDT_BASE
    GDT_DESC 0x92, 0xCF
LINEAR_CODE64_SEL:      equ $ - GDT_BASE
    GDT_DESC 0x9A, 0xAF
LINEAR_DATA64_SEL:      equ $ - GDT_BASE
    GDT_DESC 0x92, 0xCF

GDT_DESCRIPTOR:
    dw 0x28 - 1 
    dd GDT_BASE
    dd 0x0

KERNEL_ENTRY:
    dd 0

LOADER_PARAMS:
    dd 0
    
PAGE_TABLE:
    dd 0

SECTION .text

global ASM_PFX(IsCpuidSupportedAsm)
ASM_PFX(IsCpuidSupportedAsm):
    ; Store original EFLAGS for later comparison.
    pushf
    ; Store current EFLAGS.
    pushf
    ; Invert the ID bit in stored EFLAGS.
    xor dword [esp], 0x200000
    ; Load stored EFLAGS (with ID bit inverted).
    popf
    ; Store EFLAGS again (ID bit may or may not be inverted).
    pushf
    ; Read modified EFLAGS (ID bit may or may not be inverted).
    pop eax
    ; Enable bits in RAX to whichver bits in EFLAGS were changed.
    xor eax, [esp]
    ; Restore stack pointer.
    popf
    ; Leave only the ID bit EFLAGS change result in RAX.
    and eax, 0x200000
    ; Shift it to the lowest bit be boolean compatible.
    shr eax, 21
    ; Return.
    ret

global ASM_PFX(CallKernelThroughGateAsm)
ASM_PFX(CallKernelThroughGateAsm):
    ; Transitioning from protected mode to long mode is described in Intel SDM
    ; 9.8.5 Initializing IA-32e Mode. More detailed explanation why paging needs
    ; to be disabled is explained in 4.1.2 Paging-Mode Enabling.

    ; Disable interrupts.
    cli

    ; Drop return pointer as we no longer need it.
    pop ecx

    ; Save kernel entry point passed by the bootloader.
    pop ecx
    mov eax, KERNEL_ENTRY
    mov [eax], ecx

    ; Save loading params address passed by the bootloader.
    pop ecx
    mov eax, LOADER_PARAMS
    mov [eax], ecx

    ; Save identity page table passed by the bootloader.
    pop ecx
    mov eax, PAGE_TABLE
    mov [eax], ecx

    ; 1. Disable paging.
    ; Paging must be disabled before transitioning to long mode
    ; because the page table format differs between protected mode and long mode.
    mov eax, cr0
    and eax, ~(1 << 31)  ; Clear PG bit (bit 31) to disable paging
    mov cr0, eax

    ; 2. Switch to our GDT that supports 64-bit mode and update CS to LINEAR_CODE_SEL.
    ; Load custom GDT descriptor that includes 64-bit code/data segments
    lgdt [GDT_DESCRIPTOR]

    ; Perform far jump to update CS register (cannot be done with mov)
    ; This switches to our linear code segment in 32-bit protected mode
    jmp LINEAR_CODE_SEL:AsmWithOurGdt

AsmWithOurGdt:

    ; 3. Reset all the data segment registers to linear mode (LINEAR_DATA_SEL).
    ; Update all data segment registers to use our linear data segment
    mov ax, LINEAR_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 4. Enable PAE/PGE in CR4, which is required to transition to long mode.
    ; PAE (Physical Address Extension) is required for long mode paging
    ; PGE (Page Global Enable) allows frequently used or shared pages to be marked
    ; as global to all users
    mov eax, cr4
    or eax, (1 << 5)    ; Set PAE bit (bit 5)
    or eax, (1 << 7)    ; Uncomment to also set PGE bit (bit 7) if needed
    mov cr4, eax

    ; 5. Update page table address register (CR3) right away with the supplied PAGE_TABLE.
    ; This does nothing as paging is off at the moment as paging is disabled.
    ; Load the page table address that was passed by the bootloader
    mov eax, [PAGE_TABLE]
    mov cr3, eax

    ; 6. Enable long mode (LME) and execute protection (NXE) via the EFER MSR register.
    ; EFER (Extended Feature Enable Register) MSR number is 0xC0000080
    ; LME (Long Mode Enable) is bit 8
    ; NXE (No-Execute Enable) is bit 11
    mov ecx, 0xC0000080  ; EFER MSR number
    rdmsr                ; Read current EFER value
    or eax, (1 << 8)     ; Set LME bit (bit 8)
    or eax, (1 << 11)    ; Set NXE bit (bit 11) for execute protection
    wrmsr                ; Write back to EFER MSR

    ; 7. Enable paging as it is required in 64-bit mode.
    ; Re-enable paging with additional protection bits
    mov eax, cr0
    or eax, (1 << 31)   ; Set PG bit (bit 31) to enable paging
    or eax, (1 << 16)   ; Set WP bit (bit 16) for write protection
    or eax, (1 << 18)   ; Set AM bit (bit 18) for alignment mask
    or eax, (1 << 29)   ; Set NW bit (bit 29) and CD bit (bit 30) if needed
    mov cr0, eax

    ; 8. Transition to 64-bit mode by updating CS with LINEAR_CODE64_SEL.
    ; Perform far jump to switch to 64-bit code segment
    ; This is the actual transition point to long mode
    jmp LINEAR_CODE64_SEL:AsmInLongMode

AsmInLongMode:
    BITS 64

    ; 9. Reset all the data segment registers to linear 64-bit mode (LINEAR_DATA64_SEL).
    ; In long mode, segment registers are mostly ignored, but we set them properly
    ; Use 64-bit registers and the flat memory model
    mov ax, LINEAR_DATA64_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 10. Jump to the kernel code.
    mov ecx, [REL LOADER_PARAMS]
    mov ebx, [REL KERNEL_ENTRY]
    jmp rbx

noreturn:
    hlt
    jmp noreturn
