/*
 * STM32F413ZHTx linker script (NUCLEO-F413ZH)
 * Flash 1536K @ 0x08000000, SRAM1 256K @ 0x20000000, SRAM2 64K @ 0x20040000
 */
ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);      /* top of SRAM1 */
_Min_Heap_Size  = 0x400;                  /* libc heap for printf/newlib */
_Min_Stack_Size = 0x800;                  /* main() stack before the scheduler starts */
__libc_heap_limit = _estack - _Min_Stack_Size; /* reserve MSP/ISR stack at runtime */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1280K
  SETTINGS (r) : ORIGIN = 0x08140000, LENGTH = 256K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 256K
  RAM2  (xrw) : ORIGIN = 0x20040000, LENGTH = 64K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);
  ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(SETTINGS), "Firmware overlaps settings")

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* FreeRTOS heap lives in SRAM2 so DMA buffers in SRAM1 stay uncontended. */
  .freertos_heap (NOLOAD) :
  {
    . = ALIGN(8);
    _srtos_heap = .;
    KEEP(*(.freertos_heap))
    . = ALIGN(8);
    _ertos_heap = .;
  } >RAM2

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
