#include "kernel/types.h"
#include "user/user.h"

void fork_pipe(int rpipe, int wpipe) {
    char buf[1];
    int p[2];
    int created = 0;
    int pid = fork();
    
    if (pid == 0) {
        pipe(p);
        close(wpipe);

        read(rpipe, buf, 1);
        int base_prime = (int) buf[0];
        printf("prime %d\n", base_prime);
        //printf("prime %d, %d\n", base_prime, getpid());
        
        while (read(rpipe, buf, 1)) {
            int v = (int) buf[0];
            int res = v % base_prime;
            if (res && !created) {
                created = 1;
                fork_pipe(p[0], p[1]);
                close(p[0]);
            }
            if (res) {
                write(p[1], buf, 1);
            }
        }
        close(p[1]);
        close(rpipe);
        wait((int *) 0);
        exit(0);
    } else {
        return;
    }
}

int main() {
    int p[2];
    pipe(p);
    printf("prime 2\n");
    
    fork_pipe(p[0], p[1]);
    close(p[0]);
    for (char i = 3; i <= 35; ++i) {
        if (i & 0x1) {
            write(p[1], &i, 1);
        }
    }
    close(p[1]);
    wait((int *) 0);
    exit(0);
}