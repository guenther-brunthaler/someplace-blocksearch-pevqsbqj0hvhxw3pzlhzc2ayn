.POSIX:

TARGETS = bytesearch

all: $(TARGETS)

#CFLAGS = -D NDEBUG -O3 -mtune=native -pipe
CFLAGS = -D DEBUG -O0 -ggdb3 -pipe
LDFLAGS = -Wl,-O1,--as-needed,--hash-style=gnu

all: $(TARGETS)

clean:
	-rm $(TARGETS)
