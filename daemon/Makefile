# Makefile for lease-sync daemon
# DHCP Lease Synchronization Daemon for dnsmasq HA

# Binary name
TARGET = lease-sync

# Source files
SRCS = lease-sync.c \
       lease-db.c \
       peer-sync.c \
       ubus-handler.c \
       retry-queue.c \
       config.c \
       util.c \
       crypto.c \
       log.c

# Object files
OBJS = $(SRCS:.c=.o)

# Compiler and flags
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -std=gnu99
CFLAGS += -D_GNU_SOURCE
CFLAGS += -DSTATEDIR=\"$(STATEDIR)\"

# Include paths
INCLUDES = -I. -I/usr/local/include

# Libraries
LDFLAGS += -L/usr/local/lib
LIBS = -lubus -lubox -lssl -lcrypto

# Installation directories
PREFIX ?= /usr
BINDIR = $(PREFIX)/sbin
CONFDIR = /etc/lease-sync
STATEDIR = /var/lib/lease-sync

# ============================================
# Build Rules
# ============================================

.PHONY: all clean install uninstall analyze check

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Built $(TARGET)"

%.o: %.c common.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ============================================
# Installation
# ============================================

install: $(TARGET)
	@echo "Installing lease-sync daemon..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(CONFDIR)
	install -d $(DESTDIR)$(STATEDIR)
	@echo "Installation complete"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Create configuration file: $(CONFDIR)/config"
	@echo "  2. Start daemon: $(BINDIR)/lease-sync"
	@echo ""

# Uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	@echo "Uninstalled lease-sync daemon"

# ============================================
# Cleanup
# ============================================

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "Cleaned build artifacts"

# ============================================
# Development Helpers
# ============================================

# Format code to match dnsmasq upstream style (requires astyle)
format:
	astyle --options=../.astylerc *.c *.h

# Static analysis (optional - skips gracefully if cppcheck not installed)
analyze:
	@./scripts/static-analysis.sh

# Legacy alias for static analysis
check:
	@./scripts/static-analysis.sh || echo "Static analysis skipped or failed (optional)"

# Print build configuration
info:
	@echo "Build configuration:"
	@echo "  CC:       $(CC)"
	@echo "  CFLAGS:   $(CFLAGS)"
	@echo "  LDFLAGS:  $(LDFLAGS)"
	@echo "  LIBS:     $(LIBS)"
	@echo "  TARGET:   $(TARGET)"
	@echo "  PREFIX:   $(PREFIX)"
	@echo "  BINDIR:   $(BINDIR)"
	@echo "  CONFDIR:  $(CONFDIR)"
	@echo "  STATEDIR: $(STATEDIR)"

# Dependencies
lease-sync.o: lease-sync.c common.h log.h
lease-db.o: lease-db.c common.h
peer-sync.o: peer-sync.c common.h crypto.h
ubus-handler.o: ubus-handler.c common.h
retry-queue.o: retry-queue.c common.h retry-queue.h
config.o: config.c common.h crypto.h
util.o: util.c common.h
crypto.o: crypto.c crypto.h
log.o: log.c log.h

# ============================================
# Testing
# ============================================

.PHONY: test test-crypto test-retry-queue test-util test-config test-lease-db test-all

test: test-crypto test-retry-queue test-util test-config test-lease-db
	@echo ""
	@echo "All unit tests passed"

test-crypto:
	cd ../tests && ./run-crypto-tests.sh

test-retry-queue:
	cd ../tests && ./run-retry-queue-tests.sh

test-util:
	cd ../tests && ./run-util-tests.sh

test-config:
	cd ../tests && ./run-config-tests.sh

test-lease-db:
	cd ../tests && ./run-lease-db-tests.sh

# Run all tests including integration tests (if on OpenWrt)
test-all:
	cd ../tests && ./run-tests.sh --all
