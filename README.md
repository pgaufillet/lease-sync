# lease-sync

DHCP Lease Synchronization Daemon for dnsmasq High Availability

## Overview

The `lease-sync` daemon provides active/active high availability for dnsmasq DHCP servers by synchronizing lease information across multiple nodes in real-time.

### Features

- Real-time lease synchronization via UDP
- AES-256-GCM encryption (PSK)
- Conflict resolution using timestamps (Last-Write-Wins)
- Message deduplication to prevent loops
- Heartbeat monitoring for peer health
- IPv4 and IPv6 support

### Architecture

```
┌─────────────────┐         ┌─────────────────┐
│   dnsmasq (1)   │         │   dnsmasq (2)   │
│   DHCP Server   │         │   DHCP Server   │
└────────┬────────┘         └────────┬────────┘
         │ ubus events               │ ubus events
         ▼                           ▼
┌─────────────────┐         ┌─────────────────┐
│  lease-sync (1) │◄───────►│  lease-sync (2) │
│     daemon      │   UDP   │     daemon      │
└─────────────────┘  sync   └─────────────────┘
```

## Building

### Prerequisites

This daemon requires OpenWrt's ubus IPC system (libubus, libubox). These libraries are **OpenWrt-specific** and not available in standard Linux distributions.

```bash
# OpenWrt only
opkg update
opkg install libubus-dev libubox-dev libopenssl-dev
```

