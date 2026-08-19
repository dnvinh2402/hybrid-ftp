#!/usr/bin/env python3
"""Functional FTP control-channel integration test.

This script starts the real ftp_server executable, opens a TCP control
connection, authenticates, exercises representative state-changing and
metadata commands, verifies the expected FTP reply codes, and cleans up all
files/directories it creates.

It complements run_28_command_smoke.py:
- run_28_command_smoke.py proves all 28 handlers are reachable.
- this test verifies that important control commands return the expected
  success/error codes and update server state correctly.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import socket
import subprocess
import sys
import time


HOST = "127.0.0.1"
PORT = 2121


class FTPControlConnection:
    def __init__(self, sock: socket.socket) -> None:
        self.sock = sock
        self.buffer = bytearray()

    def _read_line(self) -> str:
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("Server closed the control connection")
            self.buffer.extend(chunk)

        raw, _, remaining = self.buffer.partition(b"\n")
        self.buffer = bytearray(remaining)
        return raw.rstrip(b"\r").decode("utf-8", errors="replace")

    def receive_reply(self) -> str:
        first = self._read_line()
        lines = [first]

        if len(first) >= 4 and first[:3].isdigit() and first[3] == "-":
            terminator = first[:3] + " "

            while True:
                line = self._read_line()
                lines.append(line)

                if line.startswith(terminator):
                    break

        return "\n".join(lines)

    def command(self, text: str) -> str:
        self.sock.sendall((text + "\r\n").encode("utf-8"))
        return self.receive_reply()


def reply_code(reply: str) -> str:
    first = reply.splitlines()[0] if reply else ""
    return first[:3] if len(first) >= 3 else ""


def expect_code(connection: FTPControlConnection, command: str, code: str) -> str:
    reply = connection.command(command)
    actual = reply_code(reply)

    print(f"{command:<34} -> {reply.splitlines()[0] if reply else '<empty>'}")

    if actual != code:
        raise AssertionError(
            f"{command!r}: expected FTP code {code}, got {actual}: {reply!r}"
        )

    return reply


def load_demo_account(project_root: Path) -> tuple[str, str]:
    users_file = project_root / "config" / "users.txt"

    for raw_line in users_file.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()

        if not line or line.startswith("#") or ":" not in line:
            continue

        username, password = line.split(":", 1)
        return username.strip(), password.strip()

    raise RuntimeError("No username:password entry found in config/users.txt")


def wait_for_server(timeout_seconds: float = 8.0) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error: OSError | None = None

    while time.monotonic() < deadline:
        try:
            with socket.create_connection((HOST, PORT), timeout=0.25):
                return
        except OSError as exc:
            last_error = exc
            time.sleep(0.1)

    raise RuntimeError(f"ftp_server did not start on {HOST}:{PORT}: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--server",
        required=True,
        help="Path to the compiled ftp_server executable",
    )
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    server_path = Path(args.server).resolve()

    if not server_path.exists():
        raise FileNotFoundError(f"ftp_server not found: {server_path}")

    username, password = load_demo_account(project_root)

    unique = f"integration_{os.getpid()}_{int(time.time() * 1000)}"
    directory_name = unique + "_dir"
    original_name = unique + "_old.txt"
    renamed_name = unique + "_new.txt"

    ftp_root = project_root / "ftp_root"
    ftp_root.mkdir(parents=True, exist_ok=True)
    original_path = ftp_root / original_name
    renamed_path = ftp_root / renamed_name
    directory_path = ftp_root / directory_name

    original_path.write_text("FTP command integration test\n", encoding="utf-8")

    server = subprocess.Popen(
        [str(server_path)],
        cwd=str(project_root),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    try:
        wait_for_server()

        with socket.create_connection((HOST, PORT), timeout=5.0) as sock:
            sock.settimeout(5.0)
            ftp = FTPControlConnection(sock)

            greeting = ftp.receive_reply()
            if reply_code(greeting) != "220":
                raise AssertionError(f"Expected 220 greeting, got: {greeting!r}")
            print(f"GREETING                           -> {greeting.splitlines()[0]}")

            # Authentication gate.
            expect_code(ftp, "PWD", "530")
            expect_code(ftp, f"USER {username}", "331")
            expect_code(ftp, f"PASS {password}", "230")

            # Session and directory state.
            expect_code(ftp, "NOOP", "200")
            expect_code(ftp, "PWD", "257")
            expect_code(ftp, f"MKD {directory_name}", "257")
            expect_code(ftp, f"CWD {directory_name}", "250")

            pwd_in_dir = expect_code(ftp, "PWD", "257")
            if directory_name not in pwd_in_dir:
                raise AssertionError("PWD did not reflect the CWD operation")

            expect_code(ftp, "CDUP", "250")
            expect_code(ftp, f"RMD {directory_name}", "250")

            # Transfer representation/mode state.
            expect_code(ftp, "TYPE A", "200")
            expect_code(ftp, "TYPE I", "200")
            expect_code(ftp, "MODE S", "200")

            # File metadata and integrity.
            expect_code(ftp, f"SIZE {original_name}", "213")
            expect_code(ftp, f"MDTM {original_name}", "213")
            hash_reply = expect_code(ftp, f"HASH {original_name}", "213")

            first_hash_line = hash_reply.splitlines()[0]
            if "SHA256" not in first_hash_line or len(first_hash_line.split()[-1]) != 64:
                raise AssertionError(f"HASH did not return a SHA-256 digest: {hash_reply!r}")

            expect_code(ftp, "STAT", "211")
            expect_code(ftp, "HELP HASH", "214")

            # Rename and delete are verified against the real ftp_root.
            expect_code(ftp, f"RNFR {original_name}", "350")
            expect_code(ftp, f"RNTO {renamed_name}", "250")

            if original_path.exists() or not renamed_path.exists():
                raise AssertionError("RNFR/RNTO did not rename the server file")

            expect_code(ftp, f"DELE {renamed_name}", "250")

            if renamed_path.exists():
                raise AssertionError("DELE did not remove the server file")

            # Both data-connection setup commands should be accepted even
            # though this test intentionally performs no data transfer.
            expect_code(ftp, "PORT 127,0,0,1,156,64", "200")
            pasv_reply = expect_code(ftp, "PASV", "227")

            if "(" not in pasv_reply or ")" not in pasv_reply:
                raise AssertionError("PASV reply did not contain h1,h2,h3,h4,p1,p2")

            # PASV leaves an idle DataChannel open. ABOR with no active
            # transfer must close it and return 225.
            expect_code(ftp, "ABOR", "225")
            expect_code(ftp, "QUIT", "221")

        print("ftp_command_integration passed: control-channel state and reply codes verified")
        return 0

    finally:
        for path in (original_path, renamed_path):
            try:
                path.unlink()
            except FileNotFoundError:
                pass

        try:
            directory_path.rmdir()
        except OSError:
            pass

        if server.poll() is None:
            server.terminate()

            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=3)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - test runner should show full reason
        print(f"FAIL: {exc}", file=sys.stderr)
        raise