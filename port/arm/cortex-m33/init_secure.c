#include <stdint.h>

#include "arch_ops.h"
#include "utils.h"

// MPS2 AN505 Secure Attribution Unit (SAU) and Peripheral Protection Controller (PPC) Definitions
#define SAU_CTRL (*((volatile uint32_t *)0xE000EDD0U)) // Control Register
#define SAU_RNR (*((volatile uint32_t *)0xE000EDD8U))  // Region Number Register
#define SAU_RBAR (*((volatile uint32_t *)0xE000EDDCU)) // Region Base Address Register
#define SAU_RLAR (*((volatile uint32_t *)0xE000EDE0U)) // Region Limit Address Register

// System Control Block (SCB) register definitions for MPS2 AN505
#define SCB_VTOR_SE (*((volatile uint32_t *)0xE000ED08U)) // Secure Vector Table Offset Register
#define SCB_VTOR_NS (*((volatile uint32_t *)0xE002ED08U)) // Non-Secure Vector Table Offset Register Alias

#define SCB_AIRCR (*((volatile uint32_t *)0xE000ED0CU)) // Application Interrupt and Reset Control Register
#define VECTKEY 0x05FA0000
#define BFHFNMINS BIT(13) // Bit 13: BusFault, HardFault, and NMI targeting
#define PRIS BIT(14)      // Bit 14: Prioritize Secure Exceptions

#define SCB_SHCSR_SE (*((volatile uint32_t *)0xE000ED24U)) // Secure System Handler Control and State Register
#define SCB_SHCSR_NS (*((volatile uint32_t *)0xE002ED24U)) // SE view to Non-Secure System Handler Control and State Register
#define SCB_DEMCR (*((volatile uint32_t *)0xE000EDFCU))    // Debug Exception and Monitor Control Register

#define SCB_DAUTHCTRL_SE (*((volatile uint32_t *)0xE000EE04U)) // Debug Authentication Control Register
#define SCB_DAUTHCTRL_NS (*((volatile uint32_t *)0xE002EE04U)) // Debug Authentication Control Register

// Nested Vectored Interrupt Controller (NVIC) Definitions
#define NVIC_ITNS ((volatile uint32_t *)0xE000E380U)    // Interrupt Target Non-secure Register (ITNS) for MPS2 AN505
#define NVIC_ICER_NS ((volatile uint32_t *)0xE002E180U) // Non-Secure Interrupt Clear-Enable Register 0
#define NVIC_ICPR_NS ((volatile uint32_t *)0xE002E280U) // Non-Secure Interrupt Clear-Pending Register 0
#define NVIC_ISER_NS ((volatile uint32_t *)0xE002E100U) // Non-Secure Interrupt Set Enable Register

// MPS2 AN505 IoT Kit Security Control Block (SECCTL) Specific Peripheral Protection Controllers (PPC)
#define IOTKIT_SECCTL_BASE_NS 0x40080000U // Non-Secure PPC configuration window
#define IOTKIT_SECCTL_BASE_SE 0x50080000U // Secure PPC configuration window
#define IOTKIT_SECCTL_BASE IOTKIT_SECCTL_BASE_SE

// Main System Non-Secure PPC Configuration Registers
#define AHB_NSPPC0 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x50)))
#define APB_NSPPC0 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x70))) // Controls Timers
#define APB_NSPPC1 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x74)))

// Expansion Non-Secure PPC Configuration Registers
#define APB_NSPPCEXP0 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x80))) // 0x80 - ssram mpcs
#define APB_NSPPCEXP1 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x84))) // 0x84 - Controls UART0-4, SPI, I2C
#define APB_NSPPCEXP2 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x88)))
#define APB_NSPPCEXP3 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x8C)))

#define APB_PRIV0 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x90)))
#define APB_PRIVEXP0 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0xA4)))
#define APB_PRIVEXP1 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0xA8)))
#define APB_PRIVEXP2 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0xAC)))
#define APB_PRIVEXP3 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0xB0)))

#define APB_PPCEXPINTCLR0 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x230)))
#define APB_PPCEXPINTCLR1 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x234))) // Handshake for UART0-4
#define APB_PPCEXPINTCLR2 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x238)))
#define APB_PPCEXPINTCLR3 (*((volatile uint32_t *)(IOTKIT_SECCTL_BASE + 0x23C)))

// Coprocessor Access Control Register (CPACR) for FPU access
#define CPACR_NS (*((volatile uint32_t *)0xE002ED88U))
#define CPACR_SE (*((volatile uint32_t *)0xE000ED88U))

