#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import sys
import time
import subprocess
import socket
import select
import termios
import tty

def resolve_target_paths(target_chip, image_name):
    """Locate target firmware artifacts."""
    project_root = Path(__file__).resolve().parent

    search_anchors = [
        project_root,
        project_root.parent,
        Path.home()
    ]

    relative_subpath = Path("build") / target_chip / "port" / image_name
    base_dir = next((a / relative_subpath.parent for a in search_anchors if (a / relative_subpath.parent).exists()), project_root / relative_subpath.parent)

    exe_path = base_dir / image_name
    elf_path = base_dir / f"{image_name}.elf"
    bin_path = base_dir / f"{image_name}.bin"

    return project_root, elf_path, bin_path, exe_path

from pathlib import Path

def pci_build_status(target_chip):
    """Parse CMakeCache.txt to detect if ENABLE_PCI is set."""
    cache_file = Path(f"build/{target_chip}/CMakeCache.txt")
    if not cache_file.exists():
        return False

    with open(cache_file, "r") as f:
        for line in f:
            # Look for lines ENABLE_PCI:BOOL=ON or ENABLE_PCI:UNINITIALIZED=True
            if "ENABLE_PCI" in line and any(val in line.upper() for val in ["=ON", "=TRUE", "=1"]):
                return True
    return False

