#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAX_PATH_LEN 512

char *fmtname(char *path) {
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for(p=path+strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;

    // Return blank-padded name.
    if(strlen(p) >= DIRSIZ)
        return p;
    memmove(buf, p, strlen(p));
    memset(buf+strlen(p), 0, DIRSIZ+1-strlen(p));
    return buf;
}

void find(char *dir, char *target) {
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(dir, O_RDONLY)) < 0) {
        fprintf(2, "find: cannot open %s\n", dir);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", dir);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_DEVICE:
        case T_FILE:
            char *nm = fmtname(dir);
            if (!strcmp(nm, target)) {
                fprintf(1, "%s\n", dir);
            }
            break;
        case T_DIR:
            if (strlen(dir) + 1 + DIRSIZ + 1 > MAX_PATH_LEN) {
                fprintf(2, "find: path too long\n");
                break;
            }
            int len = strlen(dir);
            *(dir + len) = '/';
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {
                if (de.inum == 0 || !strcmp(de.name, ".") || !strcmp(de.name, "..")) {
                    continue;
                }
                memmove(dir + len + 1, de.name, strlen(de.name));
                *(dir + len + 1 + strlen(de.name)) = 0;
                find(dir, target);
            }
            *(dir + len) = 0;
            break;
    }
    close(fd);
} 

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Parameter Error\n");
        exit(1);
    }

    char dir[512], target[DIRSIZ + 1];
    
    memset(dir, 0, sizeof(dir));
    memset(target, 0, sizeof(target));

    memmove(dir, argv[1], strlen(argv[1]));
    memmove(target, argv[2], strlen(argv[2]));

    find(dir, target);

    exit(0);
}