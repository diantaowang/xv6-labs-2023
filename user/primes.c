#include "kernel/types.h"
#include "user/user.h"

/*void test(int rp) {
    char *buf = 0;
    read(rp, buf, 1);
    int base_prime = (int) buf[0];
    printf("prime %d\n", base_prime);
    
    int p[2];
    pipe(p);
    int found = 0;
    while (read(rp, buf, 1)) {
        int v = (int) buf[0];
        if (v % base_prime && !found) {

        } else if (v % base_prime) {

        }
    }
    close(p[0]);
    close(p[1]);
}*/

int main() {

    exit(0);
}