**Note**: For non-OpenWrt systems, ubus must be built from source (see [OpenWrt libubox](https://git.openwrt.org/project/libubox.git) and [OpenWrt ubus](https://git.openwrt.org/project/ubus.git)).

### Compile

```bash
cd daemon/
make
```

### Install

```bash
make install
```

Installs to:
- Binary: `/usr/sbin/lease-sync`
- Config: `/etc/lease-sync/`

## Configuration

Create `/etc/lease-sync/config`:

```ini
# Peer nodes
peer=192.168.1.10
peer=192.168.1.11

# Network
sync_port=5378

# Security (recommended)
security_mode=encrypted
psk_key=<64-character hex key>

# Timing
sync_interval=30
peer_timeout=120

# Logging (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG)
log_level=2
```

Generate an encryption key:
```bash
head -c 32 /dev/urandom | xxd -p -c 32
```

### Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `peer` | none | Peer IP address (max 10 peers) |
| `sync_port` | 5378 | UDP port for synchronization |
| `bind_address` | none | Explicit bind address for sync traffic (optional) |
| `security_mode` | encrypted | `encrypted` or `plain` |
| `psk_key` | none | 64-char hex key for encryption |
| `sync_interval` | 30 | Sync request interval (seconds) |
| `peer_timeout` | 120 | Peer timeout threshold (seconds) |
| `log_level` | 2 | Logging verbosity |

## Command-Line Options

```
  -c, --config FILE      Configuration file
  -d, --debug            Enable debug logging
  -v, --version          Print version
  -h, --help             Print help
```

## Limitations

The following limits are compile-time constants defined in `daemon/common.h`:

| Limit | Value | Description |
|-------|-------|-------------|
| **MAX_PEERS** | 10 | Maximum number of peer nodes |
| **MAX_HOSTNAME_LEN** | 256 | Maximum DHCP hostname length |
| **MAX_CLIENT_ID_LEN** | 256 | Maximum DHCP client ID length |
| **MAX_NODE_ID_LEN** | 64 | Maximum node identifier length |
| **MAX_UDP_PACKET_SIZE** | 1400 | Maximum sync message size (bytes) |
| **HASH_TABLE_SIZE** | 1024 | Lease database hash buckets |
| **RETRY_QUEUE_SIZE** | 64 | Maximum pending retry operations |

**To change these limits:**
1. Edit `daemon/common.h`
2. Rebuild: `make clean && make`
3. Reinstall: `make install`

**Recommended values:**
- **MAX_PEERS**: Increase to 16-32 for larger deployments
- **HASH_TABLE_SIZE**: Increase to 2048 or 4096 for >500 active leases
- **RETRY_QUEUE_SIZE**: Increase to 128 if frequent dnsmasq restarts occur

## Integration with dnsmasq

lease-sync requires dnsmasq with ubus support (`add_lease`, `delete_lease` methods).

**Note on Init Scripts:** This directory includes a standalone `files/lease-sync.init` script for generic OpenWrt deployments. If you're using the ha-cluster package from `openwrt-ha-feed`, a different UCI-integrated init script is used instead. The standalone version is intended for upstream contribution and manual installations.

### OpenWrt with dnsmasq-ha Package (Recommended)

When using the `dnsmasq-ha` package, integration is automatic via hotplug:

1. Install packages:
   ```bash
   opkg install dnsmasq-ha lease-sync
   ```

2. Configure `/etc/lease-sync/config` with peer addresses and encryption key

3. Start services:
   ```bash
   /etc/init.d/lease-sync enable
   /etc/init.d/lease-sync start
   ```

The hotplug script (`/etc/hotplug.d/dhcp/50-lease-sync`) automatically publishes DHCP events to ubus.

### Standalone Setup

For custom OpenWrt setups without the ha-cluster package, use the included `dhcp-script-ha.sh`.

The DHCP script bridges dnsmasq events to lease-sync: dnsmasq calls it on every lease event (add, renew, release), and the script publishes the event to ubus where lease-sync picks it up for synchronization.

**Prerequisite:** lease-sync requires a dnsmasq version that implements `add_lease` and `delete_lease` ubus methods. Stock dnsmasq does not include these — use the `dnsmasq-ha` package or this fork.

1. **Install the DHCP script**:
   ```bash
   cp dhcp-script-ha.sh /usr/lib/dnsmasq/
   chmod +x /usr/lib/dnsmasq/dhcp-script-ha.sh
   ```

2. **Configure dnsmasq** (`/etc/dnsmasq.conf`):
   ```
   enable-ubus
   dhcp-script=/usr/lib/dnsmasq/dhcp-script-ha.sh
   ```

3. **Configure firewall** to allow UDP port 5378 between peers

4. **Start services**:
   ```bash
   /etc/init.d/dnsmasq restart
   /etc/init.d/lease-sync start
   ```

## Monitoring

### Check Status

```bash
# View logs
logread | grep lease-sync

# Check if running
ps | grep lease-sync

# Network connections
netstat -ulnp | grep 5378
```

### Verify Synchronization

```bash
# Check leases on each node
ubus call dnsmasq dhcp-leases
```

### Statistics

The daemon logs statistics every 5 minutes:
```
lease-sync: Statistics: leases=42 (local=30, peer=12), added=50, updated=120, deleted=8, ...
```

- `leases` - Current entries in lease-sync's in-memory database
- `local` - Leases originated from this node's dnsmasq
- `peer` - Leases received from peer nodes
- `added/updated/deleted` - Cumulative counters since daemon start

## Troubleshooting

### No Synchronization

1. Check daemon is running: `ps | grep lease-sync`
2. Verify peer connectivity: `ping <peer-ip>`
3. Check firewall allows UDP 5378
4. Enable debug: `/usr/sbin/lease-sync -f -d`

### Conflicts Not Resolving

- Ensure NTP is running on all nodes (clock synchronization required)
- Check timestamps in debug logs

### High Memory Usage

- Normal: ~2 MB base + ~1 KB per lease
- 1000 leases ≈ 3 MB RAM

## Performance

| Metric | Value |
|--------|-------|
| Sync latency | <100ms on LAN |
| Memory | ~2 MB base + 1 KB/lease |
| CPU (idle) | <1% |
| Bandwidth | ~500 bytes per lease update |

## Protocol

### Message Format

```c
struct sync_message {
    uint32_t magic;           // 0x4C53594E ("LSYN")
    uint8_t  version;         // Protocol version
    uint8_t  action;          // ADD/UPDATE/DELETE/HEARTBEAT
    uint16_t sequence;        // Message sequence number
    char     node_id[64];     // Originating node
    uint64_t timestamp_ms;    // Millisecond timestamp
    // ... lease data
};
```

### Conflict Resolution

- Uses Last-Write-Wins (LWW) strategy
- Compares `timestamp_ms` field
- Requires synchronized clocks (NTP recommended)

## Security

See [SECURITY.md](SECURITY.md) for detailed security configuration including:
- AES-256-GCM PSK encryption (recommended)
- Plain mode with WireGuard/IPsec
- Threat model and recommendations

## Testing

```bash
# Run all unit tests (any Linux with GCC + OpenSSL)
./tests/run-tests.sh --unit

# Or from daemon/ directory
make test
```

Unit tests cover the daemon's core modules:

| Script | Module |
|--------|--------|
| `run-crypto-tests.sh` | AES-256-GCM encryption |
| `run-retry-queue-tests.sh` | Injection retry queue |
| `run-util-tests.sh` | Utility functions |
| `run-config-tests.sh` | Configuration parsing |
| `run-lease-db-tests.sh` | Lease database |
| `run-peer-sync-tests.sh` | Peer sync protocol |

**Prerequisites:** GCC, OpenSSL development libraries (`apt-get install build-essential libssl-dev` on Debian/Ubuntu).

For end-to-end HA cluster testing with real DHCP clients, see the container-based test infrastructure in `openwrt-ha-feed/tests/`.

## License

GPL-2.0-or-later (same as dnsmasq)
lease-sync has been developed using Claude Code from Anthropic.

## Maintainer

Pierre Gaufillet <pierre.gaufillet@bergamote.eu>
