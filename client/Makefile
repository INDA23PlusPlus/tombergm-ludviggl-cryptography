CFLAGS		= -Og -g -Wall
CPPFLAGS	= -I../include -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=26
LDLIBS		= -lfuse -lsodium
SRC		= cache.c client.c fs.c main.c
PROG		= client
DEPS		= $(PROG).d

all: $(PROG)

clean:
	rm -f $(PROG) $(DEPS)

-include $(DEPS)

$(PROG): $(SRC)
	$(CC) -o $@ -MMD -MF $(DEPS) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS)