// Floating Point Context Control Register (FPCCR)
#define FPCCR_NS (*((volatile uint32_t *)0xE002EF34U))
#define FPCCR_SE (*((volatile uint32_t *)0xE000EF34U))

// Non-Secure Access Control Register (NSACR)
#define SECURITY_EXTENSION_UNSUPPORTED 0x00000CFF
#define NSACR (*((volatile uint32_t *)0xE000ED8CU))

// Memory Protection Unit (MPU) Register Definitions
#define MPU_CTRL_SE (*((volatile uint32_t *)0xE000ED94U)) // MPU Control Register
#define MPU_CTRL_NS (*((volatile uint32_t *)0xE002ED94U)) // Non-Secure MPU Control Register

// MPS2 AN505 Memory Protection Controller (MPC) Definitions
typedef struct {
    volatile uint32_t CTRL; // Offset: 0x000 Control Register
    uint32_t          RESERVED[3];
    volatile uint32_t BLK_MAX;   // Offset: 0x010 Block Maximum Register
    volatile uint32_t BLK_CFG;   // Offset: 0x014 Block Configuration Register
    volatile uint32_t BLK_IDX;   // Offset: 0x018 Block Index Register
    volatile uint32_t BLK_LUT;   // Offset: 0x01C Block Lookup Table Register
    volatile uint32_t INT_STAT;  // Offset: 0x020 Interrupt Status Register
    volatile uint32_t INT_CLEAR; // Offset: 0x024 Interrupt Clear Register
} MPC_t;

// MPCs access for MPS2-AN505
#define MPC_FLASH ((MPC_t *)0x58007000) // Controls ssram-0 (Flash window)
#define MPC_SRAM1 ((MPC_t *)0x58008000) // Controls ssram-1 (Main application RAM)
#define MPC_SRAM2 ((MPC_t *)0x58009000) // Controls ssram-2 (Extended RAM)

// Application Constants
#define NS_FLASH_BASE 0x00020000U        // Non-Secure Flash Base Address
#define FLASH_ALIAS 0x10000000U          // Secure Flash Base Address Alias
#define NS_IMAGE_FLASH_ADDR 0x00020000U  // Non-Secure Flash Address (where the NS code/data is loaded from)
#define NS_IMAGE_FLASH_ALIAS 0x10020000U // Non-Secure Flash Address (where the NS code/data is loaded from)
#define NS_IMAGE_RAM_ADDR 0x28004000U    // Non-Secure RAM Address (where the NS code/data is loaded to)
#define NS_IMAGE_RAM_ALIAS 0x38004000U   // Non-Secure RAM Address Alias (where the NS code/data is loaded to)
#define EXT_RAM_BASE 0x28200000U
#define EXT_RAM_ALIAS 0x38200000U
#define FLASH_SIZE_BYTES (4 * 1024 * 1024)                                       // 4MB ssram-0 capacity
#define RAM_BANK_SIZE_BYTES (2 * 1024 * 1024)                                    // 2MB bank size
#define RAM_BANK0_SIZE_BYTES (RAM_BANK_SIZE_BYTES)                               // 2MB ssram-1 capacity
#define RAM_BANK1_SIZE_BYTES (RAM_BANK_SIZE_BYTES)                               // 2MB ssram-2 capacity
#define FLASH_SE_MEM_SIZE_BYTES (128 * 1024)                                     // 128KB of Secure code/data in FLASH
#define FLASH_NS_MEM_SIZE_BYTES (FLASH_SIZE_BYTES - FLASH_SE_MEM_SIZE_BYTES)     // 4MB - 128KB of Non-Secure code/data in FLASH
#define SRAM1_SE_MEM_SIZE_BYTES (16 * 1024)                                      // 16KB of Secure code/data in SRAM1
#define SRAM1_NS_MEM_SIZE_BYTES (RAM_BANK0_SIZE_BYTES - SRAM1_SE_MEM_SIZE_BYTES) // 2MB - 16KB of Non-Secure code/data in RAM Bank 0
#define SRAM2_NS_MEM_SIZE_BYTES (RAM_BANK1_SIZE_BYTES)                           // 2MB of Non-Secure code/data in RAM Bank 1

void secure_reset_handler(void);
void nmi_se_handler(void);
void hard_fault_se_handler(void);
void mem_manage_se_handler(void);
void bus_fault_se_handler(void);
void usage_fault_se_handler(void);
void secure_fault_se_handler(void);

