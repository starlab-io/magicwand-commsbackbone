# comments work in makefiles, apparently

CC=gcc
CFLAGS=-I.
#LDFLAGS=-lm -std=c99
LDFLAGS=-lm
DEPS = random.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) 

all: single_static single_uniform single_exp multi_dtmc multi_ctmc multi_semi

single_static: single_static.o
	$(CC) $(CFLAGS) -o single_static single_static.o $(LDFLAGS)

single_uniform: single_uniform.o random.o
	$(CC) $(CFLAGS) -o single_uniform single_uniform.o random.o $(LDFLAGS)

single_exp: single_exp.o
	$(CC) $(CFLAGS) -o single_exp single_exp.o random.o $(LDFLAGS)

multi_dtmc: multi_dtmc.o
	$(CC) $(CFLAGS) -o multi_dtmc multi_dtmc.o random.o $(LDFLAGS)

multi_ctmc: multi_ctmc.o
	$(CC) $(CFLAGS) -o multi_ctmc multi_ctmc.o random.o $(LDFLAGS)

multi_semi: multi_semi.o
	$(CC) $(CFLAGS) -o multi_semi multi_semi.o random.o $(LDFLAGS)

clean:
	rm *o single_static single_uniform single_exp multi_dtmc multi_ctmc multi_semi
