# Hybrid FTP Verification Report

This document records verification performed on the final integrated source. It is a technical test record, not a replacement for the course report.

## Automated verification

The source is expected to pass:

- `protocol_tests` — AuthenticationManager validation and SHA-256 known-vector/file hashing checks.
- `concurrency_tests` — thread-safe SessionRegistry operations, including concurrent session creation, update, and removal.
- `command_tests` — all 28 required FTP commands are recognized correctly, including case normalization and argument-less `STOU` parsing.
- `socket_compat_tests` — socket creation, bind, receive-timeout configuration, error handling, and socket close through the cross-platform socket abstraction.
- `rdt_integration_tests` — Go-Back-N transfer with an intentionally dropped ACK to verify timeout/retransmission behavior, followed by transferred byte-size and SHA-256 equality checks.
- `transfer_integration_tests` — complete UDP file-transfer verification through `DataChannel`, covering both Stop-and-Wait and Go-Back-N modes, followed by file-size and SHA-256 equality checks.
- `ascii_transfer_tests` — ASCII and Binary transfer-mode verification, including LF/CRLF normalization for `TYPE A` and exact byte preservation for `TYPE I`.
- `abort_transfer_tests` — transfer cancellation verification using `abortTransfer()`, including sender termination, receiver termination, busy-state reset, and cleanup of incomplete partial files.
- `session_isolation_tests` — concurrent transfer verification using same-named files in separate sessions to ensure independent temporary paths, no file collisions, and correct SHA-256 results for each session.
- `ftp_command_integration` — real TCP control-channel integration test against the running FTP server, validating authentication, command reply codes, directory operations, file metadata commands, rename/delete behavior, `PORT`, `PASV`, `ABOR`, and `QUIT`.

The complete automated test suite is expected to report:

```text
protocol_tests ..................... Passed
concurrency_tests .................. Passed
command_tests ...................... Passed
socket_compat_tests ................ Passed
rdt_integration_tests .............. Passed
transfer_integration_tests ......... Passed
ascii_transfer_tests ............... Passed
abort_transfer_tests ............... Passed
session_isolation_tests ............ Passed
ftp_command_integration ............ Passed

100% tests passed
0 tests failed

## End-to-end scenarios verified during integration

* Binary upload and SHA-256 match.
* Active-mode binary download and SHA-256 match.
* Passive-mode binary download and SHA-256 match.
* ASCII upload/download with CRLF/LF normalization.
* Detailed `LIST` transfer.
* `STOU` unique-name upload.
* `APPE` append semantics.
* `ABOR` during a large transfer, with partial-file cleanup and a still-usable TCP control connection afterward.
* Two simultaneous clients uploading same-named local files without receive-temp collisions.
* Two simultaneous clients downloading the same remote file without local receive-temp collisions.
* Successful handlers and valid FTP replies for all required control commands.
* Server logging of connected client IP addresses.
* Server logging of executed FTP commands.
* Active-session table updates for connected, authenticated, transferring, and disconnected clients.

## Test coverage summary

| Test                         | Main verification target                                         |
| ---------------------------- | ---------------------------------------------------------------- |
| `protocol_tests`             | Authentication and SHA-256                                       |
| `concurrency_tests`          | Thread-safe SessionRegistry                                      |
| `command_tests`              | Recognition of all 28 FTP commands                               |
| `socket_compat_tests`        | Cross-platform socket abstraction                                |
| `rdt_integration_tests`      | Go-Back-N, ACK loss, timeout, retransmission                     |
| `transfer_integration_tests` | End-to-end Stop-and-Wait and Go-Back-N file transfer             |
| `ascii_transfer_tests`       | ASCII newline conversion and Binary byte preservation            |
| `abort_transfer_tests`       | Real transfer cancellation and partial-file cleanup              |
| `session_isolation_tests`    | Concurrent transfer isolation and same-name collision prevention |
| `ftp_command_integration`    | Real FTP server reply/state behavior over TCP                    |

## Platform status

* POSIX socket path: clean build and runtime verification performed in the integration environment.
* Linux/macOS socket behavior is handled through the POSIX branch of `common/socket_platform.h`.
* Windows socket support is implemented through the Windows branch of `common/socket_platform.h` using Winsock-compatible operations.
* Windows is included in the project CI/build configuration.
* A real Windows runtime demonstration should still be captured for final course evidence if physical Windows runtime proof is required by the instructor.

## Logging and runtime evidence

Runtime verification should also be supported by:

```text
logs/server.log
logs/client.log
```

The server log is expected to contain:

* Connected client IP addresses.
* Session identifiers.
* Authenticated usernames.
* Executed FTP commands.
* Masked password commands.
* Transfer-state changes.
* Active-session table output.
* Client disconnect events.

Example:

```text
Accepted TCP client: 127.0.0.1

Session 1 | Client 127.0.0.1 | COMMAND | USER admin
Session 1 | Client 127.0.0.1 | COMMAND | PASS ********
Session 1 | Client 127.0.0.1 | COMMAND | PWD

==================== ACTIVE SESSIONS ====================
ID        CLIENT IP           USER              STATE
=========================================================
1         127.0.0.1           admin             TRANSFERRING
2         127.0.0.1           student           IDLE
=========================================================
Total active sessions: 2
=========================================================
```

Final demonstration evidence should be stored separately under:

```text
demo_evidence/
├── logs/
│   ├── server_demo.log
│   └── client_demo.log
└── screenshots/
```

## Known protocol scope

`MODE S` is implemented and used for Stream transfer mode.

`MODE B` and `MODE C` are recognized but their transfer semantics are not implemented. They return:

```text
504 Command not implemented for that parameter.
```

These additional transfer modes should only be implemented if the course rubric explicitly requires Block mode or Compressed mode semantics.

## Verification conclusion

The final verification combines:

* Unit-level protocol checks.
* Command parser verification.
* Real TCP control-channel tests.
* UDP file-transfer integration tests.
* Stop-and-Wait reliability tests.
* Go-Back-N Sliding Window tests.
* ACK-loss and retransmission verification.
* ASCII/Binary transfer verification.
* SHA-256 integrity checking.
* ABOR cancellation and cleanup testing.
* Concurrent session isolation testing.
* Cross-platform socket testing.
* Live upload/download demonstrations.
* Server/client logs and demo screenshots.

The project should be considered technically verified only when the complete automated test suite passes and the required live demonstration evidence has been captured from the final integrated source.