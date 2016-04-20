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

static void sd_initpin(GPIO_TypeDef *gpio, uint16_t pin) {
	GPIO_InitTypeDef pin_def = {
		.GPIO_Pin = pin,
		.GPIO_Mode = GPIO_Mode_AF,
		.GPIO_Speed = GPIO_Fast_Speed,
		.GPIO_OType = GPIO_OType_PP,
		.GPIO_PuPd = GPIO_PuPd_UP
	};

	GPIO_Init(gpio, &pin_def);
	GPIO_PinAFConfig(gpio, pin, GPIO_AF_SDIO);
}

static void sd_simplecmd(uint32_t cmd_idx, uint32_t arg, bool response) {
	SDIO_CmdInitTypeDef cmd = {
		.SDIO_Argument = arg,
		.SDIO_CmdIndex = cmd_idx,
		.SDIO_Response = response ? SDIO_Response_Short : SDIO_Response_No,
		.SDIO_Wait = SDIO_Wait_No,
		.SDIO_CPSM = SDIO_CPSM_Enable
	};

	SDIO_SendCommand(&cmd);
}

static void sd_appcmd(uint32_t cmd_idx, uint32_t arg) {
	// XXX stuff RCA
	sd_simplecmd(MMC_APP_CMD, 0, true);

	// XXX get resp1
	
	sd_simplecmd(cmd_idx, arg, true);
}

void sd_init(bool fourbit) {
	// Clocks programmed elsewhere and peripheral/GPIO clock enabled already
	
	// program GPIO / PinAF
	// SDIO_CK=PB15            SDIO_CMD=PA6
	// SDIO_D0=PB7 SDIO_D1=PA8 SDIO_D2=PA9 SDIO_D3=PB5

	sd_initpin(GPIOA, GPIO_Pin_6);
	sd_initpin(GPIOA, GPIO_Pin_8);
	sd_initpin(GPIOA, GPIO_Pin_9);
	sd_initpin(GPIOB, GPIO_Pin_5);
	sd_initpin(GPIOB, GPIO_Pin_7);
	sd_initpin(GPIOB, GPIO_Pin_15);

	// Take, then fix up default settings to talk slow.
	SDIO_InitTypeDef sd_settings;
	SDIO_StructInit(&sd_settings);

	sd_settings.SDIO_ClockDiv = 118;	// /120; = 400KHz at 48MHz
						// and 320KHz at 38.4MHz

	SDIO_Init(&sd_settings);

	// Turn it on and enable the clock
	SDIO_SetPowerState(SDIO_PowerState_ON);

	SDIO_ClockCmd(ENABLE);

	// The SD card negotiation and selection sequence is annoying.

	// CMD0- go idle state 4.2.1 (card reset)
	sd_simplecmd(MMC_GO_IDLE_STATE, 0, false);
	// XXX wait / test error

	// XXX CMD8 SEND_IF_COND 4.2.2 (Operating Condition Validation)
	// (Find out if we're extended capacity, mostly)
	sd_simplecmd(SD_SEND_IF_COND, /* voltage/pattern XXX */ 0, true);
	// get resp7 4.9.6

	// XXX A-CMD41 SD_SEND_OP_COND -- may return busy , need to loop
	sd_appcmd(ACMD_SD_SEND_OP_COND, /* XXX voltage | type */ 0);
	// get resp3

	// Legacy multimedia card multi-card addressing stuff that infects
	// the SD standard follows

	// XXX CMD2 SD_CMD_ALL_SEND_CID enter identification state
	// get resp2
	
	// XXX CMD3 SD_CMD_SET_REL_ADDR get RCA 
	// get resp6

	// Now that the card is inited.. crank the bus speed up!
	sd_settings.SDIO_ClockDiv = 0;		// /2; = 24MHz at 48MHz
						// and 19.2MHz at 38.4MHz

	SDIO_Init(&sd_settings);

	// XXX CMD7 SD_CMD_SEL_DESEL_CARD select the card
	// get resp1

	// Interrupt configuration would go here... but we don't use it now.
	// DMA configuration would go here... but we don't use it now.
	
	// Four-bit support is mandatory, so don't bother to check it.
	if (fourbit) {
		sd_appcmd(ACMD_SET_BUS_WIDTH, SD_BUS_WIDTH_4);

		sd_settings.SDIO_BusWide = SDIO_BusWide_4b;
		SDIO_Init(&sd_settings);
	}

}
