// OpenLager bootloader
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
//

#include <stdbool.h>

#include <stm32f4xx_flash.h>
#include <stm32f4xx_rcc.h>

#include <systick_handler.h>

#include <sdio.h>
#include <ff.h>

#include <led.h>

extern uint32_t _efill;

const void *_interrupt_vectors[FPU_IRQn] __attribute((section(".interrupt_vectors"))) = {
};

#define MAINPROGRAM_OFFSET 0x08010000 // from src/memory.ld
void invoke_next_program()
{
	// XXX should deinit interrupt-y things here... or at least mask
	// interrupts.
	RCC_APB2PeriphResetCmd(0xffffffff, ENABLE);
	RCC_APB1PeriphResetCmd(0xffffffff, ENABLE);
	RCC_APB2PeriphResetCmd(0xffffffff, DISABLE);
	RCC_APB1PeriphResetCmd(0xffffffff, DISABLE);

	void *memory = (void *) MAINPROGRAM_OFFSET;

	register uintptr_t stack_pointer asm ("r0") = *((const uintptr_t *)(memory));
	register uintptr_t program_entry asm ("r1") = *((const uintptr_t *)(memory + sizeof(void *)));

	asm volatile (
		"mov	sp, r0\n\t"
		"msr	MSP, r0\n\t"
		"bx	r1\n\t"
		: "+r" (stack_pointer), "+r" (program_entry)
		: :);   // No clobbers etc, because this never comes back.

	__builtin_unreachable();
}

void chk_flashop(FLASH_Status f)
{
	if (f != FLASH_COMPLETE) {
		while (1) {
			// ..-. . .-. .-.
			led_panic("FERR");
		}
	}
}

/* If anything goes wrong here, we'll still go to the main program.
 * But it's doubtful, because things going wrong here are likely to
 * affect the main program too.  So we are willing to pay the penalty
 * to flash an error code.
 */
void try_loader_stuff()
{
	/* Real hardware has LED on PB9. (sink on) */
	led_init_pin(GPIOB, GPIO_Pin_9, true);

	/* Discovery hardware has blue LED on PD15. */
	/* led_init_pin(GPIOD, GPIO_Pin_15, false); */

	if (sd_init(false)) {
		// -.-. .- .-. -..
		led_send_morse("CARD ");
		return;
	}

	FATFS fatfs;

	if (f_mount(&fatfs, "0:", 1) != FR_OK) {
		// -.. .- - .-
		led_send_morse("DATA ");
		return;
	}

	FIL fil;

	if (f_open(&fil, "0:lager.bin", FA_READ) != FR_OK) {
		// Missing image is not an error -> we don't blink
		return;
	}

	uint32_t *prog_flash = &_efill;
	UINT amount;

	bool diff = false;

	// XXX elim magic number.
	uint32_t buf[16384];    // Nice to have a lot of ram;
	                        // can safely use 64k on this stack
	                        // frame on the loader.

	/* XXX Ideally we would set up to flash larger than 64k
	 * programs, since there's 192k of flash... */
	if (FR_OK != f_read(&fil, buf, sizeof(buf), &amount)) {
		// .. ---
		led_send_morse("IO ");
		return;
	}

	if ((amount < 500) || (amount % sizeof(*buf))) {
		// Very short or not an integral number of words.
		led_send_morse("TRUNC ");
		return;
	}

	amount /= sizeof(*buf);         // convert into word count

	for (int i = 0; i < amount; i++) {
		if (buf[i] != prog_flash[i]) {
			diff = true;

			// ..- .--.   ..- .--.
			led_send_morse("UP UP ");

			break;
		}
	}

	if (!diff) {
		// no update necessary.. go to main firmware.
		return;
	}

	FLASH_Unlock();

	// checks success, infloop blinking if not
	chk_flashop(FLASH_EraseSector(FLASH_Sector_4, VoltageRange_3));

	led_send_morse("PRG ");

	for (int i = 0; i < amount; i++) {
		chk_flashop(FLASH_ProgramWord((uint32_t) (prog_flash + i),
				buf[i]));
	}

	led_send_morse(".. ");

	FLASH_Lock();
}

int main()
{
	/* Keep it really simple in the loader-- just run from 16MHz RC osc,
	 * no wait states, etc. */
	RCC_DeInit();

	/* Wait for the internal oscillator, not that we expect to need
	 * it.
	 */
	while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);

	RCC_HCLKConfig(RCC_SYSCLK_Div1);        /* AHB = 16MHz */
	RCC_PCLK1Config(RCC_HCLK_Div1);         /* APB1 = 16MHz */
	RCC_PCLK2Config(RCC_HCLK_Div1);         /* APB2 = 16MHz */
	RCC_TIMCLKPresConfig(RCC_TIMPrescDesactivated);
	/* "Desactivate"... the timer prescaler */

	/* The PLL is necessary to talk to the SDIO peripheral */
	RCC_PLLConfig(RCC_PLLSource_HSI,
			8,      /* PLLM = /8 = 2MHz */
			96,     /* PLLN = *96 = 192MHz */
			2,      /* PLLP = /2 = 96MHz, but not used */
			5       /* PLLQ = /5 = 38.4MHz, underclock SDIO
	                         * (Maximum is 48MHz)  Will get a 19.2MHz
	                         * SD card clock from dividing by 2, or
	                         * 9.6MBps at 4 bits wide.
	                         */
			);

	/* SDIO peripheral clocked at 38.4MHz.  minimum APB2 = 14.4MHz,
	 * and we have 16MHz.. so we're good */

	/* Enable and wait for the PLL */
	RCC_PLLCmd(ENABLE);

	while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA |
			RCC_AHB1Periph_GPIOB |
			RCC_AHB1Periph_GPIOC |
			RCC_AHB1Periph_GPIOD |
			RCC_AHB1Periph_GPIOE,
			ENABLE);

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 |
			RCC_APB1Periph_TIM3 |
			RCC_APB1Periph_TIM4 |
			RCC_APB1Periph_TIM5,
			ENABLE);

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1 |
			RCC_APB2Periph_USART1 |
			RCC_APB2Periph_SPI1 |
			RCC_APB2Periph_SYSCFG |
			RCC_APB2Periph_SDIO,
			ENABLE);

	/* Seize PA14/PA13 from SWD. */
	GPIO_InitTypeDef swd_def = {
		.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_13,
		.GPIO_Mode = GPIO_Mode_IN,
		.GPIO_Speed = GPIO_Low_Speed,
		.GPIO_OType = GPIO_OType_PP,
		.GPIO_PuPd = GPIO_PuPd_NOPULL
	};

	GPIO_Init(GPIOA, &swd_def);

	SysTick_Config(16000000/250);   /* 250Hz systick */

	try_loader_stuff();

	invoke_next_program();

	return 0;
}
