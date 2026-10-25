/* Reset handler and C runtime init for STM32F413 (GNU as syntax).
 * The vector table itself is generated into vector_table.c.
 */
  .syntax unified
  .cpu cortex-m4
  .fpu fpv4-sp-d16
  .thumb

  .global Reset_Handler
  .type Reset_Handler, %function
Reset_Handler:
  ldr   r0, =_estack
  mov   sp, r0

  /* SystemInit() sets 100 MHz and the vector table offset. */
  ldr   r0, =SystemInit
  blx   r0

  /* Copy .data from flash to SRAM. */
  ldr   r0, =_sdata
  ldr   r1, =_edata
  ldr   r2, =_sidata
  movs  r3, #0
  b     copy_check
copy_loop:
  ldr   r4, [r2, r3]
  str   r4, [r0, r3]
  adds  r3, r3, #4
copy_check:
  adds  r4, r0, r3
  cmp   r4, r1
  bcc   copy_loop

  /* Zero .bss. */
  ldr   r0, =_sbss
  ldr   r1, =_ebss
  movs  r3, #0
  b     zero_check
zero_loop:
  str   r3, [r0]
  adds  r0, r0, #4
zero_check:
  cmp   r0, r1
  bcc   zero_loop

  /* Run static constructors, then main(). */
  bl    __libc_init_array
  bl    main

  /* main() must never return. */
  b     .
  .size Reset_Handler, . - Reset_Handler
