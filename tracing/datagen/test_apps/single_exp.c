/*
 * single_exp.c runs a single track execution with exponential dist times
 */

#include <unistd.h>
#include "random.h"

void fun1();
void fun2();

void fun1() {
    usleep(uniformInt(0, 1000000));
    return;
}

void fun2() {
    usleep(uniformInt(0, 2000000));
    return;
}

int main() {
    init();
    fun1();
    fun2();
    return 0;
}
