// STM32F4xx USART peripheral support
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

#include <usart.h>
#include <systick_handler.h>

#define OUR_USART USART1
#define TXPORT GPIOA
#define TXPIN 15
#define RXPORT GPIOB
#define RXPIN 3

static volatile char *usart_rx_buf;
static unsigned int usart_rx_buf_len;
static volatile unsigned int usart_rx_spilled;
static volatile unsigned int usart_rx_buf_wpos;
static volatile unsigned int usart_rx_buf_rpos;
static unsigned int usart_rx_buf_next_rpos;

static void usart_initpin(GPIO_TypeDef *gpio, uint16_t pin_pos) {
	GPIO_InitTypeDef pin_def = {
		.GPIO_Pin = 1 << (pin_pos),
		.GPIO_Mode = GPIO_Mode_AF,
		.GPIO_Speed = GPIO_Fast_Speed,
		.GPIO_OType = GPIO_OType_PP,
		.GPIO_PuPd = GPIO_PuPd_UP
	};

	GPIO_Init(gpio, &pin_def);
	GPIO_PinAFConfig(gpio, pin_pos, GPIO_AF_USART1);
}

static inline unsigned int advance_pos(unsigned int cur_pos, unsigned int amt) {
	cur_pos += amt;
	if (cur_pos > usart_rx_buf_len) {
		cur_pos -= usart_rx_buf_len;
	}

	return cur_pos;
}

static void usart_rxint() {
	// Receive the character ASAP.
	unsigned char c = USART_ReceiveData(OUR_USART);

	unsigned int wpos = usart_rx_buf_wpos;
	unsigned int next_wpos = advance_pos(wpos, 1);

	if (next_wpos == usart_rx_buf_rpos) {
		usart_rx_spilled++;
		return;
	}

	usart_rx_buf[wpos] = c;
	usart_rx_buf_wpos = next_wpos;
}

// RXNE is the interrupt flag
// RXNEIE is the interrupt enable
void usart_int_handler() {
	if (USART_GetITStatus(OUR_USART, USART_IT_RXNE) == SET) {
		usart_rxint();
	}
}

// Logic for return here is as follows:
// 1) Always return in timeout time
// 1a) can return early if the amount exceeds min_preferred_chunk
// 1b) can also return early if we are at the end of the buffer
// 2) If, after timeout, we have at least some we can return that keeps us
// aligned with preferred_align, return it
// 3) else, return everything we have
// It's expected the buffer is a multiple of preferred_align.
// min_preferred_chunk should be >= 2x preferred_align; that way, if we
// are unaligned we can get a complete aligned chunk plus the offset
const char *usart_receive_chunk(unsigned int timeout,
		unsigned int preferred_align,
		unsigned int min_preferred_chunk,
		unsigned int *bytes_returned) {
	unsigned int expiration = systick_cnt + timeout;

	// Release the previously read chunk, so receiving can proceed into it
	unsigned int rpos = usart_rx_buf_next_rpos;
	usart_rx_buf_rpos = rpos;

	unsigned int bytes;

	unsigned int unalign = rpos % preferred_align;

	// Busywait for a completion condition
	do {
		unsigned int wpos = usart_rx_buf_wpos;

		if (wpos < rpos) {
			bytes = usart_rx_buf_len - rpos;
			break;	// case 1b
		}

		bytes = wpos - rpos;

		if (bytes >= min_preferred_chunk) break;
	} while (systick_cnt < expiration);

	if ((bytes + unalign) >= preferred_align) {
		// Fixup for align
		bytes += unalign;

		// Get integral number of align-sized chunks
		bytes /= preferred_align;
		bytes *= preferred_align;

		// Unfixup for align
		bytes -= unalign;
	}

	*bytes_returned = bytes;

	// Next time, we'll release these returned bytes.
	usart_rx_buf_next_rpos = advance_pos(rpos, bytes);

	return (const char *) (usart_rx_buf + rpos);
}

void usart_init(uint32_t baud, void *rx_buf, unsigned int rx_buf_len) {
	usart_rx_buf = rx_buf;
	usart_rx_buf_len = rx_buf_len;

	// program GPIOs
	usart_initpin(TXPORT, TXPIN);
	usart_initpin(RXPORT, RXPIN);

	// Fill out default parameters; stuff in our baudrate
	USART_InitTypeDef usart_params;
	USART_StructInit(&usart_params);

	usart_params.USART_BaudRate = baud;

	// Init the USART
	USART_Init(OUR_USART, &usart_params);

	// Enable the USART
	USART_Cmd(OUR_USART, ENABLE);
}
