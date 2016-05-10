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

#include <jsmn.h>

#include <lagercfg.h>

#ifndef MIN
#define MIN(a,b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a < _b ? _a : _b; })
#endif

const void *_interrupt_vectors[FPU_IRQn] __attribute((section(".interrupt_vectors"))) = {
	[USART1_IRQn] = usart_int_handler
};

static FATFS fatfs;

static uint32_t cfg_baudrate = 115200;
static uint32_t cfg_prealloc = 0;
static uint32_t cfg_prealloc_grow = false;

#define CFGFILE_NAME "lager.cfg"

// Must have non-digit characters before the digit characters.
#define LOGNAME_FMT "log000.txt"

/* Configuration functions */
static int parse_num(const char *cfg_buf, jsmntok_t *t) {
	int value = 0;
	bool neg = false;

	for (int pos = t->start; pos < t->end; pos++) {
		char c = cfg_buf[pos];

		if ((c == '-') && (pos == t->start)) {
			neg = true;
			continue;
		}

		if ((c < '0') || (c > '9')) {
			led_panic("?");
		}

		value *= 10;

		value += c - '0';
	}

	if (neg) {
		return -value;
	}

	return value;
}

static bool parse_bool(const char *cfg_buf, jsmntok_t *t) {
	switch (cfg_buf[t->start]) {
		case 't':
		case 'T':
			return true;
		case 'f':
		case 'F':
			return false;
		default:
			led_panic("?");
	}

	return false;	// Unreachable
}

static inline bool compare_key(const char *cfg_buf, jsmntok_t *t,
		const char *value, jsmntype_t typ) {
	if ((t->end - t->start) != strlen(value)) {
		return false;
	}

	// Technically we should be case sensitive, but.. Meh!
	if (strncasecmp(value, cfg_buf + t->start,
				t->end - t->start)) {
		return false;
	}

	// OK, the key matches.  Is the value of the expected type? 
	// if not, panic (invalid config)
	if (typ != t[1].type) {
		led_panic("?");
	}

	return true;
}

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

	char cfg_buf[4096];

	char cfg_morse[128];
	cfg_morse[0] = 0;

	UINT amount;

        if (FR_OK != f_read(&cfg_file, cfg_buf, sizeof(cfg_buf), &amount)) {
		led_panic("RCFG");
	}

	if (amount == 0 || amount >= sizeof(cfg_buf)) {
		led_panic("RCFG");
	}

	jsmntok_t tokens[100];
	jsmn_parser parser;

	jsmn_init(&parser);

	/* parse config */
	int num_tokens = jsmn_parse(&parser, cfg_buf, amount, tokens, 100);

	// Minimal should be JSMN_OBJECT 
	if (num_tokens < 1) {
		// ..--..
		led_panic("?");
	}

	int skip_count = 0;

	if (tokens[0].type != JSMN_OBJECT) {
		// ..--..
		led_panic("?");
	}

	// tokens-1 to guarantee that there's always room for a value after
	// our string key..
	for (int i=1; i<(num_tokens-1); i++) {
		jsmntok_t *t = tokens + i;
		jsmntok_t *next = t + 1;

		if (skip_count) {
			skip_count--;

			if ((t->type == JSMN_ARRAY) ||
					(t->type == JSMN_OBJECT)) {
				skip_count += t->size;
			}

			continue;
		}

		if (t->type != JSMN_STRING) {
			led_panic("?");
		}

		/* OK, it's a string. */

		if (compare_key(cfg_buf, t, "startupMorse", JSMN_STRING)) {
			int len = MIN(next->end - next->start, sizeof(cfg_morse)-1);

			memcpy(cfg_morse, cfg_buf + next->start, len);
			cfg_morse[len] = 0;
		} else if (compare_key(cfg_buf, t, "useSPI", JSMN_PRIMITIVE)) {
			if (parse_bool(cfg_buf, next)) {
				// XXX SPI not supported yet
				led_panic("?SPI?");
			}
		} else if (compare_key(cfg_buf, t, "baudRate", JSMN_PRIMITIVE)) {
			cfg_baudrate = parse_num(cfg_buf, next);
		} else if (compare_key(cfg_buf, t, "preallocBytes", JSMN_PRIMITIVE)) {
			cfg_prealloc = parse_num(cfg_buf, next);
		} else if (compare_key(cfg_buf, t, "preallocGrow", JSMN_PRIMITIVE)) {
			cfg_prealloc_grow = parse_bool(cfg_buf, next);
		}

		i++;	// Skip the value too on next iter.
	}

	f_close(&cfg_file);

	if (cfg_morse[0]) {
		led_send_morse(cfg_morse);
	}
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

		// This is likely n^2 or worse with number of config files
		res = f_open(fil, filename, FA_WRITE | FA_CREATE_NEW);
	}

	if (res != FR_OK) {
		// --- .-... --- --.
		led_panic("OLOG");
	}

	if (cfg_prealloc > 0) {
		// Attempt to preallocate contig space for the logfile
		// Best effort only-- figure it's better to keep going if
		// we can't alloc it at all.

		f_expand(fil, cfg_prealloc, cfg_prealloc_grow ? 1 : 0);
	}

}

static void do_usart_logging(void) {
	char buf[125*1024];

	usart_init(cfg_baudrate, buf, sizeof(buf));

	FIL log_file;

	open_log(&log_file);

	while (1) {
		const char *pos;
		unsigned int amt;

		// 50 ticks == 200ms, prefer 512 byte sector alignment,
		// and >= 2560 byte chunks are best
		// Never get more than about 2/5 of the buffer (40 * 1024)--
		// because we want to finish the IO and free it up
		pos = usart_receive_chunk(50, 512, 5*512,
				40*1024, &amt);

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
	RCC_DeInit();

	// Wait for internal oscillator settle.
	while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);

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
			RCC_AHB1Periph_GPIOE |
			RCC_AHB1Periph_DMA2,
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
		.GPIO_Mode = GPIO_Mode_IN,	// Input, not AF
		.GPIO_Speed = GPIO_Low_Speed,
		.GPIO_OType = GPIO_OType_PP,
		.GPIO_PuPd = GPIO_PuPd_NOPULL
	};

	GPIO_Init(GPIOA, &swd_def);

	// Program 3 wait states as necessary at >2.7V for 96MHz
	FLASH_SetLatency(FLASH_Latency_3);

	// Wait for the PLL to be ready.
	while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET);

	SysTick_Config(96000000/250);	/* 250Hz systick */

	/* Real hardware has LED on PB9. (sink on) */
	led_init_pin(GPIOB, GPIO_Pin_9, true);

	/* Discovery hardware has blue LED on PD15. */
	/* led_init_pin(GPIOD, GPIO_Pin_15, false); */

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
