#!/usr/bin/env python3
#
# vfio_user_pkt_sniffer.py
"""
vfio-user packet sniffer management module.
"""
import os
import re
import time
import shutil
import getpass
import traceback
import signal
import subprocess
from typing import Optional, Tuple

def resolve_absolute_paths() -> Tuple[str, str, str, str]:
    """Calculate the absolute root directory of the project file"""
    current_dir = os.path.dirname(os.path.abspath(__file__)) # tools/net/
    project_root = os.path.abspath(os.path.join(current_dir, "..", ".."))

    log_dir = os.path.join(project_root, "logs")
    pcap_file_path = os.path.join(log_dir, "vfio-user.pcap")
    slog_file_path = os.path.join(log_dir, "sockdump.log")

    search_anchors = [
        os.path.abspath(os.path.join(project_root, "..")),
        os.path.abspath(os.path.join(project_root, "..", "..")),
        os.path.expanduser("~")
    ]

    return project_root, log_dir, pcap_file_path, slog_file_path

def setup_log_directory(log_dir, pcap_file_path, slog_file_path, max_backups=2):
    """Create the log directory (if it doesn't exist) and
       rotate old logs to preserve a history of executions."""
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)
        print(f"[Agent] Created logging directory: '{log_dir}/'", flush=True)

    files = [pcap_file_path, slog_file_path]

    for target in files:
        for i in range(max_backups - 1, 0, -1):
            src = f"{target}.{i}"
            dst = f"{target}.{i+1}"
            if os.path.exists(src):
                shutil.move(src, dst)
        if os.path.exists(target) and os.path.getsize(target) > 0:
            shutil.move(target, f"{target}.1")

    print(f"[Agent] Rotated historical logs (max backups: {max_backups})", flush=True)

def configure_kernel_headers_for_vfio_user_pkt_sniffer():
    """Setup BPF kernel headers for multiple distributions."""

    kernel_release = subprocess.check_output(["uname", "-r"]).decode().strip()
    build_path = f"/lib/modules/{kernel_release}/build"

    if os.path.exists(os.path.join(build_path, "include")):
        return True

    # Microsoft WSL2
    if "microsoft" in kernel_release.lower():
        subprocess.run(["sudo", "mkdir", "-p", f"/lib/modules/{kernel_release}"], check=True)
        possible_siblings = ["debian", "Ubuntu", "ubuntu"]
        for distro in possible_siblings:
            sibling_source = f"/wsl.localhost/{distro}/lib/modules/{kernel_release}/build"
            if os.path.exists(os.path.join(sibling_source, "include")):
                subprocess.run(["sudo", "ln", "-sfn", sibling_source, build_path], check=True)
                print("[!] Established cross-distribution link to kernel headers for kernel release: {kernel_release}")
                return True

        kheaders_archive = "/sys/kernel/kheaders.tar.xz"
        if os.path.exists(kheaders_archive):
            subprocess.run(["sudo", "tar", "-xf", kheaders_archive, "-C", build_path], check=True)
            print("[!] Kernel headers extracted at {build_path} from archive")
            return True

        print("[Agent] Reconstructing WSL2 upstream kernel headers from upstream repository archives...")
        subprocess.run(["sudo", "apt-get", "update"], check=True)
        subprocess.run(["sudo", "apt-get", "install", "-y", "flex", "bison", "libelf-dev", "libssl-dev", "bc"], check=True)

        version_digits = re.findall(r'\d+', kernel_release)
        major_minor = f"{version_digits[0]}.{version_digits[1]}" if len(version_digits) >= 2 else "6.6"

        branch = f"linux-msft-wsl-{major_minor}.y"
        scratch_dir = f"/var/tmp/wsl2_kernel_src_{major_minor}"
        url = "https://github.com/microsoft/WSL2-Linux-Kernel/"

        if not os.path.exists(scratch_dir):
            subprocess.run(["git", "clone", "--depth=1", "-b", branch, url, scratch_dir], check=True)
        else:
            subprocess.run(["make", "mrproper"], cwd=scratch_dir, check=True)

        subprocess.run(["make", "defconfig"], cwd=scratch_dir, check=True)
        with open(os.path.join(scratch_dir, ".config"), "a") as f:
            f.write("\nCONFIG_BPF=y\n")
            f.write("CONFIG_BPF_SYSCALL=y\n")
            f.write("CONFIG_BPF_JIT=y\n")
            f.write("CONFIG_BPF_EVENTS=y\n")
            f.write("CONFIG_BPF_EVENTS=y\n")
            f.write("CONFIG_KPROBES=y\n")
            f.write("CONFIG_HAVE_KPROBES=y\n")
            f.write("CONFIG_NET_CLS_ACT=y\n")
            f.write("CONFIG_NET_ACT_BPF=y\n")

        subprocess.run(["make", "olddefconfig"], cwd=scratch_dir, check=True)
        subprocess.run(["make", "modules_prepare"], cwd=scratch_dir, check=True)
        subprocess.run(["sudo", "ln", "-sfn", scratch_dir, build_path], check=True)
        print(f"[!] WSL2 upstream kernel headers for relase {branch} loaded to {build_path}")
        return True

    else:
        try:
            subprocess.run(["sudo", "apt-get", "update"], check=True)
            subprocess.run(["sudo", "apt-get", "install", "-y", f"linux-headers-{kernel_release}"], check=True)
            print("[!] Standard kernel headers for kernel release: {kernel_release} loaded.")
            return True
        except subprocess.CalledProcessError:
            subprocess.run(["sudo", "apt-get", "install", "-y", "linux-headers-generic"], check=True)
            print("[!] Generic kernel headers loaded.")
            return True

