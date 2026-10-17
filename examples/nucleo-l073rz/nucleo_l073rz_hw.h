#ifndef NUCLEO_L073RZ_HW_H
#define NUCLEO_L073RZ_HW_H

#include <stdint.h>

typedef struct {
    volatile uint32_t CR;
    volatile uint32_t ICSCR;
    volatile uint32_t CRRCR;
    volatile uint32_t CFGR;
    volatile uint32_t CIER;
    volatile uint32_t CIFR;
    volatile uint32_t CICR;
    volatile uint32_t IOPRSTR;
    volatile uint32_t AHBRSTR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t APB1RSTR;
    volatile uint32_t IOPENR;
    volatile uint32_t AHBENR;
    volatile uint32_t APB2ENR;
    volatile uint32_t APB1ENR;
} RccRegisters;

typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
} GpioRegisters;

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t BRR;
    uint32_t reserved0[3];
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t RDR;
    volatile uint32_t TDR;
} UsartRegisters;

typedef struct {
    volatile uint32_t CCR;
    volatile uint32_t CNDTR;
    volatile uint32_t CPAR;
    volatile uint32_t CMAR;
} DmaChannelRegisters;

typedef struct {
    volatile uint32_t CR1;
    uint32_t reserved0[2];
    volatile uint32_t DIER;
    volatile uint32_t SR;
    volatile uint32_t EGR;
    uint32_t reserved1[4];
    volatile uint32_t PSC;
    volatile uint32_t ARR;
} TimerRegisters;

#define RCC ((RccRegisters *)0x40021000u)
#define GPIOA ((GpioRegisters *)0x50000000u)
#define USART2 ((UsartRegisters *)0x40004400u)
#define DMA1_CHANNEL4 ((DmaChannelRegisters *)0x40020044u)
#define DMA1_ISR (*(volatile uint32_t *)0x40020000u)
#define DMA1_IFCR (*(volatile uint32_t *)0x40020004u)
#define DMA1_CSELR (*(volatile uint32_t *)0x400200A8u)
#define TIM2 ((TimerRegisters *)0x40000000u)
#define TIM6 ((TimerRegisters *)0x40001000u)
#define TIM7 ((TimerRegisters *)0x40001400u)
#define NVIC_ISER ((volatile uint32_t *)0xE000E100u)

#define RCC_CR_HSI16ON (1u << 0)
#define RCC_CR_HSI16RDY (1u << 2)
#define RCC_CFGR_SW_MASK 3u
#define RCC_CFGR_SW_HSI16 1u
#define RCC_CFGR_SWS_MASK (3u << 2)
#define RCC_CFGR_SWS_HSI16 (1u << 2)
#define RCC_IOPENR_GPIOAEN (1u << 0)
#define RCC_AHBENR_DMA1EN (1u << 0)
#define RCC_APB1ENR_TIM2EN (1u << 0)
#define RCC_APB1ENR_TIM6EN (1u << 4)
#define RCC_APB1ENR_TIM7EN (1u << 5)
#define RCC_APB1ENR_USART2EN (1u << 17)

#define USART_CR1_UE (1u << 0)
#define USART_CR1_TE (1u << 3)
#define USART_CR3_DMAT (1u << 7)
#define DMA_CSELR_USART2 (4u << 12)
#define DMA_CCR_EN (1u << 0)
#define DMA_CCR_TCIE (1u << 1)
#define DMA_CCR_TEIE (1u << 3)
#define DMA_CCR_DIR (1u << 4)
#define DMA_CCR_MINC (1u << 7)
#define DMA_CCR_PL_HIGH (1u << 12)
#define DMA_IFCR_CLEAR4 ((1u << 12) | (1u << 13) | (1u << 14) | (1u << 15))
#define TIM_CR1_CEN (1u << 0)
#define TIM_DIER_UIE (1u << 0)
#define TIM_SR_UIF (1u << 0)
#define TIM_EGR_UG (1u << 0)

typedef enum {
    NUCLEO_TIMER_2,
    NUCLEO_TIMER_6,
    NUCLEO_TIMER_7
} NucleoTimerId;

enum {
    NUCLEO_LED_PIN = 5u,
    NUCLEO_UART_BAUD = 115200u,
    NUCLEO_CPU_HZ = 16000000u
};

enum {
    NUCLEO_DMA_ISR_TCIF4 = (1u << 13),
    NUCLEO_DMA_ISR_TEIF4 = (1u << 15)
};

void nucleo_clock_init(void);
void nucleo_gpio_uart_init(void);
void nucleo_dma_init(void);
void nucleo_led_toggle(void);

void nucleo_timer_start(NucleoTimerId timer, uint32_t interval_ms);
uint32_t nucleo_timer_irq_status(NucleoTimerId timer);
void nucleo_timer_irq_clear(NucleoTimerId timer);
void nucleo_timer_irqs_enable(void);

void nucleo_uart_dma_start(char const *data, uint16_t length);
void nucleo_uart_dma_stop(void);
uint32_t nucleo_uart_dma_irq_status(void);
void nucleo_uart_dma_irq_clear(void);

#endif