// -- Globals --

struct ns_data {
    uint32_t ns_sp;
    uint32_t ns_pc;
} store;

extern uint32_t    _estack;
static const char *msg = nullptr;

// -- End of globals --

__attribute__((naked, section(".text.boot"))) void secure_boot_entry(void)
{
    __asm volatile(
        "movs r2, #0\n\t" // Set Non-Secure CONTROL register to 0 (privileged, SP main)
        "msr control_ns, r2\n\t"
        "isb\n\t"
        "ldr r0, =_estack\n\t" // Fetch the Secure stack end
        "msr msp, r0\n\t"      // Initialize Main Stack Pointer
        "dsb\n\t"
        "isb\n\t"
        "b secure_reset_handler\n\t" // Jump to secure reset handler
        :
        :
        : "memory"
    );
}

// clang-format off
__attribute__((section(".secure_vector_table"), aligned(8), used))
void (*const secure_vector_table[])(void) = {
    (void (*)(void))&_estack,   // 0: Initial Stack Pointer
    secure_boot_entry,          // 1: Reset
    nmi_se_handler,             // 2: NMI
    hard_fault_se_handler,      // 3: Hard Fault
    mem_manage_se_handler,      // 4: MPU
    bus_fault_se_handler,       // 5: Bus Fault
    usage_fault_se_handler,     // 6: Usage Fault
    secure_fault_se_handler,    // 7: Secure Fault
    0, 0, 0, 0,                 // 8-11: Reserved
    0, 0, 0, 0                  // 12-15: Reserved
};
// clang-format on

void halt(const char *error_msg)
{
    msg = error_msg;

    while (true) {
        HALT_CPU();
    }
}

// Configure Secure Attribution Unit (SAU) Region for Non-Secure access
void cfg_sau_region(uint32_t rnr, uint32_t rbar, uint32_t rlar)
{
    SAU_RNR = rnr;
    SAU_RBAR = rbar;
    SAU_RLAR = rlar;
    __asm volatile("dsb sy; isb sy" ::: "memory");
}

// Initialize the Memory Protection Controller (MPC) for memory access
// Configure the Look-Up Table (LUT) in memory block size increments.
// Each index (i) controls a 32-bit bitmap register. Each bit inside BLK_LUT
// represents a physical memory block (0/1 = Secure/Non-Secure execution allowed).
// Writing 0xFFFFFFFF allocates 32 contiguous blocks to the Non-Secure state.
void init_mpc(MPC_t *mpc_sram, uint32_t se_size_bytes, uint32_t ns_size_bytes)
{
    mpc_sram->CTRL |= BIT(4);  // Disable the Secure Fault or a fatal system Lockup Exception
    mpc_sram->CTRL &= ~BIT(8); // Disable auto-increment for manual block configuration
    __asm volatile("dsb; isb");

    uint32_t          max_idx = mpc_sram->BLK_MAX;
    uint32_t          blk_cfg = mpc_sram->BLK_CFG & 0xF;
    volatile uint32_t blk_size_bytes = 1 << (blk_cfg + 5);

    if ((se_size_bytes % blk_size_bytes) != 0)
        halt("[CRITICAL] Secure Memory Size is not aligned to the block size!");
    if ((ns_size_bytes % blk_size_bytes) != 0)
        halt("[CRITICAL] Non-Secure Memory Size is not aligned to the block size!");

    uint32_t max_blks = (se_size_bytes + ns_size_bytes) / blk_size_bytes;
    uint32_t sec_blks = se_size_bytes / blk_size_bytes;

    uint32_t max_luts = (max_blks + 31U) / 32U;
    uint32_t sec_luts = sec_blks / 32U;
    uint32_t sec_rem = sec_blks % 32U;

    if (max_luts > (max_idx + 1U))
        halt("[CRITICAL] MPC SRAM Block LUTs exceed the maximum index!");

    for (uint32_t idx = 0; idx < max_luts; idx++) {
        mpc_sram->BLK_IDX = idx;
        __asm volatile("dsb; isb");

        if (idx < sec_luts) {
            mpc_sram->BLK_LUT = 0; // set all 32 blocks secure
        } else if (idx == sec_luts && sec_rem > 0) {
            mpc_sram->BLK_LUT = ~((1U << sec_rem) - 1U);
        } else {
            mpc_sram->BLK_LUT = 0xFFFFFFFFU; // set all 32 blocks non-secure
        }
        __asm volatile("dsb; isb");
    }

    mpc_sram->INT_CLEAR |= BIT(0);
    mpc_sram->CTRL |= BIT(8); // Re-enable auto-increment
    __asm volatile("dsb; isb");
}

