# Final Build and Demo Guide

Run all commands from the repository root.

## Clean build

```bash
rm -rf build
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Server and client

Terminal 1:

```bash
build/bin/ftp_server
```

Terminal 2:

```bash
build/bin/ftp_client 127.0.0.1 2121
```

Login:

```text
USER admin
PASS admin123
```

## Upload demo

```text
TYPE I
PUT client_files/picture.png demo-picture.png
HASH demo-picture.png
```

Expected evidence includes `150`, `226`, transfer progress and SHA-256 `MATCH`.

## Download demo

Passive:

```text
GETP demo-picture.png downloaded-picture.png
```

Active:

```text
GET demo-picture.png downloaded-active-picture.png
```

Expected evidence includes `150`, `226` and SHA-256 `MATCH`.

## Active session table

Keep Terminal 1 visible. Open two clients in separate terminals and login with
`admin` and `student`. The server prints a table containing both client IPs,
usernames and session state. Start a transfer to capture `TRANSFERRING`.

## 28-command smoke test

With the server running:

```bash
python3 tests/run_28_command_smoke.py 127.0.0.1 2121
```

## Preserve evidence

```bash
cp logs/server.log demo_evidence/logs/server_demo.log
cp logs/client.log demo_evidence/logs/client_demo.log
```