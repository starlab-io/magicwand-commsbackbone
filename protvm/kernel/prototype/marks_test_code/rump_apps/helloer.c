#include <stdio.h>
#include <unistd.h>

#define ICOUNT_MAX 20
int
main()
{
    int icount;

    icount = 0;

    while(icount < ICOUNT_MAX) {
        printf("Hello, Rumprun ... I'm feeling tired\n");
        sleep(2);
        printf("much better!\n");
        icount++;
    }
    return 0;
}