def compile_firmware(target_chip, opt_args, verbose=False):
    """CMake generation and compilation using default-override logic."""
    print(f"\033[94m[Agent] Starting compilation phase for target_chip: {target_chip}...\033[0m")

    is_first_build = not os.path.exists(f"build/{target_chip}")

    if is_first_build:
        cmake_gen = [
            "cmake", "-G", "Ninja", "-S", ".", "-B", f"build/{target_chip}",
        ]
    else:
        cmake_gen = [
            "cmake", "-B", f"build/{target_chip}",
        ]

    if target_chip == "cortex-a9-virt":
        if is_first_build:
            cmake_gen.extend([
                "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake",
                "-DTARGET_CHIP=cortex-a9-virt"
            ])
    elif target_chip == "gd32vf103":
        if is_first_build:
            cmake_gen.extend([
                "-DCMAKE_TOOLCHAIN_FILE=cmake/riscv-none-elf.cmake",
                "-DTARGET_CHIP=gd32vf103"
            ])
    elif target_chip == "stm32f4":
        if is_first_build:
            cmake_gen.extend([
                "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake",
                "-DTARGET_CHIP=stm32f4"
            ])
    elif target_chip == "x86-virt":
        if is_first_build:
            cmake_gen.extend([
                "-DCMAKE_TOOLCHAIN_FILE=cmake/x86_none_elf.cmake",
                "-DTARGET_CHIP=x86-virt"
            ])
    elif target_chip == "x86_64":
        if is_first_build:
            cmake_gen.extend("")

    if opt_args:
        cmake_gen.extend(opt_args.split())

    cmake_build = ["cmake", "--build", f"build/{target_chip}"]
    if verbose:
        cmake_build.append("-v")

    try:
        print(f"[Agent] Configuring: {' '.join(cmake_gen)}")
        subprocess.run(cmake_gen, check=True)
        print(f"[Agent] Building: {' '.join(cmake_build)}")
        subprocess.run(cmake_build, check=True)
        print("\033[92m[Agent] Compilation completed successfully.\033[0m")
    except subprocess.CalledProcessError as e:
        print(f"\033[91m[Agent] ERROR: Compilation failed: {e}\033[0m")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Simulation Framework Agent")
    parser.add_argument(
        "--build",
        action="store_true",
        help="Build target chip firmware before launch. Select --chip to build its firmware."
    )
    parser.add_argument(
        "--opt",
        help='Build options for target chip firmware. Example: --opt="-DENABLE_PCI=False"'
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Enable verbose build output"
    )
    parser.add_argument(
        "--chip", choices=["stm32f4", "gd32vf103", "cortex-a9-virt", "x86-virt", "x86_64"], default="stm32f4",
        help="Specify the target chip (default: stm32f4)"
    )
    parser.add_argument(
        "--debug", action="store_true",
        help="Enable GDB server on port 1234 and stop guest CPU at startup"
    )
    args = parser.parse_args()

    if args.build:
        compile_firmware(args.chip, args.opt, args.verbose)

    image_name = "runner"
    project_root, image_elf, image_bin, image_exe = resolve_target_paths(args.chip, image_name)
    if args.chip == "x86_64":
        print(f"\033[92m[Agent] '{args.chip}' is a Host target. No QEMU required. Exiting.\033[0m")
        print(f"\033[95m[Agent] You can directly run '{image_exe}'.\033[0m")
        sys.exit(0)

    arch = None
    if args.chip == "gd32vf103":
        arch = "riscv"
    elif args.chip in ["stm32f4", "cortex-a9-virt"]:
        arch = "arm"
    elif args.chip == "x86-virt":
        arch = "i386:x86-64"

    print(f"[Agent] Target Chip: {args.chip}")
    print("\033[94m[Agent] Resolving workspace infrastructure layout...\033[0m")

    target_image = image_bin if args.chip == "gd32vf103" else image_elf
    print(f"\033[94m[Agent] Target firmware located at:\033[0m {target_image}")

    if not os.path.exists(target_image):
        print(f"\033[91m[Agent] ERROR: Firmware file not found: {target_image} Run compilation step first.\033[0m")
        sys.exit(1)

    if args.chip == "stm32f4":
        print("[Agent] Structuring QEMU layout for ARM STM32F4...")
        qemu_bin = "qemu-system-arm"
        qemu_args = [
            "-M", "netduinoplus2",
            "-nographic",
            "-serial", f"unix:/tmp/uart.sock,server=on,wait=on",
            "-kernel", str(image_elf),
        ]
    elif args.chip == "cortex-a9-virt":
        print("[Agent] Structuring QEMU layout for ARM Cortex-A9 Virtual...")
        qemu_bin = "qemu-system-arm"
        qemu_args = [
            "-M", "virt",
            "-cpu", "cortex-a15",
            "-m", "128M",
            "-display", "none",
            "-serial", f"unix:/tmp/uart.sock,server=on,wait=on",
            "-device", f"loader,file={image_elf},cpu-num=0",
        ]
    elif args.chip == "gd32vf103":
        print("[Agent] Structuring QEMU layout for RISC-V GD32VF103...")
        qemu_bin = "qemu-system-riscv32"
        qemu_args = [
            "-M", "none",
            "-cpu", "rv32,im=true,a=true,c=true,Zicsr=true",
            "-nographic",
            "-serial", f"unix:/tmp/uart.sock,server=on,wait=on",
            "-device", f"loader,addr=0x08000000,size=0x20000,type=ram", # 128KB execution space
            "-device", f"loader,addr=0x20000000,size=0x8000,type=ram",  # 32KB data space
            "-kernel", str(image_elf),
        ]
    elif args.chip == "x86-virt":
        qemu_bin = "qemu-system-x86_64"
        qemu_args = [
            "-M", "q35",
            "-cpu", "pentium3",
            "-m", "128M",
            "-display", "none",
            "-serial", f"unix:/tmp/uart.sock,server=on,wait=on",
            "-kernel", str(image_elf),
        ]
        pci_enabled = pci_build_status(args.chip)
        if pci_enabled:
            vfio_user_sock = "/tmp/vfio-pcie.sock"
            if not os.path.exists(vfio_user_sock):
                print(f"\033[91m[Agent] FATAL: PCI-Bridge UDS '{vfio_user_sock}' not found. Please start the PCI-Bridge first.\033[0m")
                sys.exit(1)
            qemu_args.extend(["-device", "pcie-root-port,id=pcie.1"])
            json_config = '{"driver": "vfio-user-pci", "socket": {"type": "unix", "path": "/tmp/vfio-pcie.sock"}, "bus": "pcie.1", "id": "pcie_cosim"}'
            qemu_args.extend(["-device", json_config])
    else:
        print(f"\033[91m[Agent] FATAL: Unsupported architecture/CPU pairing specified\033[0m")
        sys.exit(1)

    if args.debug:
        qemu_args.extend(["-s", "-S"])

    qemu_proc = None
    socat_proc = None
    gdb_proc = None

    try:
        print(f"[Agent] QEMU initialization: {qemu_bin} {' '.join(qemu_args)}")
        print(f"[Agent] Launching Guest OS Emulation...")

        current_env = os.environ.copy()
        qemu_proc = subprocess.Popen(
            [qemu_bin] + qemu_args,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=current_env
        )

        print(f"[Agent] Waiting for QEMU to start...")
        start_time = time.time()
        qemu_ready = False

        while time.time() - start_time < 3.0:
            if qemu_proc.poll() is not None:
                print("\033[91m[Agent] ERROR: QEMU exited unexpectedly.\033[0m")
                sys.exit(1)
            try:
                qemu_ready = True
                time.sleep(0.5)
                break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.05)

        if not qemu_ready:
            print("\033[91m[Agent] ERROR: Timeout connecting to QEMU.\033[0m")
            sys.exit(1)

        print("\033[92m[Agent] Spawning Terminal for Serial (socat) Console...\033[0m")

        socat_cmd = [
            "xterm", "-hold", "-title", f"UART Console {args.chip}",
            "-e", "socat", f"-,rawer", f"unix-connect:/tmp/uart.sock"
        ]
        socat_proc = subprocess.Popen(socat_cmd, env=current_env)
        time.sleep(0.5) # Let socat connect and unblock QEMU

        if args.debug:
            print("\033[92m[Agent] Spawning Terminal for Debugging (gdb)...\033[0m")

            gdb_commands = (
                f"set architecture {arch}\n"
                f"target remote localhost:1234\n"
            )
            gdb_script_path = os.path.join(project_root, "build", ".gdb_init_agent")
            with open(gdb_script_path, "w") as f:
                f.write(gdb_commands)

            gdb_cmd = [
                "xterm", "-hold", "-title", f"GDB Target {arch}/{args.chip}",
                "-e", "gdb-multiarch", image_elf, "-x", gdb_script_path
            ]
            gdb_proc = subprocess.Popen(gdb_cmd, env=current_env)
            print("\033[95m[Agent] Debug mode active. GDB stub listening on port 1234. CPU frozen at entry point.\033[0m")

        print("\033[94m[Agent] Press 'Ctrl + ]' to exit.\033[0m")
        old_settings = termios.tcgetattr(sys.stdin.fileno())
        last_char = ""

        try:
            tty.setraw(sys.stdin.fileno())
            while True:
                r, w, x = select.select([sys.stdin], [], [], 0.05)

                if qemu_proc.poll() is not None:
                    break

                if sys.stdin in r:
                    char = sys.stdin.read(1)
                    if not char:
                        break
                    if char == "\x1d":  # 'Ctrl + ]' Escape Routing
                        break
                    if last_char == "q" and char in ["\n", "\r"]:
                        time.sleep(0.05)
                        break
                    last_char = char
        finally:
            termios.tcsetattr(
                sys.stdin.fileno(), termios.TCSANOW, old_settings
            )

    except KeyboardInterrupt:
        pass
    finally:
        os.system("stty sane")
        print("\r\033[K", end="")
        print(f"\r\033[96m[Agent] Tearing down infrastructure...\033[0m")
        if os.path.exists("/tmp/uart.sock"):
            try:
                os.remove("/tmp/uart.sock")
            except Exception:
                pass

        for proc, name in [(qemu_proc, "QEMU"), (socat_proc, "Socat"), (gdb_proc, "GDB")]:
            if proc:
                try:
                    if proc.poll() is None:
                        proc.terminate()
                        proc.wait(timeout=1.0)
                        print(f"[Agent] Terminated {name}.")
                except Exception:
                    try:
                        proc.kill()
                    except Exception:
                        pass

        gdb_script_path = os.path.join(os.getcwd(), "build", ".gdb_init_agent")
        if os.path.exists(gdb_script_path):
            os.remove(gdb_script_path)
    print("[Agent] Teardown complete.")

if __name__ == "__main__":
    main()
