CFLAGS		= -Og -g -Wall
CPPFLAGS	= -I../include -D_FILE_OFFSET_BITS=64
LDLIBS		= -lsodium
SRC		= main.c mtree.c server.c
PROG		= server

all: $(PROG)

clean:
	rm -f $(PROG)

$(PROG): $(SRC)
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS)
