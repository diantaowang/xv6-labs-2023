struct vma {
    uint64 addr;
    unsigned long len;
    int prot;
    int flags;
    struct file *f;
    int valid;
};