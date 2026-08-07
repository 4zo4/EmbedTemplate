#!/usr/bin/env python3

from getpass import getpass
import grp
import json
import os
import subprocess
import sys

def install_system_packages(package_list):
    """Update repositories and install a list of system packages."""
    if not package_list:
        return True
    try:
        print(f"[+] Processing system installation for: {', '.join(package_list)}")
        subprocess.run(["sudo", "apt", "update"], check=True)
        env = os.environ.copy()
        env["DEBIAN_FRONTEND"] = "noninteractive"
        subprocess.run(["sudo", "-E", "apt", "install", "-y"] + package_list, env=env, check=True)
        return True
    except subprocess.CalledProcessError as e:
        print(f"[-] Native package manager reported a failure: {e}")
        return False

try:
    from InquirerPy import inquirer
    from InquirerPy.base.control import Choice
except ModuleNotFoundError:

    if install_system_packages(["python3-inquirerpy"]):
        print("[✓ Success] python3-inquirerpy installed.\n")
        os.execv(sys.executable, [sys.executable] + sys.argv)
    else:
        print("[-] Please execute manually: sudo apt install python3-inquirerpy")
        sys.exit(1)

def load_json(path):
    if os.path.exists(path):
        with open(path, "r") as f:
            try:
                return json.load(f)
            except json.JSONDecodeError:
                print(f"[-] Warning: Failed to parse '{path}'.")
                return None
    return None

