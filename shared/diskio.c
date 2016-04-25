/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2016        */
/*-----------------------------------------------------------------------*/

#include "diskio.h"		/* FatFs lower layer API */
#include <sdio.h>		/* dRonin SDIO implementation functions */

/* Definitions of physical drive number for each drive */
#define CARD		0	/* Example: Map ATA harddisk to physical drive 0 */


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv != CARD)
		return STA_NOINIT;

	return 0;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (pdrv != CARD)
		return STA_NOINIT;

	return 0;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Sector address in LBA */
	UINT count		/* Number of sectors to read */
)
{
	if (pdrv != CARD)
		return RES_PARERR;

	BYTE *rptr = buff;

	for (UINT i = 0; i < count; i++) {
		int retries=3;

retry: ;
		int ret = sd_read(rptr, sector + i);

		if (ret) {
			if (retries--) {
				goto retry;
			}

			return RES_ERROR;
		}

		rptr += 512;
	}

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Sector address in LBA */
	UINT count			/* Number of sectors to write */
)
{
	if (pdrv != CARD)
		return RES_PARERR;

	const BYTE *wptr = buff;

	for (UINT i = 0; i < count; i++) {
		int retries = 3;

retry: ;
		int ret = sd_write(wptr, sector + i);

		if (ret) {
			if (retries--) {
				goto retry;
			}

			return RES_ERROR;
		}

		wptr += 512;
	}

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	if (pdrv != CARD)
		return RES_PARERR;

	if (cmd == CTRL_SYNC)
		return RES_OK;

	return RES_PARERR;
}

