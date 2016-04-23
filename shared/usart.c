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

#define OUR_USART USART1
#define TXPORT GPIOA
#define TXPIN 15
#define RXPORT GPIOB
#define RXPIN 3

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


void usart_init(uint32_t baud) {
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
