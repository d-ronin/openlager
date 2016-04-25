// OpenLager main
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
#include <string.h>
#include <unistd.h>

#include <ff.h>
#include <led.h>
#include <sdio.h>
#include <usart.h>

#include <stm32f4xx_rcc.h>
#include <systick_handler.h>

#include <lagercfg.h>

const void *_interrupt_vectors[FPU_IRQn] __attribute((section(".interrupt_vectors"))) = {
	[USART1_IRQn] = usart_int_handler
};

static FATFS fatfs;

static uint32_t cfg_baudrate = 230400;

#define CFGFILE_NAME "0:lager.cfg"

// Must have non-digit characters before the digit characters.
#define LOGNAME_FMT "log000.txt"

// Try to load a config file.  If it doesn't exist, create it.
// If we can't load after that, PANNNNIC.
void process_config() {
	FIL cfg_file;

	FRESULT res = f_open(&cfg_file, CFGFILE_NAME, FA_WRITE | FA_CREATE_NEW);

	if (res == FR_OK) {
		UINT wr_len = sizeof(lager_cfg);
		UINT written;
		res = f_write(&cfg_file, lager_cfg, wr_len, &written);

		if (res != FR_OK) {
			led_panic("WCFG");
		}

		if (written != wr_len) {
			led_panic("FULL");
		}

		f_close(&cfg_file);
	} else if (res != FR_EXIST) {
		led_panic("WCFG2");
	}

	res = f_open(&cfg_file, CFGFILE_NAME, FA_READ | FA_OPEN_EXISTING);

	if (res != FR_OK) {
		led_panic("RCFG");
	}

	/* XXX parse config */

	f_close(&cfg_file);
}

static bool is_digit(char c) {
	if (c < '0') return false;
	if (c > '9') return false;

	return true;
}

static int advance_filename(char *f) {
	while (*f && (!is_digit(*f))) {
		f++;
	}

	if (!*f) return -1;	// No digits in string

	while (is_digit(*f)) {
		f++;
	}

	f--;			// Scan backwards

	// Now we're at the last digit.  Increment the number, with ripple
	// carry.

	while (true) {
		// If we're not pointing to a digit currently, we lose
		if (!is_digit(*f)) {
			return -1;
		}

		(*f)++;

		if (is_digit(*f)) {
			// We incremented and don't need to carry.  we win.
			return 0;
		}

		*f = '0';

		f--;
	}

}

static void open_log(FIL *fil) {
	char filename[] = LOGNAME_FMT;
	FRESULT res;

	res = f_open(fil, filename, FA_WRITE | FA_CREATE_NEW);

	while (res == FR_EXIST) {
		if (advance_filename(filename)) {
			// ..-. .. .-.. . ...
			led_panic("FILES");
		}

		res = f_open(fil, filename, FA_WRITE | FA_CREATE_NEW);
	}

	if (res != FR_OK) {
		// --- .-... --- --.
		led_panic("OLOG");
	}

}

static void do_usart_logging(void) {
	char buf[100*1024];

	usart_init(cfg_baudrate, buf, sizeof(buf));

	FIL log_file;

	open_log(&log_file);

	while (1) {
		const char *pos;
		unsigned int amt;

		// 50 ticks == 200ms, prefer 512 byte sector alignment,
		// and >= 2560 byte chunks are best
		pos = usart_receive_chunk(50, 512, 5*512, &amt);

		// Could consider if pos is short, waiting a little longer
		// (400-600ms?) next time...

		led_set(true);	// Illuminate LED during IO

		FRESULT res;

		if (!amt) {
			// If nothing has happened in 200ms, flush our
			// buffers.
			res = f_sync(&log_file);

			if (res != FR_OK) {
				// . .-. .-.
				led_panic("SERR");
			}
		} else {
			UINT written;

			res = f_write(&log_file, pos, amt, &written);

			if (res != FR_OK) {
				// . .-. .-.
				led_panic("WERR");
			}

			if (written != amt) {
				// ..-. ..- .-.. .-..
				led_panic("FULL");
			}
		}

		led_set(false);
	}
}

