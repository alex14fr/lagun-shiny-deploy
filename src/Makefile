CFLAGS=-O3 -Wall -std=c99 -D_GNU_SOURCE
LDFLAGS=-s
BINS=dropcap-simple dropcap-inner dropcap-outer httpget relayttpd cleanfiles relayttpdssl
all: $(BINS)
clean:
	rm -f $(BINS)
install: $(BINS)
	cp -a $(BINS) $(DESTDIR)/bin
relayttpdssl: relayttpd.c
	$(CC) -o $@ $(CFLAGS) -DINET6 -DTLS $(LDFLAGS) $< $(LDLIBS) -lcrypto -lssl
.PHONY: all clean install
