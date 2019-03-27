# Debugging mwcomms driver from pvm

***Very Important to have PVM set to ONE cpu in the xen config file or else GDB will not work properly.***

**On Dom0** Create pvm and copy relevant source files using the **mw_copy_to_pvm** script
```
$ $MWROOT/util/mw_copy_to_pvm
```
---
On the PVM side in the mwcomms directory, make sure the following lines are not commented out
```
ccflags-y += -DMYTRAP
ccflags-y += -DMYDEBUG
```
MYTRAP enables the DEBUG_EMIT_BREAKPOINT() macro
MYDEBUG enables the MWSOCKET_DEBUG_OPTIMIZE_OFF directive that turns off compiler optimizations on a per function basis, which is needed to step through your code.  This macro will need to be defined for any new functions that you may have added that you would like to step through using gdb


---
On the PVM Build the mwcomms driver:
```
$ cd $MWROOT/protvm/kernel/mwcomms
$ make clean all
```
---
**On Dom 0** Copy the binary and source files from the pvm using the **mw_copy_from_pvm** script.
This will copy over all relevant driver files from the pvm and the binary and source
files needed to step through your code using gdb and gdbsx
```
$ $MWROOT/util/mw_copy_from_pvm
```
---
**On Dom0** copy the **gdbinit_mwcomms** file into the mwcomms directory in the tmp folder and rename it .gdbinit
```
$ cp $MWROOT/test/test_artifacts/gdbinit_mwcomms /tmp/mwpvm/protvm/kernel/mwcomms/.gdbinit
```
make sure the line:
```
set substitute-path /home/alex/ins-production/magicwand-commsbackbone/protvm /tmp/mwpvm/protvm is set correctly
```

---
**On Dom0** run start script as sudo
```
$ $MWROOT/test/test_artifacts/start_vm
```
This starts the pvm in a paused state and waits for gdb to connect on port 2222

---

**On Dom 0** cd into the mwcomms directory in /tmp, run gdb and source the .gdbinit file if it was not automatically sourced by gdb
```
$ cd /tmp/mwpvm/protvm/kernel/mwcomms
$ gdb -tui -ex 'target remote localhost:2222'
(gdb) source .gdbinit 
```
On the PVM load the driver, and when the breakpoint is hit, run the following command
(gdb) addsym $rax
(gdb) continue

---

You'll see the load script output the following:
section .text.... 0xffffffffc018c000
section .bss.... 0xffffffffc019ff00
if you want to view global values while debugging, you will need to pause the pvm via xl pause and run the addsym command as such:
```
(gdb) addsym 0xffffffffc018c000 0xffffffffc019ff00
```