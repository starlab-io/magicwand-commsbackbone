##
## NOTICE: If you compiled rump from a git branch, the rump binaries
## were put into a directory whose name is branch-dependent. Adjust
## your PATH accordingly.
##

CC = x86_64-rumprun-netbsd-gcc 
CFLAGS = -ggdb -std=gnu99 -Wall -Wno-missing-braces
#CFLAGS = -std=gnu99 -Wall -Wno-missing-braces
CFLAGS += -I..
CFLAGS += -I../../../../common
CFLAGS += -I../platform/xen/xen/include 

#CFLAGS+= -DMYDEBUG
CFLAGS+= -DMYTRAP
#CFLAGS+= -DNODEVICE

BAKE=rumprun-bake
BAKE_FLAGS=-c ../xenevents.conf xen_events

# For testing without /dev/xe
#BAKE_FLAGS=xen_pv

# For testing outside of Rump, use Makefile.util

APPNAME=xenevent

ARCHIVES=

all: $(APPNAME).run

# We can call rumprun-bake in the most annoying manner possible;
# but it shows exactly what the bake script is doing. Here's how:
# bash -x `which $(BAKE)` $(BAKE_FLAGS) $@ $^ $(ARCHIVES)	
#
# Direct running instrutions:
#@echo "Run    rumprun -S xen -i $@"
#@echo " rumprun -S xen -dip -D 1234 $@"
#
# N.B. Calling rumprun with -d works here, but it doesn't work with
# thread_app. Why?

$(APPNAME).run: $(APPNAME).bin
	#bash -x `which $(BAKE)` $(BAKE_FLAGS) $@ $^ $(ARCHIVES)
	$(BAKE) $(BAKE_FLAGS) $@ $^ $(ARCHIVES)
	@echo "Run   ./run.sh"
	@echo "Debug with:"
	@echo " rumprun -S xen -dip -D 1234 $@"
	####@echo " ./run.sh"
	@echo "and"
	@echo " gdb -ex 'target remote:1234' $@"

# threads.c
$(APPNAME).bin: $(APPNAME).c networking.c workqueue.c
	$(CC) $(CFLAGS) -o $@ $^


clean:
	rm -f $(APPNAME).run $(APPNAME).bin *.o
