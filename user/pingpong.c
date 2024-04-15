#include "kernel/types.h"
#include "user/user.h"

int main() {
    char buf[2];
    int pwr[2], prw[2];

    pipe(pwr);
    pipe(prw);
    if (fork() == 0) {
        close(pwr[1]);
        close(prw[0]);

        read(pwr[0], buf, 1);
        printf("%d: received ping\n", getpid());
        printf("child: %s\n", buf);
        write(prw[1], "Y", 1);

        close(pwr[0]);
        close(prw[1]);
    } else {
        close(pwr[0]);
        close(prw[1]);

        write(pwr[1], "X", 1);
        read(prw[0], buf, 1);
        printf("%d: received pong\n", getpid());
        printf("parent: %s\n", buf);

        close(pwr[1]);
        close(prw[0]);
    }
    exit(0);
}

/*int main() {
    char buf[2];
    int p[2];

    pipe(p);
    if (fork() == 0) {
        close(p[1]);
        read(p[0], buf, 1);
        printf("%s\n", buf);
    } else {
        close(p[0]);
        write(p[1], "X", 1);
    }
    exit(0);
}*/

/*int main() {
    char buf[2] = "Y";
    int p[2];

    pipe(p);
    close(p[0]);
    write(p[1], "M", 1);
    read(p[0], buf, 1);
    printf("%s\n", buf);
    exit(0);
}*/