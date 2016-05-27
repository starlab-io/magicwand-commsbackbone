/*
 * random.c is used to generate random numbers with different distributions
 *      it is primarily meant to produce markov-like output in functions
 */
#include <stdlib.h>
#include <time.h>
#include <math.h>

// This should be called ONCE before running any other function

void init() {
    /* Intializes random number generator */
    time_t t;
    srand((unsigned) time(&t));
    rand(); // pull off the time seed
}

// Random Variables

double uniform() {
    // return a double uniformly distributed in [0.0, 1.0)
    return ((double) rand())/(((double) RAND_MAX) + 1);
}

double uniformDouble(double min, double max) {
    // return a double uniformly distributed in [min, max)
    return (uniform() * (max - min)) + min;
}

int uniformInt(int min, int max) {
    // return an int uniformly distributed in [min, max)
    return (int) (uniform() * (max - min)) + min;
}

double expDouble(double mean) {
    // mean = 1/lambda
    return mean * -log(uniform());
}

int expInt(double mean) {
    // quantize the result to an int 
    return (int) (mean * -log(uniform()));
}

// PDFs and CDFs

int choice(double cdf[], int size) {
    // return the index of the randomly chosen element
    if (size <= 0) {
        return -1; // indicate failure by -1
    }

    double d = uniform();
    for (int i = 0; i < size - 1; i++) {
        if (d < cdf[i]) { // linear search is fast for small n
            return i;
        }
    }
    return size - 1;
}

void pdfToCdf(double pdf[], double cdf[], int size) {
    if (size > 0) {
        cdf[0] = pdf[0];
        for (int i = 1; i < size - 1; i++) {
            cdf[i] = cdf[i - 1] + pdf[i];
        }
        cdf[size - 1] = 1.0; // ensure the last entry is 1.0
    }
}
