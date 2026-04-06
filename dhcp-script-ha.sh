#!/bin/sh
# DHCP Lease Event Publisher for High Availability
# This script is called by dnsmasq for every DHCP lease event
# It publishes events to ubus for consumption by the lease-sync daemon
#
# Usage: Configure in dnsmasq.conf:
#   dhcp-script=/usr/lib/dnsmasq/dhcp-script-ha.sh
#
# Copyright (c) 2025 - Licensed under GPL v2 or later

# Enable strict error handling
set -e

# Configuration
UBUS_EVENT="dhcp.lease"
LOG_TAG="dhcp-script-ha"
NODE_ID_FILE="/etc/lease-sync/node-id"
DEBUG="${DEBUG:-0}"

# Action from dnsmasq: add, old, del, init, tftp, arp-add, arp-del
ACTION="$1"
MAC="$2"
IP="$3"
HOSTNAME="$4"

# Logging function
log() {
    local level="$1"
    shift
    logger -t "$LOG_TAG" -p "daemon.$level" "$@"
    [ "$DEBUG" = "1" ] && echo "[$level] $@" >&2
}

# Get or generate node ID
get_node_id() {
    if [ -f "$NODE_ID_FILE" ]; then
        cat "$NODE_ID_FILE"
    else
        # Generate from primary interface MAC if file doesn't exist
        # This will be created properly by lease-sync daemon init script
        cat /sys/class/net/br-lan/address 2>/dev/null || echo "unknown"
    fi
}

# Sanitize a string for safe JSON embedding.
# Escapes backslash and double-quote characters to prevent injection.
sanitize_json() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

# Build JSON payload for ubus event
build_lease_json() {
    local json='{'

    # Required fields (ACTION, IP, MAC are validated by dnsmasq;
    # HOSTNAME is untrusted DHCP client input — must sanitize)
    json="${json}\"action\":\"$ACTION\""
    [ -n "$IP" ] && json="${json},\"ip\":\"$IP\""
    [ -n "$MAC" ] && json="${json},\"mac\":\"$MAC\""
    [ -n "$HOSTNAME" ] && json="${json},\"hostname\":\"$(sanitize_json "$HOSTNAME")\""

    # Node identification
    json="${json},\"node_id\":\"$(sanitize_json "$(get_node_id)")\""
    json="${json},\"timestamp\":$(date +%s)"

    # Lease timing (from environment variables)
    [ -n "$DNSMASQ_LEASE_EXPIRES" ] && json="${json},\"expires\":$DNSMASQ_LEASE_EXPIRES"
    [ -n "$DNSMASQ_LEASE_LENGTH" ] && json="${json},\"lease_length\":$DNSMASQ_LEASE_LENGTH"

    # Client identification
    [ -n "$DNSMASQ_CLIENT_ID" ] && json="${json},\"client_id\":\"$DNSMASQ_CLIENT_ID\""
    [ -n "$DNSMASQ_INTERFACE" ] && json="${json},\"interface\":\"$DNSMASQ_INTERFACE\""

    # IPv6 specific fields
    if [ -n "$DNSMASQ_IAID" ]; then
        local iaid_num
        iaid_num=$(echo "$DNSMASQ_IAID" | sed 's/^T//')
        json="${json},\"iaid\":$iaid_num"
        # Check if temporary address (IAID starts with "T")
        if echo "$DNSMASQ_IAID" | grep -q "^T"; then
            json="${json},\"is_temporary\":1"
        else
            json="${json},\"is_temporary\":0"
        fi
    fi

    json="${json}}"
    echo "$json"
}

# Publish event to ubus
publish_event() {
    local payload="$1"

    if [ "$DEBUG" = "1" ]; then
        log debug "Publishing to ubus: $payload"
    fi

    # Send to ubus
    # Using 'ubus send' broadcasts the event to all subscribers
    if ubus send "$UBUS_EVENT" "$payload" 2>/dev/null; then
        log info "Published $ACTION event for $IP to ubus"
        return 0
    else
        log err "Failed to publish event to ubus: $ACTION $IP"
        return 1
    fi
}

# Main event handling
case "$ACTION" in
    add|old|del)
        # Normal DHCP lease events
        if [ -z "$IP" ]; then
            log warning "Received $ACTION event without IP address, ignoring"
            exit 0
        fi

        payload=$(build_lease_json)

        # Publish to ubus for lease-sync daemon
        if ! publish_event "$payload"; then
            # Log failure but don't block dnsmasq
            log err "Event publication failed but continuing"
        fi
        ;;

    init)
        # Called at dnsmasq startup with leasefile-ro option
        # The script should output existing leases to stdout
        # For HA setup, this is handled by lease-sync daemon
        # We just log that init was called
        log info "Init called - lease-sync daemon should handle lease restoration"
        exit 0
        ;;

    tftp)
        # TFTP transfer completed
        # We don't need to sync TFTP events for DHCP HA
        log debug "TFTP event ignored: $MAC $IP $HOSTNAME"
        exit 0
        ;;

    arp-add|arp-del)
        # ARP table changes (only if --script-arp enabled)
        # These could be useful for detecting rogue DHCP servers
        log debug "ARP event: $ACTION $MAC $IP"
        # Optional: could publish these for monitoring
        exit 0
        ;;

    *)
        log warning "Unknown action: $ACTION"
        exit 0
        ;;
esac

exit 0
