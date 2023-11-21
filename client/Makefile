CFLAGS		= -Og -g -Wall
CPPFLAGS	= -I../include -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26
LDLIBS		= -lfuse -lsodium
SRC		    = cache.c client.c main.c fs.c
PROG		= client

all: $(PROG)

clean:
	rm -f $(PROG)

$(PROG): $(SRC)
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS)
