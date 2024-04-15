#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[]) {  
    int pid = fork();
    if (pid == 0) {
        write(1, "hello ", 6);
        exit(0);
    } else {
        wait((void*) 0);
        write(1, "world\n", 6);
        exit(0);
    }
}