#include "kernel.h"

double kernel_weighted_square(double weight, double x) {
    return weight * x * x;
}