def is_package_installed(package_name):
    try:
        result = subprocess.run(
            ["dpkg-query", "-W", "-f=${Status}", package_name],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return "install ok installed" in result.stdout
    except FileNotFoundError:
        return False

def is_qemu_vfio_user_supported():
    """Verify if the installed qemu-system-x86_64 explicitly supports vfio-user-pci."""
    try:
        qemu_check = subprocess.run(["which", "qemu-system-x86_64"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if qemu_check.returncode != 0:
            return False

        result = subprocess.run(
            ["qemu-system-x86_64", "-device", "help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        return "vfio-user-pci" in result.stdout
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False

def configure_vfio_user_dissector():
    """Deploy the vfio_user.lua Wireshark packet dissector plugin."""
    source_dir = os.path.dirname(os.path.abspath(__file__))
    source_lua = os.path.join(source_dir, "tools", "net", "vfio_user.lua")

    target_plugin_dir = os.path.expanduser("~/.config/wireshark/plugins")
    target_lua = os.path.join(target_plugin_dir, "vfio_user.lua")

    if os.path.exists(target_lua):
        return

    if not os.path.exists(source_lua):
        print(f"    [-] Warning: Source plugin file missing from repository asset tree at: {source_lua}")
        print("    [-] Skipping vfio-user dissector deployment.")
        return

    try:
        os.makedirs(target_plugin_dir, exist_ok=True)
        import shutil
        shutil.copy2(source_lua, target_lua)
        print("[✓ Success] Wireshark 'vfio-user' packet dissector activated at {target_lua}!")
    except Exception as e:
        print(f"    [-] Failed to mirror plugin binary script asset: {e}")

def build_source_repo(repo_name, repo_path):
    """Execute target compilation recipes based on the repository identity, skipping if already installed."""
    if repo_name == "linux":
        print(f"[🌲 Kernel] Source tree checked out at {repo_path}. Ready for manual configuration.")
        return

    if repo_name == "libvfio-user":
        if os.path.exists(os.path.join(repo_path, "build", "lib", "libvfio-user.so")):
            print(f"[✓ Skipping] 'libvfio-user' already built in: {repo_path}/build")
            return

    elif repo_name == "verilator":
        if os.path.exists(os.path.join(repo_path, "bin", "verilator_bin")):
            print(f"[✓ Skipping] 'verilator' already built in: {repo_path}/bin")
            return

    elif repo_name == "qemu":
        if is_qemu_vfio_user_supported():
            print("'qemu-system-x86_64' with 'vfio-user-pci' support already built and installed")
            return
        else:
            print("[!] QEMU status: Either missing or lacks 'vfio-user-pci' capability. Initiating build recipe...")

    print(f"[🔧 Build] Starting source compilation for: {repo_name}")
    try:
        if repo_name == "libvfio-user":
            print("    -> Running: meson setup build && ninja -C build")
            build_dir = os.path.join(repo_path, "build")
            if not os.path.exists(build_dir):
                subprocess.run(["meson", "setup", "build"], cwd=repo_path, check=True)
            subprocess.run(["ninja", "-C", "build"], cwd=repo_path, check=True)

        elif repo_name == "verilator":
            print("    -> Running: autoconf && ./configure && make")
            if not os.path.exists(os.path.join(repo_path, "configure")):
                subprocess.run(["autoconf"], cwd=repo_path, check=True)
            if not os.path.exists(os.path.join(repo_path, "Makefile")):
                subprocess.run(["./configure"], cwd=repo_path, check=True)
            subprocess.run(["make", "-j", str(os.cpu_count() or 2)], cwd=repo_path, check=True)

        elif repo_name == "qemu":
            if is_package_installed("qemu-system-x86"):
                print("    [!] Conflict Detected: Removing pre-packaged distro 'qemu-system-x86'...")
                subprocess.run(["sudo", "apt", "purge", "-y", "qemu-system-x86"], check=True)

            print("    -> Running QEMU configure pipeline...")
            config_cmd = [
                "./configure",
                "--target-list=x86_64-softmmu",
                "--enable-kvm",
                "--enable-debug",
                "--enable-slirp"
            ]
            subprocess.run(config_cmd, cwd=repo_path, check=True)

            print(f"    -> Compiling with make using {os.cpu_count() or 2} execution jobs...")
            subprocess.run(["make", "-j", str(os.cpu_count() or 2)], cwd=repo_path, check=True)

            build_dir = os.path.join(repo_path, "build")
            if os.path.exists(build_dir):
                print("    -> Deploying binary to system path: sudo ninja install")
                subprocess.run(["sudo", "ninja", "install"], cwd=build_dir, check=True)
                subprocess.run(["hash", "-r"], shell=True)
                print("[✓] Verified Local QEMU Installation")
                subprocess.run(["qemu-system-x86_64", "--version"])
            else:
                print("    [-] Error: Meson 'build/' directory missing. System deployment skipped.")

        print(f"[✓ Success] Finished compiling {repo_name}!")
    except subprocess.CalledProcessError as e:
        print(f"[🗙 Error] Compilation failed for {repo_name}: {e}")

def configure_wireshark_permissions():
    """Applies non-root capture capabilities to dumpcap for Debian/Ubuntu environments."""
    print("Applying Wireshark Non-Root Capture Privileges...")
    current_user = os.environ.get("USER") or os.environ.get("LOGNAME") or getpass.getuser()
    try:
        try:
            wireshark_group = grp.getgrnam("wireshark")
            user_in_wireshark = current_user in wireshark_group.gr_mem
        except KeyError:
            wireshark_group = None
            user_in_wireshark = False
        try:
            sudo_group = grp.getgrnam("sudo")
            user_in_sudo = current_user in sudo_group.gr_mem
        except KeyError:
            user_in_sudo = False

        user_in_group = user_in_wireshark or user_in_sudo
        capabilities_set = False
        if os.path.exists("/usr/bin/dumpcap"):
            cap_check = subprocess.run(
                ["getcap", "/usr/bin/dumpcap"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            if ("cap_net_admin" in cap_check.stdout
                and "cap_net_raw" in cap_check.stdout
            ):
                capabilities_set = True

        if user_in_group and capabilities_set:
            print("[✓ Skipping] Wireshark already configured", flush=True)
            return

        if not wireshark_group:
            print("    [→] Creating 'wireshark' system group...")
            subprocess.run(["sudo", "groupadd", "-f", "wireshark"], check=True)
        if not user_in_wireshark:
            print(f"    [→] Registering current user to 'wireshark' group...")
            subprocess.run(["sudo", "usermod", "-aG", "wireshark", current_user], check=True)

        if os.path.exists("/usr/bin/dumpcap"):
            print("    [→] Applying CAP_NET_RAW and CAP_NET_ADMIN capabilities to dumpcap...")
            subprocess.run(["sudo", "setcap", "CAP_NET_RAW+eip CAP_NET_ADMIN+eip", "/usr/bin/dumpcap"], check=True)
            print("    [→] Ensuring executable access on dumpcap...")
            subprocess.run(["sudo", "chmod", "+x", "/usr/bin/dumpcap"], check=True)
            print("[✓ Success] Wireshark capture permissions configured! Please log out and back in to apply group changes.")
        else:
            print("    [-] Warning: /usr/bin/dumpcap not found. Wireshark package might have failed to install.")
    except subprocess.CalledProcessError as e:
        print(f"    [-] Failed to configure Wireshark capabilities: {e}")

def configure_x11_resources():
    """Append the Xresources merge string to the user's .bashrc file."""
    bashrc_path = os.path.expanduser("~/.bashrc")
    target_line = "xrdb -merge ~/.Xresources\n"

    if os.path.exists(bashrc_path):
        with open(bashrc_path, "r") as f:
            content = f.readlines()
        if any("xrdb -merge ~/.Xresources" in line for line in content):
            print("[✓ Skipping] '.bashrc' already contains Xresources initialization configuration")
            return

    with open(bashrc_path, "a") as f:
        f.write(f"{target_line}")
    print("[✓ Success] X11 startup rules appended to configuration profile")

def main():
    blueprint = load_json("prerequisites_config.json")
    if not blueprint:
        print("[-] Error: 'prerequisites_config.json' missing from root directory.")
        sys.exit(1)

    path_defaults = blueprint.get("default_paths", {})
    default_tools_dir = path_defaults.get("clone_tools_dir", "~/tools")
    default_os_dir = path_defaults.get("clone_os_dir", "~/kernel")

    previous_state = load_json("prerequisites.json")
    past_scopes = []
    past_tools_dir = default_tools_dir
    past_os_dir = default_os_dir

    if previous_state:
        print("[+] Existing configurations resolved from 'prerequisites.json'.")
        past_scopes = previous_state.get("meta", {}).get("selected_scopes", [])
        past_tools_dir = previous_state.get("meta", {}).get("clone_tools_directory", default_tools_dir)
        past_os_dir = previous_state.get("meta", {}).get("clone_os_directory", default_os_dir)

    tools_input = inquirer.text(
        message="Specify absolute path to host tool repositories:",
        default=past_tools_dir,
    ).execute()
    resolved_tools_dir = os.path.abspath(os.path.expanduser(tools_input.strip() if tools_input.strip() else default_tools_dir))

    os_input = inquirer.text(
        message="Specify absolute path to host operating system trees:",
        default=past_os_dir,
    ).execute()
    resolved_os_dir = os.path.abspath(os.path.expanduser(os_input.strip() if os_input.strip() else default_os_dir))

    scopes = blueprint.get("scopes", [])
    menu_choices = [Choice(name=s["name"], value=s, enabled=(s["name"] in past_scopes)) for s in scopes]

    selected_scopes = inquirer.checkbox(
        message="Select installation options:\nUse SPACE to select, ARROW KEYS to navigate, and ENTER to confirm selection",
        choices=menu_choices,
    ).execute()

    final_packages = set(blueprint.get("base_requirements", {}).get("packages", []))
    final_repos = {}

    def add_repos(repo_list):
        for r_entry in repo_list:
            r_path = r_entry["path"]
            r_type = r_entry.get("type", "tools")

            if r_type == "os":
                final_repos[r_path] = resolved_os_dir
            else:
                final_repos[r_path] = resolved_tools_dir

    add_repos(blueprint.get("base_requirements", {}).get("repos", []))

    selected_names = []
    for scope in selected_scopes:
        selected_names.append(scope["name"])
        final_packages.update(scope.get("packages", []))
        add_repos(scope.get("repos", []))

    compiled_config = {
        "meta": {
            "clone_tools_directory": resolved_tools_dir,
            "clone_os_directory": resolved_os_dir,
            "selected_scopes": selected_names,
        },
        "packages": sorted(list(final_packages)),
        "repos": [{"path": r_path, "destination": r_dest} for r_path, r_dest in sorted(final_repos.items())],
    }

    with open("prerequisites.json", "w") as f:
        json.dump(compiled_config, f, indent=2)

    print("Verifying Local Package System State...")
    packages_to_install = [pkg for pkg in compiled_config["packages"] if not is_package_installed(pkg)]

    if packages_to_install:
        print(f"[+] Missing dependencies detected: {', '.join(packages_to_install)}")
        if not install_system_packages(packages_to_install):
            print("[-] Native Package alignment halted due to execution errors")
        else:
            print("[+] Compilation dependency tools are available")

    print("Processing Repositories & Building From Source...")

    for repo_entry in compiled_config["repos"]:
        r_path = repo_entry["path"]
        repo_destination_dir = repo_entry["destination"]

        repo_name = r_path.split("/")[-1]
        repo_final_path = os.path.join(repo_destination_dir, repo_name)

        os.makedirs(repo_destination_dir, exist_ok=True)

        if os.path.exists(repo_final_path) and os.path.isdir(os.path.join(repo_final_path, ".git")):
            print(f"[→] Repository '{repo_name}' already downloaded at: {repo_final_path}")
        else:
            print(f"[→] Downloading source: {repo_name} -> {repo_final_path}")
            try:
                subprocess.run(["git", "clone", f"https://github.com/{r_path}.git", repo_final_path], check=True)
            except subprocess.CalledProcessError:
                print(f"[-] Failed to fetch repository: {r_path}")
                continue

        build_source_repo(repo_name, repo_final_path)

    if "Networking Tools" in selected_names:
        configure_wireshark_permissions()
        configure_vfio_user_dissector()

    configure_x11_resources()

    print("[+] Setup manager completed")

if __name__ == "__main__":
    main()
