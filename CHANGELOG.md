# Changelog

All notable changes to lease-sync are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[1.1.0]: https://github.com/pgaufillet/lease-sync/releases/tag/v1.1.0
[1.0.0]: https://github.com/pgaufillet/lease-sync/releases/tag/v1.0.0
