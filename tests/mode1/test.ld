ENTRY(Reset_Handler)
MEMORY { FLASH (rx) : ORIGIN = 0, LENGTH = 4M
         RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 4M }
SECTIONS { .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > FLASH
           .data : { *(.data*) } > RAM
           .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM }
