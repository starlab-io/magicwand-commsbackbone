# Do this.

> gcc test_server.c -o test_server
> gcc -Wall -fPIC -DPIC -c wrap_test_server.c
> ld -shared -o wrap_test_server.so wrap_test_server.o
> sudo LD_PRELOAD=$PWD/wrap_test_server.so ./test_server
