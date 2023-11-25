CFLAGS		= -Og -g -Wall
CPPFLAGS	= -I../include -D_FILE_OFFSET_BITS=64
LDLIBS		= -lsodium
SRC		= main.c mtree.c server.c
PROG		= server
DEPS		= $(PROG).d

all: $(PROG)

clean:
	rm -f $(PROG) $(DEPS)

-include $(DEPS)

$(PROG): $(SRC)
	$(CC) -o $@ -MMD -MF $(DEPS) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS)
