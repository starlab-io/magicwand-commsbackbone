##
## NOTICE: If you compiled rump from a git branch, the rump binaries
## were put into a directory whose name is branch-dependent. Adjust
## your PATH accordingly.
##

CC=x86_64-rumprun-netbsd-gcc 
CFLAGS=-ggdb -I../platform/xen/xen/include

#BAKE=/bin/bash -v rumprun-bake
BAKE=rumprun-bake
#BAKE_FLAGS=xen-pv
BAKE_FLAGS=-c ../xenevents.conf xen_events

APPNAME=listdir

ARCHIVES=

all: $(APPNAME).run


$(APPNAME).run: $(APPNAME).bin
	$(BAKE) $(BAKE_FLAGS) $@ $^ $(ARCHIVES)
	@echo "Run    rumprun -S xen -di $@"
	@echo " or    rumprun -S xen -dip -D 1234 $@"

$(APPNAME).bin: $(APPNAME).c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(APPNAME).run $(APPNAME).bin

# To run
# sudo $(which rumprun) xen -i listdir.run
