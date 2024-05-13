struct vma {
    uint64 addr;
    unsigned long len;
    int prot;
    int flags;
    int fd;
    int valid;
};