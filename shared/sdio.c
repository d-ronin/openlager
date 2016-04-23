// STM32F4xx SDIO peripheral support
//
// Copyright (c) 2016, dRonin
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stm32f4xx.h>

#include <sdio.h>

#include <mmcreg.h>

#include <led.h>

static uint16_t sd_rca;
static bool sd_high_cap;

// XXX / todo error codes

static void sd_initpin(GPIO_TypeDef *gpio, uint16_t pin_pos) {
	GPIO_InitTypeDef pin_def = {
		.GPIO_Pin = 1 << (pin_pos),
		.GPIO_Mode = GPIO_Mode_AF,
		.GPIO_Speed = GPIO_Fast_Speed,
		.GPIO_OType = GPIO_OType_PP,
		.GPIO_PuPd = GPIO_PuPd_UP
	};

	GPIO_Init(gpio, &pin_def);
	GPIO_PinAFConfig(gpio, pin_pos, GPIO_AF_SDIO);
}

static void sd_clearflags() {
	// clear & check physical layer status, flags
	SDIO->ICR = SDIO_ICR_CCRCFAILC | SDIO_ICR_DCRCFAILC |
		SDIO_ICR_CTIMEOUTC | SDIO_ICR_DTIMEOUTC |
		SDIO_ICR_TXUNDERRC | SDIO_ICR_RXOVERRC |
		SDIO_ICR_CMDRENDC | SDIO_ICR_CMDSENTC |
		SDIO_ICR_DATAENDC | SDIO_ICR_STBITERRC |
		SDIO_ICR_DBCKENDC | SDIO_ICR_SDIOITC |
		SDIO_ICR_CEATAENDC;
}

static int sd_waitcomplete(uint32_t response_type) {
	uint32_t status;

	uint32_t completion_mask = SDIO_FLAG_CCRCFAIL | SDIO_FLAG_CMDREND | SDIO_FLAG_CTIMEOUT;

	if (!(response_type & MMC_RSP_PRESENT)) {
		// Sending the command is enough to consider ourselves "done"
		// if no response is expected.
		completion_mask |= SDIO_FLAG_CMDSENT;
	}

	int timeout=20000;

	do {
		status = SDIO->STA;

		if (timeout-- < 0) {
			return -1;
		}

	} while (!(status & completion_mask));

	sd_clearflags();

	if (!(response_type & MMC_RSP_CRC)) {
		status &= ~SDIO_STA_CCRCFAIL;
	}

	if (status &
			(SDIO_STA_CCRCFAIL | SDIO_STA_DCRCFAIL |
			 SDIO_STA_CTIMEOUT | SDIO_STA_DTIMEOUT |
			 SDIO_STA_TXUNDERR | SDIO_STA_RXOVERR |
			 SDIO_STA_STBITERR)) {
		return -1;
	}

	return 0;
}

static int sd_sendcmd(uint8_t cmd_idx, uint32_t arg, uint32_t response_type) {
	uint32_t response = SDIO_Response_No;

	if (response_type & MMC_RSP_136) {
		response = SDIO_Response_Long;
	} else if (response_type & MMC_RSP_PRESENT) {
		response = SDIO_Response_Short;
	}

	SDIO_CmdInitTypeDef cmd = {
		.SDIO_Argument = arg,
		.SDIO_CmdIndex = cmd_idx,
		.SDIO_Response = response,
		.SDIO_Wait = SDIO_Wait_No,
		.SDIO_CPSM = SDIO_CPSM_Enable
	};

	SDIO_SendCommand(&cmd);

	int ret = sd_waitcomplete(response_type);
	if (ret) return ret;

	if (response_type & MMC_RSP_OPCODE) {
		if (SDIO_GetCommandResponse() != cmd_idx) {
			return -1;
		}
	}

	return 0;
}

static int sd_cmdtype1(uint8_t cmd_idx, uint32_t arg) {
	int ret = sd_sendcmd(cmd_idx, arg, MMC_RSP_R1);

	if (ret) return ret;

	uint32_t card_status = SDIO_GetResponse(SDIO_RESP1);

	uint32_t err_bits = R1_STATUS(card_status);

	if (err_bits) {
		return -1;
	}

	if (card_status & R1_READY_FOR_DATA) {
		return 0;
	}

	return 1;		// OK, but not ready for data.
}

static int sd_cmd8() {
	/* 3.3V, 'DA' check pattern */
	uint32_t arg = 0x000001DA;

	int ret = sd_sendcmd(SD_SEND_IF_COND, arg, MMC_RSP_R7);

	if (ret) return ret;

	uint32_t response = SDIO_GetResponse(SDIO_RESP1);

	if ((response & 0xFFF) != arg) {
		return -1;
	}

	return 0;
}

static int sd_appcmdtype1(uint8_t cmd_idx, uint32_t arg) {
	int ret;

	ret = sd_cmdtype1(MMC_APP_CMD, sd_rca << 16);

	if (ret < 0) return ret;

	ret = sd_cmdtype1(cmd_idx, arg);

	return ret;
}

