#!/usr/bin/env python3
#
# vfio_user_pkt_sniffer.py
"""
vfio-user packet sniffer management module.
"""
import os
import time
import shutil
import getpass
import traceback
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

    #print(f"[Agent] Disarming vfio-user packet sniffer...", flush=True)

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

            subprocess.run(["sudo", "kill", "-2", str(sniffer_proc.pid)], check=False)
            while time.time() - start_time < 4.0:
                if sniffer_proc.poll() is not None:
                    terminated = True
                    break
                time.sleep(0.1)

            if not terminated:
                print("[Agent] Forcing sniffer kill...", flush=True)
                subprocess.run(["sudo", "kill", "-9", str(sniffer_proc.pid)], check=False)
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
