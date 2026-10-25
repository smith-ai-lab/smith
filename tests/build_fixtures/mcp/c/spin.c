#include <stdio.h>
#include <unistd.h>

int main(void) {
    FILE *file = fopen("spin.pid", "w");
    fprintf(file, "%d\n", (int)getpid());
    fclose(file);
    int stopped_here = 1;
    sleep(60);
    return stopped_here;
}
