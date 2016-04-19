MEMORY
{
FLASH (rx)	: ORIGIN = 0x08010000, LENGTH = 64K
/* After the loader, plus only using a small bit, leaving the last 128k avail */
RAM (rwx)	: ORIGIN = 0x20000000, LENGTH = 128K
}

