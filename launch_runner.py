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
    print(f"\033[94m[Monitor] Starting compilation for target chip: {target_chip}...\033[0m")

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

    if target_chip in ["stm32f4", "cortex-a9-virt", "cortex-m33"]:
        if is_first_build:
            cmake_gen.extend([
                "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake",
                f"-DTARGET_CHIP={target_chip}"
            ])
    elif target_chip in ["gd32vf103", "gd32vf103-virt"]:
        if is_first_build:
            cmake_gen.extend([
                "-DCMAKE_TOOLCHAIN_FILE=cmake/riscv-none-elf.cmake",
                f"-DTARGET_CHIP={target_chip}"
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
        print(f"[Monitor] Configuring: {' '.join(cmake_gen)}")
        subprocess.run(cmake_gen, check=True)
        print(f"[Monitor] Building: {' '.join(cmake_build)}")
        subprocess.run(cmake_build, check=True)
        print("\033[92m[Monitor] Compilation successfull.\033[0m")
    except subprocess.CalledProcessError as e:
        print(f"\033[91m[Monitor] Error: Compilation failed: {e}\033[0m")
        sys.exit(1)

def start_pcie_cosim_bridge():
    """Start PCIe Co-Simulation Bridge."""
    project_root = Path(__file__).resolve().parent
    parent_dir = project_root.parent

    file = str(parent_dir / "*" / "src" / "bridge" / "pcie_cosim_bridge.cpp")
    files = glob.glob(file)

    if not files:
        print("[Monitor] Error: PCI-Bridge not found")
        return None

    if len(files) == 1:
        pcie_cosim_source_path = Path(files[0])
    else:
        print("[Monitor] Warning: found multiple PCI-Bridges")
        return None

    pcie_cosim_root = pcie_cosim_source_path.parents[2]
    bridge_bin = pcie_cosim_root / "build" / "pcie_sim"
    if not bridge_bin.exists():
        print(f"[Monitor] PCI-Bridge image not found. Compiling {bridge_bin}...")
        try:
            subprocess.run(["make"], cwd=pcie_cosim_root, check=True)
        except subprocess.CalledProcessError as e:
            print(f"[🗙 Error] Compilation failed for {bridge_bin}: {e}")
            return None

    print("\033[92m[Monitor] Spawning Terminal for PCI-Bridge...\033[0m")

    current_env = os.environ.copy()
    bridge_cmd = [
        "xterm", "-hold", "-title", f"PCIe Co-Simulation Bridge",
        "-e", str(bridge_bin), f"-Rvr"
    ]
    bridge_proc = subprocess.Popen(bridge_cmd, env=current_env)
    time.sleep(1.0) # Give bridge time to start

    return bridge_proc

def setup_qemu_base_target(target_chip: str, image_elf: Path, uart_sock: Path) -> tuple[str, list[str]]:
    """Return QEMU binary and arguments based on the target chip."""
    if target_chip == "stm32f4":
        qemu_bin = "qemu-system-arm"
        qemu_args = [
            "-M", "netduinoplus2",
            "-nographic",
            "-serial", f"unix:{uart_sock},server=on,wait=on",
            "-kernel", str(image_elf),
        ]
    elif target_chip == "cortex-a9-virt":
        qemu_bin = "qemu-system-arm"
        qemu_args = [
            "-M", "virt",
            "-cpu", "cortex-a15",
            "-m", "128M",
            "-display", "none",
            "-serial", f"unix:{uart_sock},server=on,wait=on",
            "-device", f"loader,file={image_elf},cpu-num=0",
        ]
    elif target_chip == "cortex-m33":
        qemu_bin = "qemu-system-arm"
        secure_boot_elf = image_elf.parent / "secure_boot.elf"
        qemu_args = [
            "-M", "mps2-an505",
            "-cpu", "cortex-m33",
            "-m", "16M",
            "-display", "none",
            "-serial", f"unix:{uart_sock},server=on,wait=on",
            "-device", f"loader,file={secure_boot_elf},cpu-num=0",
            "-device", f"loader,file={image_elf}",
        ]
    elif target_chip == "gd32vf103-virt":
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
    elif target_chip == "x86-virt":
        qemu_bin = "qemu-system-x86_64"
        qemu_args = [
            "-M", "q35",
            "-cpu", "pentium3",
            "-m", "128M",
            "-display", "none",
            "-serial", f"unix:{uart_sock},server=on,wait=on",
            "-kernel", str(image_elf),
        ]
    else:
        print(f"\033[91m[Monitor] Fatal: Unsupported architecture/CPU pairing specified\033[0m")
        sys.exit(1)
    return qemu_bin, qemu_args

def setup_pci_bridge(qemu_args: list[str], enable_net: bool, run_sniffer: bool) -> tuple[any, any]:
    """Initialize PCIe co-simulation bridge and packet sniffing."""
    vfio_user_sock = Path("/tmp/vfio-pcie.sock")

    if vfio_user_sock.exists():
        print("\033[33m[Monitor] Warning: PCI-Bridge already run.\033[0m", flush=True)
        return None, None

    bridge_proc = start_pcie_cosim_bridge()
    if not bridge_proc:
        return None, None

    if not vfio_user_sock.exists():
        print(f"\033[91m[Monitor] Fatal: PCI-Bridge failed to initialize. UDS '{vfio_user_sock}' not found.\033[0m")
        sys.exit(1)

    if enable_net and run_sniffer:
        project_root_abs, log_dir, pcap_file_path, slog_file_path = resolve_absolute_paths()
        setup_log_directory(log_dir, pcap_file_path, slog_file_path, max_backups=2)

        sniffer_proc = start_vfio_user_pkt_sniffer(
            project_root=project_root_abs,
            pcap_file_path=pcap_file_path,
            slog_file_path=slog_file_path,
            socket_path=vfio_user_sock
        )
        if sniffer_proc is None:
            print("\033[33m[Monitor] Warning: Packet sniffer failed to arm.\033[0m", flush=True)
        else:
            launch_wireshark(log_dir, sniffer_proc)

    qemu_args.extend(["-device", "pcie-root-port,id=pcie.1"])
    json_config = '{"driver": "vfio-user-pci", "socket": {"type": "unix", "path": "/tmp/vfio-pcie.sock"}, "bus": "pcie.1", "id": "pcie_cosim"}'
    qemu_args.extend(["-device", json_config])

    return bridge_proc, sniffer_proc

def setup_qemu_env(args, session_id: str, image_elf: Path, uart_sock: Path, enable_net: bool) -> tuple:
    """Configure QEMU environment."""
    qemu_bin, qemu_args = setup_qemu_base_target(args.chip, image_elf, uart_sock)

    if args.chip == "x86-virt" and pci_build_status(args.chip):
        bridge_proc, sniffer_proc = setup_pci_bridge(
            qemu_args=qemu_args,
            enable_net=enable_net,
            run_sniffer=args.sniffer
        )
    else:
        bridge_proc, sniffer_proc = None, None

    return qemu_bin, qemu_args, bridge_proc, sniffer_proc

def parse_arguments(arm_chips, riscv_chips, enable_dbg, enable_net):
    parser = argparse.ArgumentParser(description="Simulation Framework Monitor")
    parser.add_argument(
        "--build",
        action="store_true",
        help="Build target chip firmware before launch. Select --chip to build its firmware."
    )
    parser.add_argument(
        "--only",
        action="store_true",
        help="Build target chip firmware only and exit. Requires --build flag."
    )
    parser.add_argument(
        "--verbose", action="store_true",
        help="Enable verbose build output. Requires --build flag."
    )
    parser.add_argument(
        "--opt",
        help='Build options for target chip firmware. Requires --build flag. Example: --opt="-DENABLE_SECURE=True" for --chip cortex-m33'
    )
    chips = arm_chips + riscv_chips + ["x86-virt", "x86_64"]
    parser.add_argument(
        "--chip", choices=chips,
        help="Specify the target chip. This argument is mandatory."
    )
    if enable_dbg:
        parser.add_argument(
            "--debug", action="store_true",
            help="Enable GDB server and stop guest CPU at startup"
        )
    if enable_net:
        parser.add_argument(
            "--sniffer", action="store_true",
            help="Launch packet sniffer to capture QEMU vfio-user traffic, requires --chip=x86-virt"
        )
    args = parser.parse_args()
    return args

def fatal_exit(msg: str, help: bool = True, status: int = 1):
    print(f"\033[91m[Monitor] Error: {msg}\033[0m")
    if help:
        print("\033[95m[Monitor] Type './launch_runner.py -h' for usage information\033[0m")
    sys.exit(status)

def main():
    session_id = os.getpid()
    config_state = load_json(".configs.json")

    if config_state:
        config_meta = config_state.get("meta", {})
        config_scopes = config_meta.get("selected_scopes", [])
    else:
        fatal_exit("Workspace not initialized. Please run 'workspace_setup.py' to setup workspace.", help=False)

    enable_dbg = "debug" in config_scopes
    enable_net = "networking" in config_scopes
    enable_arm = "arm_toolchain" in config_scopes
    enable_riscv = "riscv_toolchain" in config_scopes

    arm_chips = ["stm32f4", "cortex-a9-virt", "cortex-m33"] if "arm_toolchain" in config_scopes else []
    riscv_chips = ["gd32vf103", "gd32vf103-virt"] if "riscv_toolchain" in config_scopes else []

    args = parse_arguments(arm_chips, riscv_chips, enable_dbg, enable_net)

    if args.chip == None:
        fatal_exit("Specify the target chip.")

    if args.build:
        compile_firmware(args.chip, args.opt, args.verbose)
        if args.only:
            print("\033[92m[Monitor] Build only complete. Exiting.\033[0m")
            sys.exit(0)

    image_name = "runner"
    project_root, image_elf, image_bin, image_exe = resolve_target_paths(args.chip, image_name)
    if args.chip == "x86_64":
        if not image_exe.exists():
            fatal_exit(f"'{image_exe}' not found: Run compilation first.", help=True)
        print(f"\033[92m[Monitor] '{args.chip}' is a Host target. No QEMU required. Exiting.\033[0m")
        print(f"\033[95m[Monitor] You can directly run '{image_exe}'.\033[0m")
        sys.exit(0)
    elif args.chip == "gd32vf103":
        if not image_elf.exists():
            fatal_exit(f"'{image_elf}' not found: Run compilation first.", help=True)
        print(f"\033[92m[Monitor] '{args.chip}' is a Renode RISC-V GD32VF103 target. Exiting.\033[0m")
        print(f"\033[95m[Monitor] You can run '{image_elf} in Renode.'\033[0m")
        sys.exit(0)

    print(f"[Monitor] Target Chip: {args.chip}")
    print("\033[94m[Monitor] Resolving workspace infrastructure layout...\033[0m")

    target_image = image_elf
    if not target_image.exists():
        fatal_exit(f"Firmware file not found: {target_image} Run compilation first.", help=True)
    print(f"\033[94m[Monitor] Target firmware located at:\033[0m {target_image}")

    gdb_sock = Path(f"/tmp/gdb_{session_id}.sock")
    uart_sock = Path(f"/tmp/uart_{session_id}.sock")
    qemu_bin, qemu_args, bridge_proc, sniffer_proc = setup_qemu_env(args, session_id, image_elf, uart_sock, enable_net)

    if enable_dbg and args.debug:
        qemu_args.extend(["-gdb", f"unix:{gdb_sock},server=on,wait=off", "-S"])

    gdb_proc, qemu_proc, socat_proc = None, None, None

    try:
        print(f"[Monitor] QEMU initialization: {qemu_bin} {' '.join(qemu_args)}")
        print(f"[Monitor] Launching Guest OS Emulation...")

        current_env = os.environ.copy()
        qemu_proc = subprocess.Popen(
            [qemu_bin] + qemu_args,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env=current_env
        )

        print(f"[Monitor] Waiting for QEMU to start...")
        start_time = time.time()
        qemu_ready = False

        while time.time() - start_time < 3.0:
            if qemu_proc.poll() is not None:
                print("\033[91m[Monitor] Error: QEMU exited unexpectedly.\033[0m")
                sys.exit(1)
            try:
                qemu_ready = True
                time.sleep(0.5)
                break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.05)

        if not qemu_ready:
            print("\033[91m[Monitor] Error: Timeout connecting to QEMU.\033[0m")
            sys.exit(1)

        arch = "arm" if args.chip in arm_chips else "riscv" if args.chip in riscv_chips else "i386:x86-64"
        print("\033[92m[Monitor] Spawning Terminal for Serial (socat) Console...\033[0m")
        socat_cmd = [
            "xterm", "-hold", "-title", f"Session {session_id} UART Console {arch}/{args.chip}",
            "-e", "socat", f"-,rawer", f"unix-connect:{uart_sock}"
        ]
        socat_proc = subprocess.Popen(socat_cmd, env=current_env)
        time.sleep(0.5) # Let socat connect and unblock QEMU

        if enable_dbg and args.debug:
            print("\033[92m[Monitor] Spawning Terminal for Debugging (gdb)...\033[0m")
            gdb_commands = (
                f"set architecture {arch}\n"
                f"target remote {gdb_sock}\n"
            )
            gdb_script_path = Path(project_root) / "build" / f".gdb_init_flight_{session_id}"
            gdb_script_path.write_text(gdb_commands)
            gdb_env = current_env.copy() if current_env else os.environ.copy()
            gdb_env["DEBUGINFOD_URLS"] = ""

            target_image = image_elf

            gdb_cmd = [
                "xterm", "-hold", "-title", f"Session {session_id} GDB Target {arch}/{args.chip}",
                "-e", "gdb-multiarch", target_image, "-x", str(gdb_script_path)
            ]
            gdb_proc = subprocess.Popen(gdb_cmd, env=gdb_env)
            print(f"\033[95m[Monitor] Debug mode active. GDB stub listening on UDS '{gdb_sock}'. CPU frozen at entry point.\033[0m")

        print("\033[94m[Monitor] Press 'Ctrl + ]' to exit.\033[0m")
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
        print("\r\033[96m[Monitor] Tearing down infrastructure...\033[0m")

        try:
            uart_sock.unlink(missing_ok=True)
        except Exception:
            pass

        if sniffer_proc:
            print("[Monitor] Tearing down vfio-user packet sniffer")
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
                    print(f"[Monitor] Terminated {name}.")
                except subprocess.TimeoutExpired:
                    try:
                        proc.kill()
                        proc.wait()
                        print(f"[Monitor] Force killed {name} (Timeout expired).")
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
            gdb_script = Path.cwd() / "build" / f".gdb_init_flight_{session_id}"
            try:
                gdb_script.unlink(missing_ok=True)
            except Exception:
                pass

        print("[Monitor] Teardown complete.")

if __name__ == "__main__":
    main()
