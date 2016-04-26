#include <led.h>
#include <systick_handler.h>
#include <morsel.h>

static GPIO_TypeDef *led_gpio;
static uint16_t led_pin;
static bool led_sense;
static int led_time_per_dot = 36;

void led_toggle()
{
	GPIO_ToggleBits(led_gpio, led_pin);
}

void led_set(bool light)
{
	if (led_sense ^ light) {
		GPIO_SetBits(led_gpio, led_pin);
	} else {
		GPIO_ResetBits(led_gpio, led_pin);
	}
}

void led_send_morse(char *string)
{
	char *pos = string;
	int val;

	uint32_t state = 0;

	uint32_t next = systick_cnt;

	while ((val = morse_send(&pos, &state)) != -1) {
		while (systick_cnt < next);

		next += led_time_per_dot;

		led_set(val > 0);
	}
}

void led_panic(char *string)
{
	while (true) {
		led_send_morse(string);
		led_send_morse("  ");
	}
}

void led_set_morse_speed(int time_per_dot)
{
	led_time_per_dot = time_per_dot;
}

void led_init_pin(GPIO_TypeDef *GPIOx, uint16_t GPIO_pin, bool sense)
{
	GPIO_InitTypeDef led_def;

	GPIO_StructInit(&led_def);

	led_def.GPIO_Pin = GPIO_pin;
	led_def.GPIO_Mode = GPIO_Mode_OUT;
	led_def.GPIO_Speed = GPIO_Low_Speed;
	led_def.GPIO_OType = GPIO_OType_PP;
	led_def.GPIO_PuPd = GPIO_PuPd_NOPULL;

	GPIO_Init(GPIOx, &led_def);

	led_sense = sense;
	led_gpio = GPIOx;
	led_pin = GPIO_pin;

	led_set(false);
}
