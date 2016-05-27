# comments work in makefiles, apparently

CC=gcc
CFLAGS=-I.
DEPS = random.h

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: main single_static single_uniform single_exp multi_dtmc multi_ctmc multi_semi

main: main.o random.o
	$(CC) $(CFLAGS) -o test main.o random.o

single_static: single_static.o
	$(CC) $(CFLAGS) -o single_static single_static.o

single_uniform: single_uniform.o
	$(CC) $(CFLAGS) -o single_uniform single_uniform.o random.o

single_exp: single_exp.o
	$(CC) $(CFLAGS) -o single_exp single_exp.o random.o

multi_dtmc: multi_dtmc.o
	$(CC) $(CFLAGS) -o multi_dtmc multi_dtmc.o random.o

multi_ctmc: multi_ctmc.o
	$(CC) $(CFLAGS) -o multi_ctmc multi_ctmc.o random.o

multi_semi: multi_semi.o
	$(CC) $(CFLAGS) -o multi_semi multi_semi.o random.o

clean:
	rm *o test single_static single_uniform single_exp multi_dtmc multi_ctmc multi_semi
