enum bsem_state { B_UNUSED, B_LOCKED, B_UNLOCKED };

struct bsem{
    enum bsem_state state;
    struct spinlock lock;
};

// the bsem descriptor is its index in bsems array