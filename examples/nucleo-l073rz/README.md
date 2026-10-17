# Nucleo-L073RZ framework test app

This is a bare-metal STM32L073RZ example for the Nucleo-L073RZ board. It uses
the board's HSI16 clock, the LD2 user LED on PA5, and USART2 on PA2/PA3, which
is routed to the ST-LINK virtual COM port.

The application exercises the framework in one repeatable loop:

* TIM2, TIM6, and TIM7 interrupt at 137 ms, 293 ms, and 887 ms respectively.
* Each ISR builds a typed fixed-pool `TimerEvent` and posts it to a different
  worker Reactor.
* The workers have strict priorities 1, 2, and 3. Each worker consumes its
  event and posts a typed `SerialLine` to the priority-7 serial Reactor.
* The serial Reactor starts a zero-copy DMA transfer directly from the
  `SerialLine` message payload and toggles LD2 when the transfer completes.
* While DMA is active, new serial messages go to an unregistered deferred
  mailbox. The serial Reactor is therefore awakened only by the DMA-complete
  interrupt, then starts the next deferred transfer.

The execution mode is selected in `zef_nucleo_execution_config.h`; the current
default is the shared-stack preemptive port.

The example is organized by responsibility: `demo_worker_actor.[ch]` contains
the worker state machine, `demo_serial_actor.[ch]` contains the DMA-backed
serial state machine, `demo_timer_source.[ch]` contains timer ISR glue,
`nucleo_l073rz_hw.[ch]` contains board register definitions and hardware
operations, and `demo_app.[ch]` contains the composition and startup wiring.
`main.c` only performs board startup and runs the Kernel.

## Build

Install the Arm GNU toolchain (`arm-none-eabi-gcc`, `arm-none-eabi-objcopy`,
and `arm-none-eabi-size`), then configure from the repository root:

```text
cmake -S . -B build-nucleo -G Ninja -DCMAKE_C_COMPILER=arm-none-eabi-gcc -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DZEF_BUILD_ARM_CM0P_PORT=ON -DZEF_BUILD_NUCLEO_L073RZ_EXAMPLE=ON -DBUILD_TESTING=OFF
cmake --build build-nucleo --target nucleo_l073rz_zef_demo
```

The build produces `.elf`, `.hex`, and `.bin` files in
`build-nucleo/examples/nucleo-l073rz`. Flash the `.bin` at `0x08000000` with
the ST-LINK utility, or use the `.hex` with STM32CubeProgrammer.

Open the ST-LINK virtual COM port at 115200 8-N-1. Expected output is similar
to:

```text
ZEF Nucleo-L073RZ demo ready
FAST event seq=1 period=137ms prio=1
MEDIUM event seq=1 period=293ms prio=2
FAST event seq=2 period=137ms prio=1
SLOW event seq=1 period=887ms prio=3
```
