#!/usr/bin/env python3

"""
Smoke test for all 28 required FTP
control commands.

Usage:

    python3 tests/run_28_command_smoke.py

or:

    python3 tests/run_28_command_smoke.py \
        127.0.0.1 2121
"""

import socket
import sys


HOST = (
    sys.argv[1]
    if len(sys.argv) >= 2
    else "127.0.0.1"
)

PORT = (
    int(sys.argv[2])
    if len(sys.argv) >= 3
    else 2121
)


def recv_reply(
    sock: socket.socket
) -> str:
    data = b""

    lines = []

    multiline_code = None

    while True:
        while b"\n" in data:
            raw, data = data.split(
                b"\n",
                1
            )

            line = (
                raw
                .rstrip(b"\r")
                .decode(
                    "utf-8",
                    errors="replace"
                )
            )

            lines.append(line)

            if (
                multiline_code is None
                and len(line) >= 4
                and line[3] == "-"
            ):
                multiline_code = (
                    line[:3]
                )

            elif multiline_code is None:
                return "\n".join(
                    lines
                )

            elif line.startswith(
                multiline_code + " "
            ):
                return "\n".join(
                    lines
                )

        chunk = sock.recv(4096)

        if not chunk:
            raise RuntimeError(
                "Server closed "
                "the connection"
            )

        data += chunk


COMMANDS = [
    "USER admin",
    "PASS admin123",

    "NOOP",

    "PWD",
    "CWD /",
    "CDUP",

    "MKD smoke_dir",
    "RMD smoke_dir",

    # No data connection is opened here.
    # 425 is acceptable for these commands.
    "LIST",
    "NLST",
    "RETR sample.txt",
    "STOR sample.txt",
    "STOU",
    "APPE sample.txt",

    "STAT",
    "SIZE sample.txt",
    "MDTM sample.txt",

    "TYPE I",
    "MODE S",

    "DELE definitely_missing_smoke_file.tmp",

    "RNFR definitely_missing_smoke_file.tmp",
    "RNTO renamed_smoke_file.tmp",

    "HASH sample.txt",

    "ABOR",

    "HELP HASH",

    # Put PORT/PASV near the end so they
    # do not affect the previous commands.
    "PORT 127,0,0,1,156,64",
    "PASV",

    "QUIT",
]


assert len(COMMANDS) == 28


with socket.create_connection(
    (HOST, PORT),
    timeout=5
) as sock:
    greeting = recv_reply(
        sock
    )

    print(
        f"[GREETING] {greeting}"
    )

    for command in COMMANDS:
        verb = command.split()[0]

        sock.sendall(
            (
                command
                + "\r\n"
            ).encode()
        )

        reply = recv_reply(
            sock
        )

        first_line = (
            reply.splitlines()[0]
            if reply
            else ""
        )

        print(
            f"{verb:4s} -> "
            f"{first_line}"
        )

        # 500 means command was not
        # recognized by the server.
        if first_line.startswith(
            "500"
        ):
            raise AssertionError(
                f"{verb} was not "
                "recognized by the server"
            )


print(
    "PASS: all 28 required FTP "
    "command handlers were reached."
)