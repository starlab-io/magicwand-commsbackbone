/*
 * single_static.c runs a single track execution with set times 
 */

#include <unistd.h>

void fun1();
void fun2();

void fun1() {
    usleep(10000);
    return;
}

void fun2() {
    usleep(20000);
    return;
}

int main() {
    fun1();
    fun2();
    return 0;
}