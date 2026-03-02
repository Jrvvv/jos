/* Simple linker script for JOS user-level programs.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Load programs at this address: "." means the current address */
    . = #ADDRESS#;

    .text : {
        *(.text .ltext .stub .text.* .ltext.* .gnu.linkonce.t.*)

        . = ALIGN(8);

        PROVIDE(etext = .); /* Define the 'etext' symbol to this value */

        *(.rodata .lrodata .rodata.* .lrodata.* .gnu.linkonce.r.*)

        /* Ensure page-aligned segment size */
        . = ALIGN(0x1000);
    }


    .data : {
        *(.ldata .data .ldata.* .data.* .data.rel.local .got .got.plt)

        PROVIDE(edata = .);

        *(.bss .lbss .bss.* .lbss.*)

        /* Ensure page-aligned segment size */
        . = ALIGN(0x1000);
    }

    PROVIDE(end = .);


    /DISCARD/ : {
        *(.interp .eh_frame .note.GNU-stack .comment)
    }
}
