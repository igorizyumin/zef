#include "demo_app.h"
#include "nucleo_l073rz_hw.h"

#include "zef.h"
#include "zef_arm_cm0p_port.h"

static DemoApp g_app;

int main(void)
{
    nucleo_clock_init();
    nucleo_gpio_uart_init();
    nucleo_dma_init();

    demo_app_init(&g_app, zef_arm_cm0p_port());
    zef_arm_cm0p_port_init(&g_app.kernel);
    demo_app_post_banner(&g_app);
    demo_app_start_timers(&g_app);

    z_kernel_run_forever(&g_app.kernel);
}

void DMA1_Channel4_7_IRQHandler(void)
{
    if (nucleo_uart_dma_irq_status() != 0u) {
        nucleo_uart_dma_irq_clear();
        demo_serial_actor_dma_complete_isr(&g_app.serial);
    }
}

void TIM2_IRQHandler(void)
{
    demo_timer_source_irq(&g_app.timers[0]);
}

void TIM6_DAC_IRQHandler(void)
{
    demo_timer_source_irq(&g_app.timers[1]);
}

void TIM7_IRQHandler(void)
{
    demo_app_clock_tick_isr(&g_app);
}
