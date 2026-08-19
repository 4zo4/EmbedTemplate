#!/usr/bin/env python3
#
# Copyright (c) 2026, Purple
# This file is licensed under the MIT License.
#
import argparse
import glob
import json
import os
from pathlib import Path
import sys
import time
import subprocess
import socket
import select
import termios
import tty
from tools.net.vfio_user_pkt_sniffer import (
    launch_wireshark,
    setup_log_directory,
    resolve_absolute_paths,
    start_vfio_user_pkt_sniffer,
    stop_vfio_user_pkt_sniffer,
)

def load_json(path):
    if not os.path.exists(path):
        return None

    with open(path, "r") as f:
        try:
            return json.load(f)
        except json.JSONDecodeError as e:
            print(f"[⚠️] Error: Failed to parse '{path}'")
            print(f"[⚠️] {path}:{e.lineno}:{e.colno}: {e.msg}")
            sys.exit(1)

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

    build_dir = Path("build") / target_chip
    is_first_build = not build_dir.exists()

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
    elif target_chip in ["gd32vf103", "gd32vf103-virt"]:
        if is_first_build:
            cmake_gen.extend([
                "-DCMAKE_TOOLCHAIN_FILE=cmake/riscv-none-elf.cmake",
                f"-DTARGET_CHIP={target_chip}"
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
        print(f"\033[91m[Agent] Error: Compilation failed: {e}\033[0m")
        sys.exit(1)

def start_pcie_cosim_bridge():
    """Start PCIe Co-Simulation Bridge."""
    project_root = Path(__file__).resolve().parent
    parent_dir = project_root.parent

    file = str(parent_dir / "*" / "src" / "bridge" / "pcie_cosim_bridge.cpp")
    files = glob.glob(file)

    if not files:
        print("[Agent] Error: PCI-Bridge not found")
        return None

    if len(files) == 1:
        pcie_cosim_source_path = Path(files[0])
    else:
        print("[Agent] Warning: found multiple PCI-Bridges")
        return None

    pcie_cosim_root = pcie_cosim_source_path.parents[2]
    bridge_bin = pcie_cosim_root / "build" / "pcie_sim"
    if not bridge_bin.exists():
        print(f"[Agent] PCI-Bridge image not found. Compiling {bridge_bin}...")
        try:
            subprocess.run(["make"], cwd=pcie_cosim_root, check=True)
        except subprocess.CalledProcessError as e:
            print(f"[🗙 Error] Compilation failed for {bridge_bin}: {e}")
            return None

    print("\033[92m[Agent] Spawning Terminal for PCI-Bridge...\033[0m")

    current_env = os.environ.copy()
    bridge_cmd = [
        "xterm", "-hold", "-title", f"PCIe Co-Simulation Bridge",
        "-e", str(bridge_bin), f"-Rvr"
    ]
    bridge_proc = subprocess.Popen(bridge_cmd, env=current_env)
    time.sleep(1.0) # Give bridge time to start

    return bridge_proc

def main():
    agent_id = os.getpid()
    config_state = load_json(".configs.json")
    config_meta = None

    if config_state:
        config_meta = config_state.get("meta", {})
        config_repos = config_meta.get("repos", []) + config_meta.get("selected_repos", [])
        config_scopes = config_meta.get("selected_scopes", [])
    else:
        print("\033[91m[Agent] Fatal: Workspace not initialized. Please run 'workspace_setup.py' to setup workspace.\033[0m")
        sys.exit(1)

    enable_arm = False
    enable_net = False
    enable_dbg = False
    enable_riscv = False

    if not "qemu" in config_repos:
        print("\033[91m[Agent] Fatal: QEMU not installed.\033[0m")
        sys.exit(1)

    arm_chips = []
    riscv_chips = []

    if "riscv_toolchain" in config_scopes:
        riscv_chips = ["gd32vf103", "gd32vf103-virt"]
        enable_riscv = True
    if "arm_toolchain" in config_scopes:
        arm_chips = ["stm32f4", "cortex-a9-virt"]
        enable_arm = True
    if "networking" in config_scopes:
        enable_net = True
    if "debug" in config_scopes:
        enable_dbg = True

    parser = argparse.ArgumentParser(description="Simulation Framework Agent")
    parser.add_argument(
        "--build",
        action="store_true",
        help="Build target chip firmware before launch. Select --chip to build its firmware."
    )
    parser.add_argument(
        "--opt",
        help='Build options for target chip firmware. Example: --opt="-DENABLE_PCI=True"'
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Enable verbose build output"
    )
    chips = arm_chips + riscv_chips + ["x86-virt", "x86_64"]
    parser.add_argument(
        "--chip", choices=chips,
        help="Specify the target chip"
    )
    if enable_dbg:
        parser.add_argument(
            "--debug", action="store_true",
            help="Enable GDB server and stop guest CPU at startup"
        )
    if enable_net:
        parser.add_argument(
            "--sniffer", action="store_true",
            help="Launch packet sniffer to capture QEMU vfio-user traffic"
        )
    args = parser.parse_args()

    if args.chip == None:
        print("\033[91m[Agent] Error: Specify the target chip.\033[0m")
        sys.exit(1)

    if args.build:
        compile_firmware(args.chip, args.opt, args.verbose)

    image_name = "runner"
    project_root, image_elf, image_bin, image_exe = resolve_target_paths(args.chip, image_name)
    if args.chip == "x86_64":
        print(f"\033[92m[Agent] '{args.chip}' is a Host target. No QEMU required. Exiting.\033[0m")
        print(f"\033[95m[Agent] You can directly run '{image_exe}'.\033[0m")
        sys.exit(0)
    elif args.chip == "gd32vf103":
        print(f"\033[92m[Agent] '{args.chip}' is a Renode RISC-V GD32VF103 target. Exiting.\033[0m")
        print(f"\033[95m[Agent] You can run '{image_elf} in Renode.'\033[0m")
        sys.exit(0)

    arch = None
    if args.chip in arm_chips:
        arch = "arm"
    elif args.chip in riscv_chips:
        arch = "riscv"
    elif args.chip == "x86-virt":
        arch = "i386:x86-64"

    print(f"[Agent] Target Chip: {args.chip}")
    print("\033[94m[Agent] Resolving workspace infrastructure layout...\033[0m")

    target_image = image_elf
    print(f"\033[94m[Agent] Target firmware located at:\033[0m {target_image}")

    if not target_image.exists():
        print(f"\033[91m[Agent] Error: Firmware file not found: {target_image} Run compilation step first.\033[0m")
        sys.exit(1)

    bridge_proc = None
    sniffer_proc = None
    gdb_sock = Path(f"/tmp/gdb_{agent_id}.sock")
    uart_sock = Path(f"/tmp/uart_{agent_id}.sock")

    if args.chip == "stm32f4":
        print("[Agent] Structuring QEMU layout for ARM STM32F4...")
        qemu_bin = "qemu-system-arm"
        qemu_args = [
            "-M", "netduinoplus2",
            "-nographic",
            "-serial", f"unix:{uart_sock},server=on,wait=on",
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
            "-serial", f"unix:{uart_sock},server=on,wait=on",
            "-device", f"loader,file={image_elf},cpu-num=0",
        ]
    elif args.chip == "gd32vf103-virt":
        print("[Agent] Structuring QEMU layout for RISC-V GD32VF103 Virtual...")
        qemu_bin = "qemu-system-riscv32"
        qemu_args = [
            "-M", "virt",
            "-cpu", "rv32",
            "-m", "32M",
            "-display", "none",
            "-bios", "none",
            "-serial", f"unix:{uart_sock},server=on,wait=on",
            "-device", f"loader,file={image_elf},cpu-num=0"
        ]
    elif args.chip == "x86-virt":
        qemu_bin = "qemu-system-x86_64"
        qemu_args = [
            "-M", "q35",
            "-cpu", "pentium3",
            "-m", "128M",
            "-display", "none",
            "-serial", f"unix:{uart_sock},server=on,wait=on",
            "-kernel", str(image_elf),
        ]
        pci_enabled = pci_build_status(args.chip)
        if pci_enabled:
            vfio_user_sock = Path("/tmp/vfio-pcie.sock")
            if vfio_user_sock.exists():
                print("\033[33m[Agent] Warning: PCI-Bridge already run.\033[0m", flush=True)
            else:
                bridge_proc = start_pcie_cosim_bridge()
                if bridge_proc:
                    if not vfio_user_sock.exists():
                        print(f"\033[91m[Agent] Fatal: PCI-Bridge failed to initialize. UDS '{vfio_user_sock}' not found.\033[0m")
                        sys.exit(1)
                    if args.sniffer:
                        project_root_abs, log_dir, pcap_file_path, slog_file_path = resolve_absolute_paths()
                        setup_log_directory(log_dir, pcap_file_path, slog_file_path, max_backups=2)
                        sniffer_proc = start_vfio_user_pkt_sniffer(
                            project_root=project_root_abs,
                            pcap_file_path=pcap_file_path,
                            slog_file_path=slog_file_path,
                            socket_path=vfio_user_sock
                        )
                        if sniffer_proc is None:
                            print("\033[33m[Agent] Warning: Packet sniffer failed to arm.\033[0m", flush=True)
                        else:
                            launch_wireshark(log_dir, sniffer_proc)
                    qemu_args.extend(["-device", "pcie-root-port,id=pcie.1"])
                    json_config = '{"driver": "vfio-user-pci", "socket": {"type": "unix", "path": "/tmp/vfio-pcie.sock"}, "bus": "pcie.1", "id": "pcie_cosim"}'
                    qemu_args.extend(["-device", json_config])
    else:
        print(f"\033[91m[Agent] Fatal: Unsupported architecture/CPU pairing specified\033[0m")
        sys.exit(1)

    if enable_dbg and args.debug:
        qemu_args.extend(["-gdb", f"unix:{gdb_sock},server=on,wait=off", "-S"])

    gdb_proc = None
    qemu_proc = None
    socat_proc = None

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
                print("\033[91m[Agent] Error: QEMU exited unexpectedly.\033[0m")
                sys.exit(1)
            try:
                qemu_ready = True
                time.sleep(0.5)
                break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.05)

        if not qemu_ready:
            print("\033[91m[Agent] Error: Timeout connecting to QEMU.\033[0m")
            sys.exit(1)

        print("\033[92m[Agent] Spawning Terminal for Serial (socat) Console...\033[0m")

        socat_cmd = [
            "xterm", "-hold", "-title", f"Agent {agent_id} UART Console {args.chip}",
            "-e", "socat", f"-,rawer", f"unix-connect:{uart_sock}"
        ]
        socat_proc = subprocess.Popen(socat_cmd, env=current_env)
        time.sleep(0.5) # Let socat connect and unblock QEMU

        if enable_dbg and args.debug:
            print("\033[92m[Agent] Spawning Terminal for Debugging (gdb)...\033[0m")

            gdb_commands = (
                f"set architecture {arch}\n"
                f"target remote {gdb_sock}\n"
            )
            gdb_script_path = Path(project_root) / "build" / f".gdb_init_agent_{agent_id}"
            gdb_script_path.write_text(gdb_commands)

            gdb_cmd = [
                "xterm", "-hold", "-title", f"Agent {agent_id} GDB Target {arch}/{args.chip}",
                "-e", "gdb-multiarch", image_elf, "-x", str(gdb_script_path)
            ]
            gdb_proc = subprocess.Popen(gdb_cmd, env=current_env)
            print(f"\033[95m[Agent] Debug mode active. GDB stub listening on UDS '{gdb_sock}'. CPU frozen at entry point.\033[0m")

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
        print("\r\033[96m[Agent] Tearing down infrastructure...\033[0m")

        try:
            uart_sock.unlink(missing_ok=True)
        except Exception:
            pass

        if sniffer_proc:
            print("[Agent] Tearing down vfio-user packet sniffer")
            stop_vfio_user_pkt_sniffer(sniffer_proc)

        process_list = [
            (qemu_proc, "QEMU"),
            (socat_proc, "Socat"),
            (gdb_proc, "GDB"),
            (bridge_proc, "Bridge")
        ]

        for proc, name in process_list:
            if not proc:
                continue

            if proc.poll() is None:
                try:
                    proc.terminate()
                    proc.wait(timeout=1.0)
                    print(f"[Agent] Terminated {name}.")
                except subprocess.TimeoutExpired:
                    try:
                        proc.kill()
                        proc.wait()
                        print(f"[Agent] Force killed {name} (Timeout expired).")
                    except Exception:
                        pass
                except Exception:
                    try:
                        proc.kill()
                    except Exception:
                        pass

        if bridge_proc:
            bridge_sockets = [
                "/tmp/vfio-pcie.sock",
                "/tmp/pcie-cosim1.sock",
                "/tmp/pcie-cosim2.sock"
            ]

            for sock_path in bridge_sockets:
                sock = Path(sock_path)
                try:
                    if sock.exists() or sock.is_socket():
                        sock.unlink(missing_ok=True)
                except Exception:
                    pass

        if gdb_proc:
            try:
                gdb_sock.unlink(missing_ok=True)
            except Exception:
                pass
            gdb_script = Path.cwd() / "build" / f".gdb_init_agent_{agent_id}"
            try:
                gdb_script.unlink(missing_ok=True)
            except Exception:
                pass


        print("[Agent] Teardown complete.")


if __name__ == "__main__":
    main()