/* Sends ACMD41, gets resp3, returns op condition register */
static int sd_acmd41(uint32_t *ocr, bool hicap){
	int ret;

	ret = sd_cmdtype1(MMC_APP_CMD, 0 /* No RCA yet */);

	if (ret < 0) return ret;

	uint32_t arg = MMC_OCR_320_330;

	if (hicap) {
		arg |= MMC_OCR_CCS;
	}

	// some reference code sets the busy bit here, but the spec says
	// it should be 0.
	// arg |= MMC_OCR_CARD_BUSY;

	ret = sd_sendcmd(ACMD_SD_SEND_OP_COND, arg, MMC_RSP_R3);

	if (ret) return ret;

	*ocr = SDIO_GetResponse(SDIO_RESP1);

	return 0;
}


/* Sends CMD3, SD_SEND_RELATIVE_ADDR. */
static int sd_getrca(uint16_t *rca) {
	int ret = sd_sendcmd(SD_SEND_RELATIVE_ADDR, 0, MMC_RSP_R6);

	if (ret < 0) return -1;

	uint32_t resp = SDIO_GetResponse(SDIO_RESP1);

	// [15:0] card status bits: 23,22,19,12:0
	// COM_CRC_ERROR, ILLEGAL_COMMAND, ERROR, CURRENT_STATE[4]
	// READY_FOR_DATA, RESV[2], APP_CMD, RESV, AKE_SEQ_ERROR,
	// RESV[3]
	// Check the error bits and current state to be what we expect.
	if ((resp & 0xfe00) != (R1_STATE_IDENT) << 9) {
		return -1;
	}

	*rca = resp >> 16;

	return 0;
}

static int sd_checkbusy() {
	return sd_cmdtype1(MMC_SEND_STATUS, sd_rca << 16);
}

int sd_init(bool fourbit) {
	// Clocks programmed elsewhere and peripheral/GPIO clock enabled already

	// program GPIO / PinAF
	// SDIO_CK=PB15            SDIO_CMD=PA6
	// SDIO_D0=PB7 SDIO_D1=PA8 SDIO_D2=PA9 SDIO_D3=PB5

	sd_initpin(GPIOA, 6);
	sd_initpin(GPIOA, 8);
	sd_initpin(GPIOA, 9);
	sd_initpin(GPIOB, 5);
	sd_initpin(GPIOB, 7);
	sd_initpin(GPIOB, 15);

	// Take, then fix up default settings to talk slow.
	SDIO_InitTypeDef sd_settings;
	SDIO_StructInit(&sd_settings);

	sd_settings.SDIO_ClockDiv = 118;	// /120; = 400KHz at 48MHz
						// and 320KHz at 38.4MHz

	SDIO_Init(&sd_settings);

	// Turn it on and enable the clock
	SDIO_SetPowerState(SDIO_PowerState_ON);

	SDIO_ClockCmd(ENABLE);

	sd_high_cap = false;

	// The SD card negotiation and selection sequence is annoying.

	// CMD0- go idle state 4.2.1 (card reset)
	if (sd_sendcmd(MMC_GO_IDLE_STATE, 0, MMC_RSP_NONE)) {
		/* Nothing works.  Give up! */
		return -1;
	}

	// CMD8 SEND_IF_COND 4.2.2 (Operating Condition Validation)
	// (Enable advanced features, if card able)
	if (!sd_cmd8()) {
		// Woohoo, we can ask about highcap!
		sd_high_cap = true;
	}

	// A-CMD41 SD_SEND_OP_COND -- may return busy, need to loop
	uint32_t ocr;
	int tries=10000;

	do {
		if (tries-- < 0) {
			return -1;
		}

		if (sd_acmd41(&ocr, sd_high_cap)) {
			return -1;
		}
	} while (!(ocr & MMC_OCR_CARD_BUSY));

	// set our sd_high_cap flag based on response
	if (!(ocr & MMC_OCR_CCS)) {
		sd_high_cap = false;
	}

	// Legacy multimedia card multi-card addressing stuff that infects
	// the SD standard follows

	// CMD2 MMC_ALL_SEND_CID enter identification state
	if (sd_sendcmd(MMC_ALL_SEND_CID, 0, MMC_RSP_R2)) {
		return -1;
	}

	/* We don't care about the response / CID now. */

	/* But we -do- care about getting the RCA so we can talk to the card */
	if (sd_getrca(&sd_rca)) {
		return -1;
	}

	// Now that the card is inited.. crank the bus speed up!
	sd_settings.SDIO_ClockDiv = 0;		// /2; = 24MHz at 48MHz
						// and 19.2MHz at 38.4MHz

	SDIO_Init(&sd_settings);

	// CMD7 select the card
	if (sd_cmdtype1(MMC_SELECT_CARD, sd_rca << 16) < 0) {
		return -1;
	}

	// Interrupt configuration would go here... but we don't use it now.
	// DMA configuration would go here... but we don't use it now.

	// Four-bit support is mandatory, so don't bother to check it.
	if (fourbit) {
		if (sd_appcmdtype1(ACMD_SET_BUS_WIDTH, SD_BUS_WIDTH_4) < 0) {
			return -1;
		}

		sd_settings.SDIO_BusWide = SDIO_BusWide_4b;
		SDIO_Init(&sd_settings);
	}

	// If we got here, we won.. I think.
	return 0;
}

