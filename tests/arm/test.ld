ENTRY(test_main)
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2M
         RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 320K }
SECTIONS {
 .text : { *(.text*) *(.rodata*) } > FLASH
 .ARM.extab : { *(.ARM.extab*) } > FLASH
 .ARM.exidx : { *(.ARM.exidx*) } > FLASH
 .data : { *(.data*) } > RAM AT> FLASH
 .bss : { *(.bss*) *(COMMON) } > RAM
}
