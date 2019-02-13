CC=x86_64-rumprun-netbsd-gcc

BAKE=rumprun-bake
BAKE_FLAGS=-c ../npf_test.conf xen_npf

MAKE=make

LIBDIR=../lib/npfctl
INCDIR=$(LIBDIR)

BIN=test
SRC=test.c
LIB=libnpfctl

LDFLAGS= -L$(LIBDIR) -lnpfctl -lutil -lnpf -lprop -lpcap

all: $(BIN).bin
	$(BAKE) $(BAKE_FLAGS) $(BIN) $(BIN).bin $(ARCHIVES)
	genisoimage -l -r -o ./images/data.iso ./images/data

$(BIN).bin:$(SRC) $(LIB)
	$(CC) -o $(BIN).bin -I$(INCDIR) $(SRC) -L$(LIBDIR) -lnpfctl $(LDFLAGS)

$(LIB):
	$(MAKE) -C $(LIBDIR) all

clean:
	rm -f *.o $(BIN) $(BIN).bin images/data.iso
	$(MAKE) -C $(LIBDIR) clean