def configure_vfio_user_pkt_sniffer_sudo_exemption(project_root):
    """Setup passwordless sudo rules for vfio-user packet sniffer."""
    local_user = os.environ.get("USER") or os.environ.get("LOGNAME")
    sudoers_file = f"/etc/sudoers.d/vfio_user_pkt_sniffer"
    sockdump_path = f"{project_root}/tools/net/sockdump.py"
    pcap_file_path = f"{project_root}/logs/vfio-user.pcap"

    exemption_rules = (
        f"{local_user} ALL=(ALL) NOPASSWD: /usr/bin/python3 {sockdump_path} *\n"
        f"{local_user} ALL=(ALL) NOPASSWD: /usr/bin/chown {local_user}\\:{local_user} {pcap_file_path}\n"
        f"{local_user} ALL=(ALL) NOPASSWD: /usr/bin/chmod 664 {pcap_file_path}\n"
    )

    sudoers_cache_dir = os.path.expanduser("~/.config/vfio_user_pkt_sniffer")
    os.makedirs(sudoers_cache_dir, exist_ok=True)
    sudoers_cache_file = os.path.join(sudoers_cache_dir, "vfio_user_pkt_sniffer.cache")

    if os.path.exists(sudoers_file) and not os.path.exists(sudoers_cache_file):
        try:
            result = subprocess.run(["sudo", "cat", sudoers_file], capture_output=True, text=True, check=True)
            with open(sudoers_cache_file, "w") as f:
                f.write(result.stdout)
        except subprocess.CalledProcessError:
            pass
    cache_content = ""
    if os.path.exists(sudoers_cache_file):
        try:
            with open(sudoers_cache_file, "r") as f:
                cache_content = f.read()
        except IOError:
            pass

    if exemption_rules in cache_content:
        print(f"[✓ Skipping] Sudoers rules for {os.path.basename(project_root)} already configured")
        return True

    final_sudoers = cache_content + exemption_rules

    try:
        os.makedirs(sudoers_cache_dir, exist_ok=True)
        with open(sudoers_cache_file, "w") as f:
            f.write(final_sudoers)

        check_cmd = ["sudo", "visudo", "-c", "-f", sudoers_cache_file]
        result = subprocess.run(check_cmd, capture_output=True, text=True)

        if result.returncode != 0:
            print(f"\033[91m[⚠] Sudoers syntax validation failed: {result.stderr.strip()}\033[0m")
            if os.path.exists(sudoers_cache_file):
                os.remove(sudoers_cache_file)
            return False

        subprocess.run(["sudo", "cp", sudoers_cache_file, sudoers_file], check=True)
        subprocess.run(["sudo", "chown", "root:root", sudoers_file], check=True)
        subprocess.run(["sudo", "chmod", "0440", sudoers_file], check=True)

        print(f"[✓ Success] Configured: {exemption_rules} to {sudoers_file} for vfio-user packet sniffer")
        return True

    except subprocess.CalledProcessError as e:
        print(f"\033[91m[⚠] Failed to write sudoers rules: {e}\033[0m")
        return False

