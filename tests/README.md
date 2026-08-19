# Test Guide

## 1. Clean build and CTest

```bash
rm -rf build
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The automated suite verifies authentication/SHA-256, session-registry concurrency, all 28 command parser entries, socket compatibility, and Go-Back-N recovery from an injected ACK loss.

## 2. 28-command server smoke test

Start the server:

```bash
build/bin/ftp_server
```

Then:

```bash
python3 tests/run_28_command_smoke.py 127.0.0.1 2121
```

This test confirms that every required command reaches a server handler. Data commands that need a UDP setup may intentionally return `425` in this control-only smoke test; successful file-transfer behavior must be verified separately.

## 3. Manual end-to-end checklist

Use at least two terminals/clients and verify:

- Correct and incorrect USER/PASS authentication.
- `LIST` and `NLST` through the data channel.
- `GET` active-mode download.
- `GETP` passive-mode download.
- `PUT` / `STOR` upload.
- `STOU` without a remote pathname.
- `APPE` appends to the requested remote file.
- `TYPE A` text transfer preserves logical line endings.
- `TYPE I` binary transfer preserves exact bytes.
- SHA-256 reports `MATCH` after STOR/RETR.
- Transfer progress reaches 100%.
- `ABOR` cancels a large transfer, removes partial receive data, and leaves the control session usable.
- Two clients can transfer concurrently without temporary-file collisions.
- `PORT` and `PASV` both return successful replies.
- File/directory commands (`PWD`, `CWD`, `CDUP`, `MKD`, `RMD`, `SIZE`, `MDTM`, `RNFR`, `RNTO`, `DELE`) return expected FTP replies.

## 4. Strict compiler warnings

```bash
cmake -S . -B build_strict -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"
cmake --build build_strict --parallel
```

For Windows, use the normal MSVC warning configuration or the GitHub Actions matrix in `.github/workflows/build.yml`.