int sd_write(const uint8_t *data, uint32_t sect_num) {
	if (!(sd_high_cap)) {
		if (sect_num > 0x7fffff) {
			return -1;
		}

		sect_num *= 512;
	}

	while (sd_checkbusy() > 0);

	int ret = sd_cmdtype1(MMC_SET_BLOCKLEN, 512);

	if (ret) {
		led_send_morse("BLKLEN ");
		return ret;
	}

	ret = sd_cmdtype1(MMC_WRITE_BLOCK, sect_num);

	if (ret) {
		led_send_morse("WRCMDFAIL ");
		return ret;
	}

	SDIO_DataInitTypeDef data_xfer = {
		.SDIO_DataTimeOut = 20000000,	/* 1 secondish */
		.SDIO_DataLength = 512,
		.SDIO_DataBlockSize = 9 << 4,
		.SDIO_TransferDir = SDIO_TransferDir_ToCard,
		.SDIO_TransferMode = SDIO_TransferMode_Block,
		.SDIO_DPSM = SDIO_DPSM_Enable
	};

	SDIO_DataConfig(&data_xfer);

	int i = 512 / 4;

	while (true) {
		uint32_t status = SDIO->STA;

		if (status &
				(SDIO_STA_CCRCFAIL | SDIO_STA_DCRCFAIL |
				 SDIO_STA_CTIMEOUT | SDIO_STA_DTIMEOUT |
				 SDIO_STA_TXUNDERR | SDIO_STA_RXOVERR |
				 SDIO_STA_STBITERR)) {
			led_send_morse("WFLAG ");
			ret = -1;	/* we lose. */
			break;
		}

		if (i > 0) {
			if (!(status & SDIO_STA_TXFIFOF)) {
				i--;

				uint32_t temp;

				// LE pack the data from RAM, in case data is
				// unaligned.
				temp = *(data++);
				temp |= *(data++) << 8;
				temp |= *(data++) << 16;
				temp |= *(data++) << 24;

				SDIO->FIFO = temp;
			}
		} else {
			if (status & SDIO_STA_DBCKEND) {
				ret = 0;	/* Sounds good! */
				break;
			}
		}
	}

	sd_clearflags();

	if (ret) {
		led_send_morse("WFAIL ");
	}

	return ret;
}

int sd_read(uint8_t *data, uint32_t sect_num) {
	while (sd_checkbusy() > 0);

	if (!(sd_high_cap)) {
		if (sect_num > 0x7fffff) {
			return -1;
		}

		sect_num *= 512;
	}

	int ret = sd_cmdtype1(MMC_SET_BLOCKLEN, 512);

	if (ret < 0) {
		led_send_morse("BLKLEN ");
		return ret;
	}

	/* config data transfer and cue up the data xfer state machine */
	SDIO_DataInitTypeDef data_xfer = {
		.SDIO_DataTimeOut = 20000000,	/* 1 secondish */
		.SDIO_DataLength = 512,
		.SDIO_DataBlockSize = 9 << 4,
		.SDIO_TransferDir = SDIO_TransferDir_ToSDIO,
		.SDIO_TransferMode = SDIO_TransferMode_Block,
		.SDIO_DPSM = SDIO_DPSM_Enable
	};

	SDIO_DataConfig(&data_xfer);

	ret = sd_cmdtype1(MMC_READ_SINGLE_BLOCK, sect_num);

	if (ret < 0) {
		led_send_morse("CMDFAIL ");
		return ret;
	}

	int i = 512 / 4;

	while (true) {
		uint32_t status = SDIO->STA;

		if (status &
				(SDIO_STA_CCRCFAIL | SDIO_STA_DCRCFAIL |
				 SDIO_STA_CTIMEOUT | SDIO_STA_DTIMEOUT |
				 SDIO_STA_TXUNDERR | SDIO_STA_RXOVERR |
				 SDIO_STA_STBITERR)) {
			led_send_morse("FLAG ");
			ret = -1;	/* we lose. */
			break;
		}

		if (status & SDIO_STA_RXDAVL) {
			if (i <= 0) {
				led_send_morse("TOOMUCH ");
				ret = -1;	/* Too much data? */
				break;
			}

			i--;

			uint32_t temp = SDIO_ReadData();

			// LE stuff the data to ram, byte at a time,
			// in case data is unaligned.
			*(data++) = temp;
			*(data++) = temp >> 8;
			*(data++) = temp >> 16;
			*(data++) = temp >> 24;
		} else {
			if (status & SDIO_STA_DBCKEND) {
				if (i == 0) {
					ret = 0;	/* Sounds good! */
					break;
				}

				led_send_morse("MISSING ");

				/* What??? Finished before we got all the data */
				ret = -1;
				break;
			}
		}
	}

	sd_clearflags();

	if (ret) {
		led_send_morse("FAIL ");
	}

	return ret;
}
