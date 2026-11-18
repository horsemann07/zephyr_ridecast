#!/usr/bin/env python3
"""RideCast Zephyr build helper (native Windows/PowerShell).

Layout:
  build.py                  - this script and root CMakeLists.txt
  app/                      - prj.conf, boards/, src/
  cc335x_port/              - wireless integration (later)
  zephyr_workspace/
    zephyr/                 - Zephyr Git submodule
    .west/                  - West workspace metadata
    .venv/                  - Python environment created by --setup
  build/                    - ONE generated build directory
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parent
WORKSPACE = ROOT / "zephyr_workspace"
ZEPHYR_DIR = WORKSPACE / "zephyr"
VENV_DIR = WORKSPACE / ".venv"
BUILD_DIR = ROOT / "build"
STATE_FILE = BUILD_DIR / ".ridecast-profile.json"
DEFAULT_BOARD = "stm32h7b3i_dk"


def venv_python():
    return VENV_DIR / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def venv_bin():
    return VENV_DIR / ("Scripts" if os.name == "nt" else "bin")


def cmd_env():
    env = os.environ.copy()
    env["VIRTUAL_ENV"] = str(VENV_DIR)
    env["PATH"] = str(venv_bin()) + os.pathsep + env.get("PATH", "")
    env["ZEPHYR_BASE"] = str(ZEPHYR_DIR)
    env.pop("PYTHONHOME", None)
    return env


def run(command, *, env=None):
    print("\n$ " + " ".join(str(part) for part in command), flush=True)
    try:
        subprocess.run(
            [str(part) for part in command],
            cwd=WORKSPACE,
            env=env,
            check=True,
        )
    except FileNotFoundError as exc:
        raise RuntimeError(f"Executable not found: {command[0]}") from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"Command failed with exit code {exc.returncode}") from exc


def west_exe():
    path = venv_bin() / ("west.exe" if os.name == "nt" else "west")
    if not path.is_file():
        raise RuntimeError("West missing. Run: python build.py --setup")
    return path


def needs_workspace():
    if not (ZEPHYR_DIR / "west.yml").is_file():
        raise RuntimeError("Zephyr submodule missing under zephyr_workspace/zephyr")
    if not (WORKSPACE / ".west" / "config").is_file():
        raise RuntimeError("West workspace missing. Run: python build.py --setup")
    if not (ROOT / "CMakeLists.txt").is_file():
        raise RuntimeError("Root CMakeLists.txt missing (check .txt.txt extension)")
    west_exe()


def setup(fetch_deps=False):
    if not (ZEPHYR_DIR / "west.yml").is_file():
        raise RuntimeError(
            "Initialize Zephyr submodule first: git submodule update --init"
        )
    if not venv_python().is_file():
        print(f"Creating Python environment: {VENV_DIR}")
        venv.EnvBuilder(with_pip=True).create(VENV_DIR)

    env = cmd_env()
    run(
        [
            venv_python(),
            "-m",
            "pip",
            "install",
            "west",
            "pyserial",
            "-r",
            ZEPHYR_DIR / "scripts" / "requirements.txt",
        ],
        env=env,
    )
    west = west_exe()
    if not (WORKSPACE / ".west" / "config").is_file():
        run([west, "init", "-l", "zephyr"], env=env)
    if fetch_deps:
        run([west, "update"], env=env)
    run([west, "zephyr-export"], env=env)
    print("\nWorkspace ready. Virtual environment activation is optional.")


def activate_shell():
    if not venv_python().is_file():
        raise RuntimeError("Python environment missing. Run --setup first")
    if os.name != "nt":
        print(f"Run: source '{venv_bin() / 'activate'}'")
        return
    activate = str(venv_bin() / "Activate.ps1").replace("'", "''")
    root = str(ROOT).replace("'", "''")
    run(
        [
            "powershell.exe",
            "-NoExit",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            f"& '{activate}'; Set-Location -LiteralPath '{root}'",
        ]
    )


def validate_board(board):
    allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-@"
    if not board or any(char not in allowed for char in board):
        raise RuntimeError("Invalid Zephyr board target")


def current_build():
    if not STATE_FILE.is_file():
        return None
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        return None


def build(board, profile, pristine=False, configure_only=False, jobs=8):
    needs_workspace()
    validate_board(board)

    wanted = {"board": board, "profile": profile}
    # Debug and release share build/: on a profile/board change, reconfigure
    # from scratch so stale Kconfig/CMake settings cannot be reused.
    old = current_build()
    must_clean = pristine or (BUILD_DIR / "CMakeCache.txt").is_file() and old != wanted
    if must_clean:
        print("Build profile/board changed (or --pristine); cleaning build/")

    # Zephyr uses Kconfig optimization choices, not merely CMAKE_BUILD_TYPE.
    config = (
        ["-DCONFIG_DEBUG=y", "-DCONFIG_DEBUG_OPTIMIZATIONS=y", "-DCONFIG_ASSERT=y"]
        if profile == "debug"
        else ["-DCONFIG_DEBUG=n", "-DCONFIG_SIZE_OPTIMIZATIONS=y", "-DCONFIG_ASSERT=n"]
    )

    command = [
        west_exe(),
        "build",
        "-b",
        board,
        "-s",
        ROOT,
        "-d",
        BUILD_DIR,
        "-p",
        "always" if must_clean else "auto",
    ]
    if configure_only:
        command.append("--cmake-only")
    else:
        command.append(f"-o=-j{jobs}")
    command += ["--", *config]

    run(command, env=cmd_env())
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    STATE_FILE.write_text(json.dumps(wanted, indent=2) + "\n", encoding="utf-8")
    print(f"\n{profile.capitalize()} output: {BUILD_DIR / 'zephyr' / 'zephyr.elf'}")


def flash():
    needs_workspace()
    if not (BUILD_DIR / "zephyr" / "zephyr.elf").is_file():
        raise RuntimeError("No firmware in build/. Run -b or -r first")
    run([west_exe(), "flash", "-d", BUILD_DIR], env=cmd_env())


def list_ports():
    if not venv_python().is_file():
        raise RuntimeError("Run --setup first")
    code = (
        "from serial.tools import list_ports\n"
        "ports = list(list_ports.comports())\n"
        "for p in ports: print(f'{p.device} | {p.description}')\n"
        "if not ports: print('No serial ports found.')"
    )
    run([venv_python(), "-c", code], env=cmd_env())


def monitor(port, baud):
    if not port:
        print("Supply --port COMx (check using --ports):")
        list_ports()
        return
    print("Serial monitor: exit with Ctrl+]")
    run([venv_python(), "-m", "serial.tools.miniterm", port, str(baud)], env=cmd_env())


def main():
    parser = argparse.ArgumentParser(
        description="RideCast STM32 + Zephyr build helper",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--setup", action="store_true", help="Set up Python/West")
    actions.add_argument(
        "-a", "--activate", action="store_true", help="Open activated PowerShell"
    )
    actions.add_argument(
        "-b", "--build", action="store_true", help="Build DEBUG firmware"
    )
    actions.add_argument(
        "-r", "--release", action="store_true", help="Build RELEASE firmware"
    )
    actions.add_argument(
        "--configure", action="store_true", help="Configure DEBUG only"
    )
    actions.add_argument("-c", "--clean", action="store_true", help="Delete build/")
    actions.add_argument(
        "-f", "--flash", action="store_true", help="Flash existing build/"
    )
    actions.add_argument("-m", "--monitor", action="store_true", help="Serial console")
    actions.add_argument("--ports", action="store_true", help="List Windows COM ports")
    actions.add_argument(
        "--all", action="store_true", help="DEBUG build + flash + optional monitor"
    )

    parser.add_argument("--board", default=DEFAULT_BOARD, help="Zephyr board target")
    parser.add_argument("--port", help="ST-LINK virtual COM port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument(
        "--pristine", action="store_true", help="Force clean reconfiguration"
    )
    parser.add_argument(
        "--fetch-deps", action="store_true", help="With --setup: west update"
    )
    parser.add_argument("-j", "--jobs", type=int, default=8, help="Parallel build jobs")

    args = parser.parse_args()
    if args.fetch_deps and not args.setup:
        parser.error("--fetch-deps requires --setup")
    if args.jobs < 1 or args.baud < 1:
        parser.error("--jobs and --baud must be positive")

    try:
        if args.setup:
            setup(args.fetch_deps)
        elif args.activate:
            activate_shell()
        elif args.clean:
            if BUILD_DIR.exists():
                shutil.rmtree(BUILD_DIR)
                print(f"Removed {BUILD_DIR}")
            else:
                print("Already clean")
        elif args.ports:
            list_ports()
        elif args.monitor:
            monitor(args.port, args.baud)
        elif args.flash:
            flash()
        elif args.configure:
            build(args.board, "debug", args.pristine, True, args.jobs)
        elif args.build or args.release:
            build(
                args.board,
                "release" if args.release else "debug",
                args.pristine,
                False,
                args.jobs,
            )
        elif args.all:
            build(args.board, "debug", args.pristine, False, args.jobs)
            flash()
            if args.port:
                monitor(args.port, args.baud)
            else:
                print(
                    "Build and flash complete. Use --ports and -m --port COMx to monitor."
                )
        return 0
    except (RuntimeError, OSError) as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
