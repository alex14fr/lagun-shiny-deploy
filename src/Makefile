CFLAGS=-O3 -Wall -std=c99 -D_GNU_SOURCE
LDFLAGS=-s
BINS=dropcap-inner dropcap-outer httpget relayttpd
all: $(BINS)
clean:
	rm $(BINS)
install: $(BINS)
	cp -a $(BINS) $(DESTDIR)/bin
.PHONY: all clean install
