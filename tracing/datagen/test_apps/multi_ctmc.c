/*
 * multi_ctmc.c approximates a continuous time markov chain; 
 *      function sleep times are exponentially distributed, and there are no self-loops
 */

#include <unistd.h>
#include "random.h"
#include <stdio.h>

typedef int bool;
#define true 1
#define false 0

const static int SIZE = 4;
static double p0[] = {0, 0.4, 0.6, 0};
static double p1[] = {0, 0, 1.0, 0};
static double p2[] = {0.35, 0.3, 0, 0.35};

static double c0[SIZE];
static double c1[SIZE];
static double c2[SIZE];

int fun0();
int fun1();
int fun2();

int fun0() {
    usleep(expInt(100000));
    return choice(c0, SIZE);
}

int fun1() {
    usleep(expInt(800000));
    return choice(c1, SIZE);
}

int fun2() {
    usleep(expInt(300000));
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
        printf("Function %d\n", next);
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
