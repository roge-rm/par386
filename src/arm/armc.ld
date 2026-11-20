/* The armc resource: one flat block of code, the entry at its start. */
SECTIONS
{
	.text 0 : { *(.text.entry) *(.text) *(.text.*) *(.rodata) *(.rodata.*) }
	.data : { *(.data) *(.data.*) *(.bss) *(.bss.*) *(COMMON) }
	/DISCARD/ : { *(.comment) }
}
