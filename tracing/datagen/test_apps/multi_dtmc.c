/*
 * multi_dtmc.c approximates a discrete-time markov chain with functions
 *      function sleep times are constant
 */

#include <unistd.h>
//#include <stdio.h>
#include "random.h"

typedef int bool;
#define true 1
#define false 0

#define SIZE 4
//const static int SIZE = 4;
static double p0[] = {0.2, 0.3, 0.5, 0};
static double p1[] = {0, 0.8, 0.2, 0};
static double p2[] = {0.3, 0.2, 0.25, 0.25};

static double c0[SIZE];
static double c1[SIZE];
static double c2[SIZE];

int fun0();
int fun1();
int fun2();

int fun0() {
    usleep(5000);
    return choice(c0, SIZE);
}

int fun1() {
    usleep(10000);
    return choice(c1, SIZE);
}

int fun2() {
    usleep(15000);
    return choice(c2, SIZE);
}

int main() {
    init();
    pdfToCdf(p0, c0, SIZE);
    pdfToCdf(p1, c1, SIZE);
    pdfToCdf(p2, c2, SIZE);

    int next = 0;
    bool condition = true;
    while (condition) {
        //printf("Function %d\n", next);
        switch (next) {
            case 0:
                next = fun0();
                break;
            case 1:
                next = fun1();
                break;
            case 2:
                next = fun2();
                break;
            default:
                condition = false;
        }
    }
    return 0;
}
