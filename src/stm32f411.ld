MEMORY
{
FLASH (rx)	: ORIGIN = 0x08000000, LENGTH = 256K
RAM (rwx)	: ORIGIN = 0x20000000, LENGTH = 128K
}

_stack_top = 0x20020000;

SECTIONS
{
	/* Start of program at beginning of flash */
	.vectors :
	{
		. = ALIGN(4);
		KEEP(*(.vectors))
		. = ALIGN(4);
	} >FLASH

	.text :
	{
		. = ALIGN(4);
		*(.text) *(.text.*)
		*(.glue_7t) *(.glue_7)
		. = ALIGN(4);
	}

	.rodata :
	{
		. = ALIGN(4);
		*(.rodata) *(.rodata*)
		. = ALIGN(4);
	}

	_sidata = LOADADDR(.data);

	.data :
	{
		. = ALIGN(4);
		_sdata = .;
		*(.data) *(.data*)
		. = ALIGN(4);
		_edata = .;
	}

	.bss :
	{
		. = ALIGN(4);
		_sbss = .;
		*(.bss) *(.bss.*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
	} >RAM
}
