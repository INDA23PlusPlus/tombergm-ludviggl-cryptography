PROGS		= client server
all-PROGS	= $(PROGS:%=all-%)
clean-PROGS	= $(PROGS:%=clean-%)

.PHONY: all clean $(all-PROGS) $(clean-PROGS)

all: $(all-PROGS)

clean: $(clean-PROGS)

$(all-PROGS):
	cd $(@:all-%=%) && make all

$(clean-PROGS):
	cd $(@:clean-%=%) && make clean
