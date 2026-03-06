# EmbedTemplate

A modular C/C++ SDK and project template for SoC FW development and HW testing. Featuring a FreeRTOS-based simulation example, test framework, CLI, and automated SoC register header generation.

## Project Architecture

- **`common/`**: Platform-independent core logic, logging, and CLI system.
- **`hw/rdl/`**: Place your SystemRDL files here.
- **`src/drivers/`**: Hardware abstraction layer. Register C headers are auto-generated from RDL files.
- **`port/`**: Porting layer for OS or hardware platforms.
- **`third_party/`**:
    - `os/freertos`: FreeRTOS-Kernel (Submodule).
    - `bsp/tf-a`: ARM Trusted Firmware-A (Submodule).
    - `lib/embedded-cli`: Reworked Interactive CLI (Subtree).
        [funbiscuit's embedded-cli](https://github.com/funbiscuit/embedded-cli)
- **`tools/`**:
    - `peakrdl`: Customized PeakRDL-cheader generator (Submodule/Fork).

## Quick Start

### 1. Prerequisites

 - **Build Tools**: `cmake`, `gcc` (C23)

#### 1.1. Optional for C Header Generator

 - **Python**: `python3`
 - **PeakRDL Tool**: `peakrdl`

 ~~~bash
    python3 -m pip install peakrdl
 ~~~

 - **PeakRDL C header generator package**: `peakrdl-cheader`

 ~~~bash
    python3 -m pip install peakrdl-cheader
 ~~~

### 2. Initialization

If you just cloned this repository, initialize the submodules:

 ~~~bash
    git submodule update --init --recursive
 ~~~

### 3. Build

 ~~~bash
    # Build with Ninja
    cmake -G Ninja -S . -B build
    cmake --build build

    # Build without tests and RTOS
    cmake -B build -DRTOS=OFF -DTEST=OFF && cmake --build build
 ~~~

### 4. Run

 ~~~bash
    ./build/port/runner
 ~~~

## Modular SDK Framework

### Bare-metal C Lib
- **TF-A C lib Port**: Ported a limited set of bare-metal optimized TF-A standard C library APIs for use across all components, including the FreeRTOS kernel.

### Interactive CLI
- **Reworked Embedded-CLI**: A customized, low-overhead interface for live system interaction, featuring an optimized O(1) hashed autocomplete lookup instead of the original linear `strcmp` loop. The new design extends the autocomplete feature to command arguments.
- **Dynamic Control**: Start/Stop simulations, and trigger tests directly from the console.
- **Static Allocation**: Zero-heap design ensuring reliability for bare-metal or RTOS environments.

### Diagnostic Logger
- **In-Memory & Serial Logging**: Structured logging for real-time firmware diagnostics and post-mortem analysis. The logger implementation is lock-free, multi-writer, and interrupt-safe.
- **Portability**: Shared infrastructure used across both drivers and system-level code.

### Device Drivers
- **ASIC-Driven**: Register headers could be auto-synced from `hw/rdl/` via PeakRDL.

### SoC Test Framework (Testhub)
- **Modular**: Decoupled test registries (groups) for **Device Tests** (Drivers) and **System Tests** (Logic). The framework allows for seamless code sharing and development of tests per IP block.

## Diagnostic Logging Matrix
The SDK uses a structured logging system organized into **Domains**, **Entities**, and **Levels**. This allows for granular control over system-wide visibility.

- **Domains**: `DEV` (Hardware/Drivers), `SYS` (Core Logic), `TEST` (Framework).
- **Entities**: `SIM`, `CLI`, `LOG` (System) and `GPIO`, `SYSCTRL`, `TIMER`, `UART` (Hardware/Drivers).
- **Levels**: `NONE`, `CRITICAL`, `ERROR`, `WARN`, `INFO`, `DEBUG`.

### Customization & Extensibility
The provided `DEV` entities (GPIO, UART, etc.) are included as a **functional demonstration** (GPIO) and **placeholders**. 
- **Modular Design**: Users should expand these entities to match their specific SoC hardware.
- **Runtime Control**: Query the current matrix via the CLI:
  - `show domains` / `show entities` / `show levels`
  - `show config log` (View current active settings)
  - `set log <domain> <entity> <level>` (Change verbosity on-the-fly)

### FreeRTOS-based Thermal Simulation Example
The project features a FreeRTOS-based thermal regulation simulation (Heater/Fan/Alarm) controlled via GPIO, which starts in a suspended state and can be interactively managed through the system CLI. The simulation's FreeRTOS is Zero-heap (static).
1. **Start**: Enter `system` menu and type `start`.
2. **Monitor**: Watch `[SYS:SIM]` and `[DEV:GPIO]` logs for temperature transitions and fan control.
3. **Filter**: Use `set log sys sim none` to silence simulation logs while running a driver test.
