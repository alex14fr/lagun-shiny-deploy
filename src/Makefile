CFLAGS=-O3 -Wall -std=c99 -D_GNU_SOURCE
LDFLAGS=-s
ifdef SSL
CFLAGS+=-DTLS -DINET6
LDLIBS=-lcrypto -lssl
endif
BINS=dropcap-simple dropcap-inner dropcap-outer httpget relayttpd cleanfiles
all: $(BINS)
clean:
	rm -f $(BINS)
install: $(BINS)
	cp -a $(BINS) $(DESTDIR)/bin
.PHONY: all clean install
