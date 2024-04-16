#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    char buf;
    char appends[MAXARG][32];
    char *args[MAXARG];

    for (int i = 1; i < argc; ++i) {
        args[i - 1] = argv[i];
    }
    memset(args + argc - 1, 0, MAXARG - argc + 1); 
    
    int appends_cnt = 0;
    int arg_idx = 0;

    while(read(0, &buf, 1)) {
        if (buf == ' ' || buf == '\n') {
            if (arg_idx) {
                appends[appends_cnt][arg_idx] = 0;
                ++appends_cnt;
                arg_idx = 0;
            }
        } else {
            appends[appends_cnt][arg_idx++] = buf;
        }

        if (buf == '\n') {
            if (fork() == 0) {
                for (int i = 0, j = argc - 1; i < appends_cnt; ++i, ++j) {
                    args[j] = appends[i];
                }
                args[argc - 1 + appends_cnt] = 0;
                exec(args[0], args);
            }
            appends_cnt = 0;
            arg_idx = 0;
        }
    }

    wait((int*) 0);
    exit(0);
}