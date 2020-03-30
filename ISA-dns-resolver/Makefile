CC=gcc
CFLAGS=-Wall
HFILES = dns-resolver.h
CFILES = dns-resolver.c

dns: $(CFILES) $(HFILES)
	$(CC) -o $@ $(CFILES) $(CFLAGS)

clean:
	rm -f *.o *~
