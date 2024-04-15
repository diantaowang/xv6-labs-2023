#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main() {
    char *argv[2];
    argv[0] = "cat";
    argv[1] = 0;
    int pid = fork();
    if (pid == 0) {
        close(0);
        open("input.txt", O_RDONLY);
        exec("cat", argv);
        exit(1);
    }
    exit(0);
}