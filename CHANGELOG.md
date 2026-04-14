# Changelog

All notable changes to lease-sync are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.0] - 2026-04-14

### Fixed
- **Security**: NUL-terminate the wire-format `node_id` field immediately
  after `recvfrom`/decrypt in `peer_sync_handle_message`. Previously a single
  UDP frame carrying 64 non-zero bytes could trigger out-of-bounds reads in
  `strcmp`/`%s`/`strncpy` on the daemon stack (information disclosure / crash
  via one packet; unauthenticated in plain mode, PSK-holder in encrypted mode).
- **Correctness**: `format_mac` now takes an explicit output-buffer size
  and bounds the generic hex dump by that size. Previously the function
  silently capped output at ~16 input bytes, so peer-injected leases with
  client-IDs longer than ~16 bytes (DHCPv6 DUIDs, RFC 4361 IDs) had a
  truncated `client_id` written into the local dnsmasq, breaking client-id
  matching across the HA pair.
- **Correctness**: `parse_mac` now takes the destination buffer size as
  a parameter instead of a hardcoded 32-byte cap. Previously local DHCP
  events with client-IDs longer than 32 bytes were corrupted at origin and
  the truncated form was broadcast to peers.
- **Memory safety**: zero-initialize the `peer_addr` stack buffer in
  `config_add_peer` before `strncpy`. Previously over-long `peer=` lines
  could leak adjacent stack contents into the `peer->address` field and
  the "Invalid peer address" log line.

### Removed
- `-f` / `--foreground` CLI flag. The flag never actually changed the
  daemonization path (only the syslog `LOG_PERROR` mode), so it was misleading.
  lease-sync always runs in the foreground of its supervising process; use
  procd / systemd for detachment.
- `persist_file` and `persist_interval` configuration options. The
  on-disk lease database was configured but never read or written, so the
  options were inert. After a daemon restart, the in-memory replicated state
  is rebuilt from peers via `SYNC_REQUEST` (existing behavior). If both nodes
  restart simultaneously, replicated state is lost until new DHCP events occur
  — this is now documented in `daemon/lease-sync.conf.example`. The legacy
  keys are silently ignored if present in older configuration files.

### Changed
- Internal API: `format_mac(const unsigned char *, int, char *, size_t)` and
  `parse_mac(const char *, unsigned char *, size_t, int *)` now take an
  explicit buffer-size parameter. All in-tree callers updated.

### Validated
- All unit tests pass (5 suites). New regression tests cover the size-aware
  format/parse functions and the `peer_addr` initialization: 20-byte client-ID
  round trip through `format_mac`/`parse_mac`, undersized output buffer
  rejection, 40-byte client-ID parsed in full, and over-long peer address
  rejected without crashing or leaking stack content.

### Notes
- Wire protocol is unchanged (`LEASE_SYNC_PROTOCOL_VERSION = 1`). v1.0.0,
  v1.1.0, and v1.2.0 nodes interoperate.
- The minor bump (rather than patch) reflects the removal of two configurable
  surface areas (`-f`, `persist_*`) and an internal API signature change. The
  removed options were inert in v1.1.0 and earlier, so existing deployments
  experience no behavioral change beyond the bug fixes.

## [1.1.0] - 2026-04-11

### Changed
- Port event loop from POSIX `select()` to OpenWrt's `libubox/uloop` event
  framework. This is the idiomatic approach for OpenWrt daemons and enables
  automatic ubus reconnection via `ubus_add_uloop()`.
- Replace `select()` main loop with `uloop_fd_add` (UDP socket) and
  `uloop_timeout` (heartbeat, sync, peer check, retry queue, stats,
  reconciliation).
- Integrate ubus through `ubus_add_uloop()` instead of manual
  `ubus_handle_event()` polling.

### Removed
- `pthread_rwlock` from `lease-db.c`. The uloop event loop is single-threaded,
  so the lock is no longer needed.
- `-lpthread` linker flag (no longer required).
- Internal `daemonize()` implementation (procd handles process management on
  OpenWrt).
- Manual `SIGTERM`/`SIGINT` handlers (uloop installs its own).

### Validated
- 227 unit tests across 5 suites pass (0 failures).
- Integration tests T01-T05 pass.
- Production soak test on a dual-node HA cluster: no crashes, no memory or
  file-descriptor growth, no timer drift, and no conflict-resolution anomalies
  over the validation window. Error profile indistinguishable from the
  `select()`-based v1.0.0 baseline running in parallel on the peer node.

### Notes
- Net diff from v1.0.0: 121 insertions, 265 deletions (-144 lines), reflecting
  the simpler event model.
- Wire protocol is unchanged (`LEASE_SYNC_PROTOCOL_VERSION = 1`). v1.1.0 and
  v1.0.0 nodes interoperate.

## [1.0.0] - 2026-04-06

### Added
- Initial release of the lease-sync daemon.
- DHCP lease synchronization between HA cluster nodes over an encrypted UDP
  channel.
- Conflict detection and resolution for concurrent lease assignments.
- Retry queue for transient dnsmasq ubus failures.
- Runtime statistics (leases, sent/received, conflicts, retries).
- ubus integration for lease add/delete operations against dnsmasq.
- Unit test suite (227 tests across config, crypto, lease-db, peer-sync, util).
- Integration test suite (T01-T05) covering end-to-end synchronization.

[1.2.0]: https://github.com/pgaufillet/lease-sync/releases/tag/v1.2.0
[1.1.0]: https://github.com/pgaufillet/lease-sync/releases/tag/v1.1.0
[1.0.0]: https://github.com/pgaufillet/lease-sync/releases/tag/v1.0.0
