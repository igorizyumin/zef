#include "nucleo_l073rz_hw.h"

#include "zef.h"

static TimerRegisters *nucleo_timer_register(NucleoTimerId timer)
{
    switch (timer) {
        case NUCLEO_TIMER_2:
            return TIM2;
        case NUCLEO_TIMER_6:
            return TIM6;
        case NUCLEO_TIMER_7:
            return TIM7;
        default:
            return TIM2;
    }
}

void nucleo_clock_init(void)
{
    RCC->CR |= RCC_CR_HSI16ON;
    while ((RCC->CR & RCC_CR_HSI16RDY) == 0u) {
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MASK) | RCC_CFGR_SW_HSI16;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_HSI16) {
    }
}

void nucleo_gpio_uart_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    GPIOA->MODER = (GPIOA->MODER & ~(3u << (NUCLEO_LED_PIN * 2u))) |
                   (1u << (NUCLEO_LED_PIN * 2u));
    GPIOA->BSRR = 1u << (NUCLEO_LED_PIN + 16u);
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 4u) | (3u << 6u))) |
                   (2u << 4u) | (2u << 6u);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~((0xFu << 8u) | (0xFu << 12u))) |
                    (4u << 8u) | (4u << 12u);
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    USART2->BRR = (NUCLEO_CPU_HZ + (NUCLEO_UART_BAUD / 2u)) /
                  NUCLEO_UART_BAUD;
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
}

void nucleo_dma_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_CSELR = (DMA1_CSELR & ~(0xFu << 12u)) | DMA_CSELR_USART2;
    DMA1_CHANNEL4->CCR = 0u;
    DMA1_IFCR = DMA_IFCR_CLEAR4;
}

void nucleo_led_toggle(void)
{
    GPIOA->ODR ^= 1u << NUCLEO_LED_PIN;
}

void nucleo_timer_start(NucleoTimerId timer, uint32_t interval_ms)
{
    TimerRegisters *registers = nucleo_timer_register(timer);
    switch (timer) {
        case NUCLEO_TIMER_2:
            RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
            break;
        case NUCLEO_TIMER_6:
            RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
            break;
        case NUCLEO_TIMER_7:
            RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
            break;
        default:
            break;
    }
    registers->PSC = (NUCLEO_CPU_HZ / 1000u) - 1u;
    registers->ARR = interval_ms - 1u;
    registers->EGR = TIM_EGR_UG;
    registers->SR = 0u;
    registers->DIER = TIM_DIER_UIE;
    registers->CR1 = TIM_CR1_CEN;
}

uint32_t nucleo_timer_irq_status(NucleoTimerId timer)
{
    return nucleo_timer_register(timer)->SR & TIM_SR_UIF;
}

void nucleo_timer_irq_clear(NucleoTimerId timer)
{
    nucleo_timer_register(timer)->SR &= ~TIM_SR_UIF;
}

void nucleo_timer_irqs_enable(void)
{
    NVIC_ISER[0] = (1u << 11u) | (1u << 15u) | (1u << 17u) | (1u << 18u);
}

void nucleo_uart_dma_start(char const *data, uint16_t length)
{
    DMA1_CHANNEL4->CCR = 0u;
    DMA1_IFCR = DMA_IFCR_CLEAR4;
    DMA1_CHANNEL4->CPAR = (uint32_t)(uintptr_t)&USART2->TDR;
    DMA1_CHANNEL4->CMAR = (uint32_t)(uintptr_t)data;
    DMA1_CHANNEL4->CNDTR = length;
    DMA1_CHANNEL4->CCR = DMA_CCR_TCIE | DMA_CCR_TEIE |
                         DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_PL_HIGH;
    USART2->CR3 |= USART_CR3_DMAT;
    DMA1_CHANNEL4->CCR |= DMA_CCR_EN;
}

void nucleo_uart_dma_stop(void)
{
    DMA1_CHANNEL4->CCR &= ~DMA_CCR_EN;
    USART2->CR3 &= ~USART_CR3_DMAT;
}

uint32_t nucleo_uart_dma_irq_status(void)
{
    return DMA1_ISR & (NUCLEO_DMA_ISR_TCIF4 | NUCLEO_DMA_ISR_TEIF4);
}

void nucleo_uart_dma_irq_clear(void)
{
    DMA1_IFCR = DMA_IFCR_CLEAR4;
}

_Noreturn void z_panic(ZPanicInfo const *info)
{
    (void)info;
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    GPIOA->MODER = (GPIOA->MODER & ~(3u << (NUCLEO_LED_PIN * 2u))) |
                   (1u << (NUCLEO_LED_PIN * 2u));
    for (;;) {
        GPIOA->ODR ^= 1u << NUCLEO_LED_PIN;
        for (volatile uint32_t delay = 0u; delay < 160000u; ++delay) {
        }
    }
}
