#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Parameter Error\n");
        exit(0);
    }
    int sleep_time = atoi(argv[1]);
    if (sleep_time < 0) {
        sleep_time = 0;
    }
    sleep(sleep_time);
    exit(0);
}