void init_mpcs(void)
{
    __asm volatile("" ::: "memory");
    init_mpc(MPC_FLASH, (uint32_t)FLASH_SE_MEM_SIZE_BYTES, (uint32_t)FLASH_NS_MEM_SIZE_BYTES);
    init_mpc(MPC_SRAM1, (uint32_t)SRAM1_SE_MEM_SIZE_BYTES, (uint32_t)SRAM1_NS_MEM_SIZE_BYTES);
    init_mpc(MPC_SRAM2, 0, (uint32_t)SRAM2_NS_MEM_SIZE_BYTES);
    __asm volatile("dsb sy; isb sy" ::: "memory");
}

void init_sau(void)
{
    // Disable SAU for Modification Configurations
    SAU_CTRL = 0; // ENABLE = 0
    __asm volatile("dsb sy; isb sy" ::: "memory");

    // Region 0: the Non-Secure Flash Window
    cfg_sau_region(0, NS_IMAGE_FLASH_ADDR, 0x003FFFF1U);

    // Region 1: the Non-Secure RAM Window
    cfg_sau_region(1, NS_IMAGE_RAM_ADDR, 0x283FFFF1U);

    // Region 2: Non-Secure Peripherals Window
    cfg_sau_region(2, 0x40000000U, 0x4FFFFFF1U);

    // Enable SAU and synchronize memory mappings
    SAU_CTRL = 1;
    __asm volatile("dsb sy; isb sy" ::: "memory");
}

void init_ns_peripherals(void)
{
    // Configure MPS2 Hardware PPC (Hardware-level Security Bypass)
    // Grant Non-Secure access permissions
    AHB_NSPPC0 = 0xFFFFFFFFU; // System AHB peripherals
    APB_NSPPC0 = 0xFFFFFFFFU; // System APB peripherals (including base Timers)
    APB_NSPPC1 = 0xFFFFFFFFU; // Additional system APB elements

    // Expansion PPC configuration
    APB_NSPPCEXP0 = 0xFFFFFFFFU; // SSRAM Memory Protection Controllers
    APB_NSPPCEXP1 = 0xFFFFFFFFU; // Delegate UART0 through UART4, SPI, and I2C to NS space
    APB_NSPPCEXP2 = 0xFFFFFFFFU; // Additional Expansion peripherals
    APB_NSPPCEXP3 = 0xFFFFFFFFU; // Additional Expansion peripherals

    APB_PRIV0 = 0x00000000;
    APB_PRIVEXP0 = 0x00000000;
    APB_PRIVEXP1 = 0x00000000;
    APB_PRIVEXP2 = 0x00000000;
    APB_PRIVEXP3 = 0x00000000;
    __asm volatile("dsb sy; isb sy" ::: "memory");
    APB_PPCEXPINTCLR0 = 0xFFFFFFFFU;
    APB_PPCEXPINTCLR1 = 0xFFFFFFFFU;
    APB_PPCEXPINTCLR2 = 0xFFFFFFFFU;
    APB_PPCEXPINTCLR3 = 0xFFFFFFFFU;
    __asm volatile("dsb sy; isb sy" ::: "memory");
    volatile uint32_t cfg_state = APB_NSPPCEXP1;
    __asm volatile("dsb sy; isb sy" ::: "memory");

    // Route Peripheral Interrupts (IRQs) to Non-Secure Space
    // Bank 0 (IRQs 0-31): Routes
    // Bit 0: Watchdog (WWDG)
    // Bit 3: CMSDK Timer 0
    // Bit 4: CMSDK Timer 1
    volatile uint32_t *const nvic_itns = NVIC_ITNS;
    const uint32_t           itns_bank0 = BIT(0) | BIT(3) | BIT(4);
    nvic_itns[0] = itns_bank0;
    // Bank 1 (IRQs 32-63): Routes Bit 0: UART0 RX (IRQ 32) and Bit 1: UART0 TX (IRQ 33)
    const uint32_t itns_bank1 = BIT(0) | BIT(1);
    nvic_itns[1] = itns_bank1;
    __asm volatile("dsb; isb");

    // Application Interrupt and Reset Control Register (AIRCR)
    // Route critical exceptions (SysTick, PendSV, SVC) to Non-Secure
    const uint32_t aircr_cfg = 0x05FA0000U | BFHFNMINS | PRIS;
    SCB_AIRCR = aircr_cfg;
    __asm volatile("dsb; isb");

    // Enable system exceptions in NS space
    // Bit 16: MemManageFault
    // Bit 17: BusFault
    // Bit 18: UsageFault
    const uint32_t shcsr_cfg = BIT(16) | BIT(17) | BIT(18);
    SCB_SHCSR_NS = shcsr_cfg;
    __asm volatile("dsb; isb");
#if 0
    SCB_DAUTHCTRL_NS = 0x0000000F; // Bit 0-3: INTSPNIDEN|SPNIDENSEL|INTSPIDEN|SPIDENSEL
    __asm volatile("dsb; isb");
    SCB_DEMCR |= BIT(24); // BIT(24) -> TRCENA (DWT, PMU, and ITM features enabled)
    __asm volatile("dsb; isb");
#endif
    // Grant Secure code access permissions to CP10 and CP11 (FPU)
    CPACR_SE |= (0xF << 20);
    __asm volatile("dsb; isb");

    FPCCR_SE |= BIT(30); // Enable Secure LSPEN (Lazy Stacking)

    // Allow Non-Secure code to access Coprocessors 10 & 11 (The FPU)
    cfg_state = NSACR;
    if (cfg_state != SECURITY_EXTENSION_UNSUPPORTED) {
        const uint32_t nsacr_cfg = BIT(10) | BIT(11);
        NSACR = nsacr_cfg;
        __asm volatile("dsb; isb");
    }

    // Set Non-Secure code privilege execution level for CP10 and CP11 (FPU) usage
    // Allow read/write access to both CP10 and CP11 for both unprivileged (user application)
    // and privileged (OS/kernel) tasks
    CPACR_NS |= (0xF << 20); // Allow NS Unprivileged/Privileged FPU access
    __asm volatile("dsb; isb");

    // Grant Non-Secure code permission to preserve/allocate its own FPU context
    // by allowing NS lazy context stacking
    const uint32_t fpccr_cfg = BIT(30) | BIT(29); // LSPEN=1, LSPENS=1
    FPCCR_NS = fpccr_cfg;
    __asm volatile("dsb; isb");

    cfg_state = MPU_CTRL_NS;
    if (cfg_state) {
        MPU_CTRL_NS = 0; // Clear NS MPU state so FreeRTOS MPU can configure it
        __asm volatile("dsb; isb");
    }
}