def start_vfio_user_pkt_sniffer(project_root, pcap_file_path, slog_file_path, socket_path) -> Optional[subprocess.Popen]:
    """Launch sockdump as a background root process to capture
       low-level IPC data going across the UDS channel."""

    print(f"[Agent] Arming vfio-user packet sniffer on {socket_path} writing to {pcap_file_path}", flush=True)

    sockdump = os.path.join(project_root, "tools", "net", "sockdump.py")

    print(f"[Agent] Starting {sockdump} with user output to {slog_file_path}", flush=True)

    with open(pcap_file_path, "w"): pass

    cmd = [
        "sudo", "-n", "python3", sockdump,
        "--format", "pcap", "--raw",
        "--output", pcap_file_path,
        "--flush",
        socket_path
    ]

    try:
        fd = open(slog_file_path, "w+", encoding="utf-8", errors="ignore", buffering=1)
        sockdump_env = os.environ.copy()
        sockdump_env["PYTHONUNBUFFERED"] = "1"

        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=fd,
            env=sockdump_env,
            start_new_session=True,
            cwd=project_root
        )

        proc.slog_fd = fd
        proc.pcap_file_path = pcap_file_path
        proc.wireshark_proc = None
        proc.wlog_fd = None
        print("[Agent] Waiting for sniffer to start...", flush=True)
        time.sleep(3.0) # Allow the sniffer enough time to initialize and bind to the socket
        if proc.poll() is not None:
            print(f"\033[31m[Agent] ERROR: Sniffer terminated early with exit code {proc.returncode}\033[0m", flush=True)
            try:
                fd.seek(0)
                err = fd.read()
                print(f"\033[31m--- SOCKDUMP STDERR ROOT CAUSE ---\n{err if err.strip() else 'No stderr payload text written.\n'}-----------------------------------\033[0m", flush=True)
            except Exception:
                pass
            fd.close()
            return None

        print(f"[Agent] Sniffer process started with PID {proc.pid}")
        return proc

    except Exception as ex:
        print(f"\033[31m[Agent] ERROR: Failed to spawn sockdump sniffer: {ex}\033[0m", flush=True)
        if 'fd' in locals():
            fd.close()
        return None

def launch_wireshark(log_dir, sniffer_proc) -> None:
    """Spawns Wireshark GUI tracker targeting the active flushed PCAP stream."""
    if sniffer_proc is None or not hasattr(sniffer_proc, 'pcap_file_path'):
        return

    print(f"[Wireshark] Opening visualization pipe for {sniffer_proc.pcap_file_path}", flush=True)

    wlog_path = os.path.join(log_dir, "wireshark.log")

    if os.path.exists(wlog_path):
        try: os.remove(wlog_path)
        except Exception: pass

    wireshark_pipe = f"tail -f -n +1 -c +0 {sniffer_proc.pcap_file_path} | wireshark -k -i -"

    print(f"[Wireshark] Starting pipe '{wireshark_pipe}' with user output to {wlog_path}", flush=True)

    try:
        fd = open(wlog_path, "w+", encoding="utf-8", errors="ignore", buffering=1)

        proc = subprocess.Popen(
            wireshark_pipe,
            shell=True,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=fd,
            start_new_session=True
        )

        sniffer_proc.wireshark_proc = proc
        sniffer_proc.wlog_fd = fd
        sniffer_proc.wpipe = wireshark_pipe
        time.sleep(1.5)

        if proc.poll() is not None:
            print(f"\033[31m[Wireshark] ERROR: Terminated early with exit code {proc.returncode}\033[0m", flush=True)
            try:
                fd.seek(0)
                err = fd.read()
                print(f"\033[31m--- WIRESHARK STDERR ROOT CAUSE ---\n{err if err.strip() else 'No stderr payload text written by graphic engine.\n'}-----------------------------------\033[0m", flush=True)
            except Exception:
                pass
            fd.close()
            sniffer_proc.wireshark_proc = None
            return

    except Exception as ex:
        print(f"\033[31m[Wireshark] ERROR: Failed to spawn UI process: {ex}\033[0m", flush=True)
        if 'fd' in locals():
            fd.close()
            return

    print(f"[Wireshark] Pipe launched with PID {proc.pid}", flush=True)

