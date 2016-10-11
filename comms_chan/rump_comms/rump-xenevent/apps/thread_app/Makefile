##
## NOTICE: If you compiled rump from a git branch, the rump binaries
## were put into a directory whose name is branch-dependent. Adjust
## your PATH accordingly.
##

CC=x86_64-rumprun-netbsd-gcc 
CFLAGS=-ggdb -I..
CFLAGS += -DMYDEBUG

BAKE=rumprun-bake

# bash -x `which $(BAKE)` $(BAKE_FLAGS) $@ $^ $(ARCHIVES)	
#BAKE=/bin/bash -x `which rumprun-bake`

#BAKE_FLAGS=xen-pv
BAKE_FLAGS=xen_pv
APPNAME=thread
ARCHIVES=

all: $(APPNAME).run

## N.B. Something bad happens when specifying -d. Don't use it with pthreads.
## We can call rumprun-bake in the most annoying manner possible;
## but it shows exactly what the bake script is doing. Here's how:
## bash -x `which $(BAKE)` $(BAKE_FLAGS) $@ $^ $(ARCHIVES)	
$(APPNAME).run: $(APPNAME).bin
	$(BAKE) $(BAKE_FLAGS) $@ $^ $(ARCHIVES)
	@echo "Run    rumprun -S xen -i $@"
	@echo "Debug with:"
	@echo " rumprun -S xen -ip -D 1234 $@"
	@echo "and"
	@echo " gdb -ex 'target remote:1234' $@"

$(APPNAME).bin: $(APPNAME).c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(APPNAME).run $(APPNAME).bin