void secure_reset_handler(void)
{
    SCB_VTOR_SE = (uint32_t)secure_vector_table; // Set Secure Vector Table
    __asm volatile("dsb sy; isb sy" ::: "memory");

    // Fetch the NS vector table before we split NS and SE spaces
    volatile uint32_t *ns_vector_table = (volatile uint32_t *)NS_IMAGE_FLASH_ALIAS;

    store.ns_sp = ns_vector_table[0];
    store.ns_pc = ns_vector_table[1];

    // Prepare Handoff to NS
    SCB_VTOR_NS = NS_IMAGE_FLASH_ADDR;
    __asm volatile("dsb; isb");

    // Split NS and SE spaces
    init_mpcs();
    init_sau();
    init_ns_peripherals();

    uint32_t ns_sp = store.ns_sp;
    uint32_t ns_pc = store.ns_pc;
    ns_pc &= ~1U; // clear thumb bit for transition to NS space with bxns inst

    // Transition to NS space
    __asm volatile(
        "msr msp_ns, %[sp]\n\t"
        "bxns %[pc]\n\t"
        :
        : [sp] "r"(ns_sp), [pc] "r"(ns_pc)
    );
}

void nmi_se_handler(void)
{
    msg = "[CRITICAL] NMI Detected!";
    while (true) {
        HALT_CPU();
    }
}

void hard_fault_se_handler(void)
{
    msg = "[CRITICAL] Hard Fault Detected!";
    while (true) {
        HALT_CPU();
    }
}

void mem_manage_se_handler(void)
{
    msg = "[CRITICAL] Memory Management Fault Detected!";
    while (true) {
        HALT_CPU();
    }
}

void bus_fault_se_handler(void)
{
    msg = "[CRITICAL] Bus Fault Detected!";
    while (true) {
        HALT_CPU();
    }
}

void usage_fault_se_handler(void)
{
    msg = "[CRITICAL] Usage Fault Detected!";
    while (true) {
        HALT_CPU();
    }
}

void secure_fault_se_handler(void)
{
    msg = "[CRITICAL] Secure Fault Detected!";
    while (true) {
        HALT_CPU();
    }
}