def stop_vfio_user_pkt_sniffer(sniffer_proc):
    """Detach the eBPF kernel hooks and save the pcap file."""
    if sniffer_proc is None:
        return

    if hasattr(sniffer_proc, 'wireshark_proc') and sniffer_proc.wireshark_proc:
        if sniffer_proc.wireshark_proc.poll() is None:
            print("[Wireshark] Closing visualization pipe", flush=True)
            try:
                pgid = os.getpgid(sniffer_proc.wireshark_proc.pid)
                os.killpg(pgid, 15)
                sniffer_proc.wireshark_proc.wait(timeout=2.0)
            except Exception:
                try:
                    os.killpg(pgid, 9)
                    sniffer_proc.wireshark_proc.wait(timeout=1.0)
                except Exception:
                    pass
    try:
        if sniffer_proc.poll() is None:
            start_time = time.time()
            terminated = False

            try:
                os.killpg(sniffer_proc.pid, signal.SIGINT)
            except Exception as sig_err:
                os.kill(sniffer_proc.pid, signal.SIGINT)
            while time.time() - start_time < 4.0:
                if sniffer_proc.poll() is not None:
                    terminated = True
                    break
                time.sleep(0.1)

            if not terminated:
                print("[Agent] Forcing sniffer kill...", flush=True)
                try:
                    os.killpg(sniffer_proc.pid, signal.SIGKILL)
                except Exception:
                    os.kill(sniffer_proc.pid, signal.SIGKILL)
                sniffer_proc.wait(timeout=1)

    except Exception as ex:
        print(f"\n\033[91m[Agent] FATAL: Exception thrown during sniffer teardown: {ex}\033[0m")
        traceback.print_exc()
    finally:
        if hasattr(sniffer_proc, 'slog_fd') and sniffer_proc.slog_fd:
            try: sniffer_proc.slog_fd.close()
            except Exception: pass
        if hasattr(sniffer_proc, 'wlog_fd') and sniffer_proc.wlog_fd:
            try: sniffer_proc.wlog_fd.close()
            except Exception: pass

    if os.path.exists(sniffer_proc.pcap_file_path):
        user = getpass.getuser()
        group = user
        os.system(f"sudo chown {user}:{group} {sniffer_proc.pcap_file_path}")
        os.system(f"sudo chmod 664 {sniffer_proc.pcap_file_path}")
        size = os.path.getsize(sniffer_proc.pcap_file_path)
        print(f"[Agent] Packet trace size: {size} bytes committed to log tree")
    else:
        print(f"\033[31m[Agent] ERROR: pcap file not found at {sniffer_proc.pcap_file_path}\033[0m")

    try:
        import glob
        temp_files = glob.glob("/tmp/wireshark_Standard input*.pcapng")

        for temp_file in temp_files:
            try:
                if os.stat(temp_file).st_uid == os.getuid():
                    os.remove(temp_file)
            except Exception:
                pass

        if temp_files:
            print(f"[Agent] Removed {len(temp_files)} temporary Wireshark swap files from /tmp", flush=True)
    except Exception:
        pass
