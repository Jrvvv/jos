#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/dwarf.h>
#include <inc/elf.h>
#include <inc/x86.h>

#include <kern/kdebug.h>
#include <kern/env.h>
#include <inc/uefi.h>

void
load_kernel_dwarf_info(struct Dwarf_Addrs *addrs) {
    addrs->aranges_begin = (uint8_t *)(uefi_lp->DebugArangesStart);
    addrs->aranges_end = (uint8_t *)(uefi_lp->DebugArangesEnd);
    addrs->abbrev_begin = (uint8_t *)(uefi_lp->DebugAbbrevStart);
    addrs->abbrev_end = (uint8_t *)(uefi_lp->DebugAbbrevEnd);
    addrs->info_begin = (uint8_t *)(uefi_lp->DebugInfoStart);
    addrs->info_end = (uint8_t *)(uefi_lp->DebugInfoEnd);
    addrs->line_begin = (uint8_t *)(uefi_lp->DebugLineStart);
    addrs->line_end = (uint8_t *)(uefi_lp->DebugLineEnd);
    addrs->str_begin = (uint8_t *)(uefi_lp->DebugStrStart);
    addrs->str_end = (uint8_t *)(uefi_lp->DebugStrEnd);
    addrs->pubnames_begin = (uint8_t *)(uefi_lp->DebugPubnamesStart);
    addrs->pubnames_end = (uint8_t *)(uefi_lp->DebugPubnamesEnd);
    addrs->pubtypes_begin = (uint8_t *)(uefi_lp->DebugPubtypesStart);
    addrs->pubtypes_end = (uint8_t *)(uefi_lp->DebugPubtypesEnd);
}

#define UNKNOWN       "<unknown>"
#define CALL_INSN_LEN 5

/* debuginfo_rip(addr, info)
 * Fill in the 'info' structure with information about the specified
 * instruction address, 'addr'.  Returns 0 if information was found, and
 * negative if not.  But even if it returns negative it has stored some
 * information into '*info'
 */
int
debuginfo_rip(uintptr_t addr, struct Ripdebuginfo *info) {
    if (!addr) return 0;

    /* Initialize *info */
    strcpy(info->rip_file, UNKNOWN);
    strcpy(info->rip_fn_name, UNKNOWN);
    info->rip_fn_namelen = sizeof UNKNOWN - 1;
    info->rip_line = 0;
    info->rip_fn_addr = addr;
    info->rip_fn_narg = 0;

    struct Dwarf_Addrs addrs;
    assert(addr >= MAX_USER_READABLE);
    load_kernel_dwarf_info(&addrs);

    Dwarf_Off offset = 0, line_offset = 0;
    int res = info_by_address(&addrs, addr, &offset);
    if (res < 0) goto error;

    char *tmp_buf = NULL;
    res = file_name_by_info(&addrs, offset, &tmp_buf, &line_offset);
    if (res < 0) goto error;
    strncpy(info->rip_file, tmp_buf, sizeof(info->rip_file));

    /* Find line number corresponding to given address.
     * Hint: note that we need the address of `call` instruction, but rip holds
     * address of the next instruction, so we should substract 5 from it.
     * Hint: use line_for_address from kern/dwarf_lines.c */

    uintptr_t call_addr = addr - CALL_INSN_LEN;
    int line_num = 0;
    if (line_for_address(&addrs, call_addr, line_offset, &line_num) == 0) {
        info->rip_line = line_num;
    }

    /* Find function name corresponding to given address.
     * Hint: note that we need the address of `call` instruction, but rip holds
     * address of the next instruction, so we should substract CALL_INSN_LEN from it.
     * Hint: use function_by_info from kern/dwarf.c
     * Hint: info->rip_fn_name can be not NULL-terminated,
     * string returned by function_by_info will always be */

    char *fn_name = NULL;
    uintptr_t fn_addr = 0;
    if (function_by_info(&addrs, call_addr, offset, &fn_name, &fn_addr) == 0) {
        strncpy(info->rip_fn_name, fn_name, sizeof(info->rip_fn_name));
        info->rip_fn_namelen = strlen(fn_name);
        info->rip_fn_addr = fn_addr;
        info->rip_fn_narg = 0;
    }

error:
    return res;
}

uintptr_t
find_function(const char *const fname) {
    /* There are two functions for function name lookup.
     * address_by_fname, which looks for function name in section .debug_pubnames
     * and naive_address_by_fname which performs full traversal of DIE tree.
     * It may also be useful to look to kernel symbol table for symbols defined
     * in assembly. */
    if (fname == NULL || fname[0] == '\0') return 0;

    const uintptr_t s_base = uefi_lp->SymbolTableStart;
    const uintptr_t s_limit = uefi_lp->SymbolTableEnd;
    const uintptr_t t_base = uefi_lp->StringTableStart;
    const uintptr_t t_limit = uefi_lp->StringTableEnd;

    if (s_base && s_limit > s_base && t_base && t_limit > t_base) {
        const struct Elf64_Sym* curr_sym = (const struct Elf64_Sym*)s_base;
        const struct Elf64_Sym* end_sym = (const struct Elf64_Sym*)s_limit;
        const char* names_pool = (const char*)t_base;

        for (; curr_sym < end_sym; ++curr_sym) {
            uint32_t name_idx = curr_sym->st_name;

            if (name_idx == 0 || (t_base + name_idx >= t_limit)) continue;

            const char* current_name = names_pool + name_idx;
            
            if (strcmp(current_name, fname) == 0) {
                uint8_t st_type = curr_sym->st_info & 0xF;
                if (st_type == STT_FUNC || st_type == STT_NOTYPE) return (uintptr_t)curr_sym->st_value;
            }
        }
    }

    struct Dwarf_Addrs debug_info;
    uintptr_t found_addr = 0;
    
    load_kernel_dwarf_info(&debug_info);
    
    if (address_by_fname(&debug_info, fname, &found_addr) == 0) return found_addr;    
    if (naive_address_by_fname(&debug_info, fname, &found_addr) == 0) return found_addr;

    return 0;
}
