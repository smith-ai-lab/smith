#include "helper.h"

unsigned long checksum(const unsigned char *data, unsigned long size) {
    unsigned long sum = 0;
    for (unsigned long i = 0; i < size; i++)
        sum = sum * 31 + data[i];
    return sum;
}
