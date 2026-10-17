#include <stdint.h>

extern int main(void);
extern uint32_t _estack;
extern uint32_t _etext;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void WWDG_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void PVD_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FLASH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RCC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI0_1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI2_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI4_15_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TSC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel2_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel4_7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC1_COMP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LPTIM1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART4_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM6_DAC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM21_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM22_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

typedef union {
    uintptr_t address;
    void (*handler)(void);
} VectorEntry;

#define VECTOR_HANDLER(handler_) { .handler = (handler_) }

__attribute__((section(".isr_vector"), used))
VectorEntry const g_vector_table[] = {
    { .address = (uintptr_t)&_estack },
    VECTOR_HANDLER(Reset_Handler),
    VECTOR_HANDLER(NMI_Handler),
    VECTOR_HANDLER(HardFault_Handler),
    { .address = 0u },
    { .address = 0u },
    { .address = 0u },
    { .address = 0u },
    { .address = 0u },
    { .address = 0u },
    { .address = 0u },
    VECTOR_HANDLER(SVC_Handler),
    { .address = 0u },
    { .address = 0u },
    VECTOR_HANDLER(PendSV_Handler),
    VECTOR_HANDLER(SysTick_Handler),
    VECTOR_HANDLER(WWDG_IRQHandler),
    VECTOR_HANDLER(PVD_IRQHandler),
    VECTOR_HANDLER(RTC_IRQHandler),
    VECTOR_HANDLER(FLASH_IRQHandler),
    VECTOR_HANDLER(RCC_IRQHandler),
    VECTOR_HANDLER(EXTI0_1_IRQHandler),
    VECTOR_HANDLER(EXTI2_3_IRQHandler),
    VECTOR_HANDLER(EXTI4_15_IRQHandler),
    VECTOR_HANDLER(TSC_IRQHandler),
    VECTOR_HANDLER(DMA1_Channel1_IRQHandler),
    VECTOR_HANDLER(DMA1_Channel2_3_IRQHandler),
    VECTOR_HANDLER(DMA1_Channel4_7_IRQHandler),
    VECTOR_HANDLER(ADC1_COMP_IRQHandler),
    VECTOR_HANDLER(LPTIM1_IRQHandler),
    VECTOR_HANDLER(USART4_5_IRQHandler),
    VECTOR_HANDLER(TIM2_IRQHandler),
    VECTOR_HANDLER(TIM3_IRQHandler),
    VECTOR_HANDLER(TIM6_DAC_IRQHandler),
    VECTOR_HANDLER(TIM7_IRQHandler),
    VECTOR_HANDLER(TIM21_IRQHandler),
    VECTOR_HANDLER(I2C3_IRQHandler),
    VECTOR_HANDLER(TIM22_IRQHandler),
    VECTOR_HANDLER(I2C1_IRQHandler),
    VECTOR_HANDLER(I2C2_IRQHandler),
    VECTOR_HANDLER(SPI1_IRQHandler),
    VECTOR_HANDLER(SPI2_IRQHandler)
};

#undef VECTOR_HANDLER

void Reset_Handler(void)
{
    uint32_t *source = &_etext;
    uint32_t *destination = &_sdata;
    while (destination < &_edata) {
        *destination++ = *source++;
    }

    for (destination = &_sbss; destination < &_ebss; ++destination) {
        *destination = 0u;
    }

    (void)main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}