int main() {
	/* Keep it really simple for now-- just run from 16MHz RC osc,
	 * no wait states, etc. */
	RCC_DeInit();

	// Wait for internal oscillator settle.
	while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);

	// XXX On real hardware: need to start external oscillator,
	// set the PLL source to ext osc.

	// Turn on the external oscillator
	RCC_HSEConfig(RCC_HSE_ON);

	bool osc_err = false;

	if (RCC_WaitForHSEStartUp() == ERROR) {
		// Settle for HSI, and flag error.

		// Program the PLL.
		RCC_PLLConfig(RCC_PLLSource_HSI,
				8,	/* PLLM = /8 = 2MHz */
				96,	/* PLLN = *96 = 192MHz */
				2,	/* PLLP = /2 = 96MHz, slight underclock */
				5	/* PLLQ = /5 = 38.4MHz, underclock SDIO
					 * (Maximum is 48MHz)  Will get a 19.2MHz
					 * SD card clock from dividing by 2, or
					 * 9.6MBps at 4 bits wide.
					 */
			);

		osc_err = true;
	} else {
		// Program the PLL.
		RCC_PLLConfig(RCC_PLLSource_HSE,
				4,	/* PLLM = /4 = 2MHz */
				96,	/* PLLN = *96 = 192MHz */
				2,	/* PLLP = /2 = 96MHz, slight underclock */
				5	/* PLLQ = /5 = 38.4MHz, underclock SDIO
					 * (Maximum is 48MHz)  Will get a 19.2MHz
					 * SD card clock from dividing by 2, or
					 * 9.6MBps at 4 bits wide.
					 */
			);
	}

	// Get the PLL starting.
	RCC_PLLCmd(ENABLE);

	// Program this first, just in case we coasted in here with other periphs
	// already enabled.  The loader does all of this stuff, but who knows,
	// maybe in the future our startup code will do less of this.

	RCC_HCLKConfig(RCC_SYSCLK_Div1);	/* AHB = 96MHz */
	RCC_PCLK1Config(RCC_HCLK_Div2);		/* APB1 = 48MHz (lowspeed domain) */
	RCC_PCLK2Config(RCC_HCLK_Div1);		/* APB2 = 96MHz (fast domain) */
	RCC_TIMCLKPresConfig(RCC_TIMPrescDesactivated);
			/* "Desactivate"... the timer prescaler */

	RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);

	/* SDIO peripheral clocked at 38.4MHz. * 3/8 = minimum APB2 = 14.4MHz,
	 * and we have 96MHz.. so we're good ;) */

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

#if 0
	/* Seize PA14/PA13 from SWD. */
	GPIO_InitTypeDef swd_def = {
		.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_13,
		.GPIO_Mode = GPIO_Mode_IN,	// Input, not AF
		.GPIO_Speed = GPIO_Low_Speed,
		.GPIO_OType = GPIO_OType_PP,
		.GPIO_PuPd = GPIO_PuPd_NOPULL
	};

	GPIO_Init(GPIOA, &swd_def);
#endif

	// Program 3 wait states as necessary at >2.7V for 96MHz
	FLASH_SetLatency(FLASH_Latency_3);

	// Wait for the PLL to be ready.
	while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

	SysTick_Config(96000000/250);	/* 250Hz systick */

	/* Real hardware has LED on PB9 / TIM4_CH4.
	 * Discovery hardware has blue LED on PD15 which can also be TIM4_CH4.
	 * Nucleo F411 has LED on PA5 (source)
	 */
	led_init_pin(GPIOD, GPIO_Pin_15, false);

	if (osc_err) {
		// blink an error; though this is nonfatal.
		led_send_morse("XOSC ");
	}

        if (sd_init(false)) {
                // -.-. .- .-. -..
                led_panic("CARD");
        }

        if (f_mount(&fatfs, "0:", 1) != FR_OK) {
                // -.. .- - .-
                led_panic("DATA ");
        }

	process_config();

	do_usart_logging();

	return 0;
}
