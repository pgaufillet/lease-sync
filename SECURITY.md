# Security

## Threat Model

lease-sync synchronizes DHCP lease data between peer nodes over UDP. The security goal is to protect this data in transit: confidentiality (lease contents), integrity (no tampering), and authenticity (only trusted peers).

**Trust boundaries:**

- **Peers** share a pre-shared key (PSK). A compromised key exposes all sync traffic.
- **dnsmasq** is trusted — lease-sync communicates with it via local ubus IPC.
- **The DHCP script** (`dhcp-script-ha.sh`) constructs JSON by string concatenation with input sanitization (backslash/quote escaping). dnsmasq also sanitizes DHCP client input (hostnames, client IDs) before passing it to the script.

**Out of scope:** host-level compromise, physical access, attacks on dnsmasq itself.

## Security Modes

### Encrypted (Default, Recommended)

```ini
security_mode=encrypted
psk_key=<64-character hex key>
```

All sync messages are encrypted with AES-256-GCM:
- 256-bit pre-shared key
- Per-message random 12-byte nonce
- 128-bit authentication tag
- Wire format: `[12-byte nonce][ciphertext][16-byte auth tag]`

Messages with invalid authentication tags are silently dropped. If keys don't match between nodes, no synchronization occurs — check logs for decryption warnings.

Generate a key (run once, copy to all nodes):
```bash
head -c 32 /dev/urandom | xxd -p -c 32
```

### Plain

```ini
security_mode=plain
plain_mode_acknowledged=1
```

For deployments where encryption is handled at the network layer (WireGuard, IPsec). The daemon **refuses to start** in plain mode without explicit `plain_mode_acknowledged=1` and logs a prominent warning on startup.

Use only when:
- Traffic is protected by WireGuard/IPsec tunnels
- Or the network is physically isolated (lab only)

## Deployment Recommendations

**Firewall:** Restrict the sync port (default 5378/UDP) to peer IP addresses only.
```bash
# Example: allow only from peer, drop everything else
iptables -A INPUT -p udp --dport 5378 -s <peer-ip> -j ACCEPT
iptables -A INPUT -p udp --dport 5378 -j DROP
```

**Network isolation:** Deploy sync traffic on a dedicated management VLAN when possible.

**NTP:** Clock synchronization across all nodes is required. Conflict resolution uses timestamps — skewed clocks cause incorrect lease precedence.

**Monitoring:** Watch for repeated decryption failures (`logread | grep lease-sync`), which may indicate a key mismatch or unauthorized peer.

## WireGuard Setup (for Plain Mode)

If using plain mode with WireGuard instead of PSK encryption:

1. **Install WireGuard:**
   ```bash
   opkg install wireguard-tools kmod-wireguard
   ```

2. **Generate keys on each node:**
   ```bash
   wg genkey | tee /etc/wireguard/private.key | wg pubkey > /etc/wireguard/public.key
   chmod 600 /etc/wireguard/private.key
   ```

3. **Configure interface** (`/etc/config/network`):
   ```uci
   config interface 'ha_backend'
       option proto 'wireguard'
       option private_key '<PRIVATE_KEY>'
       list addresses '172.20.0.10/24'
       option listen_port '51820'

   config wireguard_ha_backend
       option public_key '<PEER_PUBLIC_KEY>'
       option endpoint_host '<PEER_PHYSICAL_IP>'
       option endpoint_port '51820'
       option persistent_keepalive '25'
       list allowed_ips '172.20.0.11/32'
   ```

4. **Allow WireGuard in firewall:**
   ```bash
   uci add firewall rule
   uci set firewall.@rule[-1].name='Allow-WireGuard'
   uci set firewall.@rule[-1].src='wan'
   uci set firewall.@rule[-1].dest_port='51820'
   uci set firewall.@rule[-1].proto='udp'
   uci set firewall.@rule[-1].target='ACCEPT'
   uci commit firewall
   ```

5. **Configure lease-sync** to use WireGuard addresses:
   ```ini
   peer=172.20.0.11
   security_mode=plain
   plain_mode_acknowledged=1
   ```

6. **Verify:** `wg show` should show a recent handshake. `ping 172.20.0.11` should succeed.

For detailed WireGuard documentation, see: https://openwrt.org/docs/guide-user/services/vpn/wireguard

## Known Limitations

| Issue | Risk | Mitigation |
|-------|------|------------|
| DHCP script builds JSON by string concatenation | Crafted DHCP hostname could break JSON structure | dhcp-script-ha.sh sanitizes untrusted strings (backslash/quote escaping); dnsmasq also sanitizes client-supplied fields; encrypted mode prevents external message injection |
| No UDP rate limiting | Flood of packets could exhaust CPU | Firewall rate limiting at OS level; encrypted mode drops unauthenticated packets early |
| Predictable hash function (djb2) for lease DB | Hash collision DoS degrades lookup performance | Attacker needs PSK to inject leases; 1024 buckets sufficient for typical deployments |
| Signal handler calls syslog | Potential deadlock on shutdown if signal arrives during logging | Low probability; init system restarts daemon cleanly |
| No automatic expiry cleanup in lease-sync memory | In-memory lease table grows if leases are never explicitly deleted | dnsmasq manages lease lifecycle; daemon restart clears state |
