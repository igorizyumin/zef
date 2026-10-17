#include "zef_arm_cm0p_port.h"

#include "../../src/zef_port_internal.h"

#include <stddef.h>

#define ZEF_ARM_CM0P_SCB_ICSR ((uintptr_t)0xE000ED04u)
#define ZEF_ARM_CM0P_SCB_SHPR3 ((uintptr_t)0xE000ED20u)
#define ZEF_ARM_CM0P_PENDSVSET (1u << 28)
#define ZEF_ARM_CM0P_PENDSVCLR (1u << 27)
#define ZEF_ARM_CM0P_NMIPENDSET (1u << 31)

#if ZEF_EXECUTION_MODE == ZEF_EXECUTION_PREEMPTIVE
static ZKernel *zef_arm_cm0p_kernel;
#endif

ZCriticalState z_port_critical_enter(void)
{
    ZCriticalState previous;
    /* Read the current global interrupt mask so nested critical sections can
     * restore exactly the state that was present on entry. */
    /* MRS: copy PRIMASK into the C variable named by the output operand. */
    __asm volatile("mrs %0, primask" : "=r"(previous) :: "memory");
    /* CPSID I: mask configurable exceptions while the Kernel state changes. */
    __asm volatile("cpsid i" ::: "memory");
    return previous;
}

void z_port_critical_leave(ZCriticalState state)
{
    /* MSR: restore the saved PRIMASK, re-enabling interrupts only when they
     * were enabled before the matching critical_enter call. */
    __asm volatile("msr primask, %0" :: "r"(state) : "memory");
}

static void zef_arm_cm0p_request_reschedule(void)
{
#if ZEF_EXECUTION_MODE == ZEF_EXECUTION_PREEMPTIVE
    if (zef_arm_cm0p_kernel != NULL) {
        *(volatile uint32_t *)ZEF_ARM_CM0P_SCB_ICSR = ZEF_ARM_CM0P_PENDSVSET;
    }
#endif
}

static void zef_arm_cm0p_idle(void)
{
    /* WFI: sleep until an interrupt or exception makes more work possible. */
    __asm volatile("wfi" ::: "memory");
}

static ZPort const zef_arm_cm0p_port_instance = {
    zef_arm_cm0p_request_reschedule,
    zef_arm_cm0p_idle
};

ZPort const *zef_arm_cm0p_port(void)
{
    return &zef_arm_cm0p_port_instance;
}

void zef_arm_cm0p_port_init(ZKernel *kernel)
{
    if (kernel == NULL) {
        ZPanicInfo info = {
            Z_PANIC_INVALID_ARGUMENT, kernel, 0u, __FILE__, (uint32_t)__LINE__
        };
        z_panic(&info);
    }

#if ZEF_EXECUTION_MODE == ZEF_EXECUTION_PREEMPTIVE
    zef_arm_cm0p_kernel = kernel;

    /* PendSV priority is the least urgent implemented system-handler level. */
    *(volatile uint8_t *)(ZEF_ARM_CM0P_SCB_SHPR3 + 2u) = 0xFFu;
#else
    (void)kernel;
#endif
}

#if ZEF_EXECUTION_MODE == ZEF_EXECUTION_PREEMPTIVE
/* @relation(ZEF-IMP-074, scope=shared-stack-exception-boundary) */
/*
 * PendSV synthesizes an exception frame that enters the scheduler in Thread
 * mode on MSP. The scheduler enables interrupts while higher-priority
 * activations execute, then requests an NMI-based return to the interrupted
 * exception frame. This is the single-stack preemption shape used by QK.
 */
__attribute__((noinline, used)) static void zef_arm_cm0p_activate(void);
__attribute__((noreturn, naked, used)) static void zef_arm_cm0p_resume(void);

__attribute__((noinline, used)) static void zef_arm_cm0p_activate(void)
{
    __asm volatile("cpsie i" ::: "memory");
    if (zef_arm_cm0p_kernel != NULL) {
        zef_kernel_activate_higher(zef_arm_cm0p_kernel);
    }
    __asm volatile("cpsid i" ::: "memory");
    /* Do not call zef_arm_cm0p_resume().  PendSV fabricated its address as
     * this function's stacked link register.  Returning through the normal
     * compiler epilogue pops that link register and branches to the helper,
     * preserving the single-stack QK return sequence. */
}

/* PendSV preserves the interrupted exception frame below a temporary
 * synthesized frame that enters zef_arm_cm0p_activate in Thread mode. */
__attribute__((naked, used)) void PendSV_Handler(void)
{
    __asm volatile(
        /* Save r0 and the original exception-return token held in lr. */
        "push {r0,lr}\n"
        /* Mask interrupts while the temporary activation frame is assembled. */
        "cpsid i\n"
        /* Point r0 at the System Control Block Interrupt Control and State
         * Register, which controls PendSV pending state. */
        "ldr r0,=0xE000ED04\n"
        /* Load the write value for ICSR.PENDSVCLR (bit 27). */
        "ldr r1,=0x08000000\n"
        "str r1,[r0]\n"
        /* Load the activation entry address and clear its Thumb tag. */
        "ldr r2,=zef_arm_cm0p_activate\n"
        "movs r3,#1\n"
        "bic r2,r3\n"
        /* Load the return helper as the stacked link register. */
        "ldr r1,=zef_arm_cm0p_resume\n"
        /* Reserve eight words for the synthetic hardware exception frame. */
        "sub sp,#32\n"
        /* Store stacked lr = resume, stacked pc = activate, and xPSR Thumb. */
        "mov r0,sp\n"
        "movs r3,#20\n"
        "add r0,r3\n"
        "ldr r3,=0x01000000\n"
        "stmia r0!,{r1-r3}\n"
        /* Return to Thread mode using MSP and unstack the synthetic frame. */
        "movs r0,#6\n"
        "mvn r0,r0\n"
        "bx r0\n"
        ::: "memory"
    );
}

/* The activation routine returns here with PRIMASK set. A pending NMI
 * discards the temporary frame and resumes the original exception frame. */
__attribute__((noreturn, naked, used)) static void zef_arm_cm0p_resume(void)
{
    __asm volatile(
        "ldr r0,=0xE000ED04\n"
        "ldr r1,=0x80000000\n"
        "str r1,[r0]\n"
        "b .\n"
        ::: "memory"
    );
}

__attribute__((naked, used)) void NMI_Handler(void)
{
    __asm volatile(
        "add sp,#32\n"
        "cpsie i\n"
        "pop {r0,pc}\n"
    );
}
#endif
