#include "led.h"
#include "ch32v30x.h"

#define LED_PORT      GPIOA
#define LED_PIN       GPIO_Pin_3
#define LED_RCC       RCC_APB2Periph_GPIOA

/* Active-low: LOW = lit, HIGH = dark. */
#define LED_ON()      GPIO_ResetBits(LED_PORT, LED_PIN)
#define LED_OFF()     GPIO_SetBits  (LED_PORT, LED_PIN)

static volatile uint8_t s_can_open;
static volatile uint8_t s_identify;

static void led_apply(void)
{
    if (s_identify || s_can_open) LED_ON();
    else                          LED_OFF();
}

void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(LED_RCC, ENABLE);
    /* Drive HIGH first (LED off) to avoid a glimmer between RCC enable and
     * GPIO_Init when the pin is briefly an input — the on-die pull-up will
     * raise the pad before we configure it as output. */
    GPIO_SetBits(LED_PORT, LED_PIN);

    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;     /* status LED — slow edges are fine */
    GPIO_Init(LED_PORT, &gpio);

    s_can_open = 0;
    s_identify = 0;
    LED_OFF();
}

void led_set_can_open(uint8_t open)
{
    s_can_open = open ? 1 : 0;
    led_apply();
}

void led_set_identify(uint8_t identify)
{
    s_identify = identify ? 1 : 0;
    led_apply();
}
