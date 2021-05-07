#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "bsem.h"

struct bsem bsems[MAX_BSEM];

void init_bsems(){
	struct bsem *b;
	for (b = bsems; b < &bsems[MAX_BSEM]; b++){
		initlock(&b->lock, "bsem");
	}
}


int
bsem_alloc(){
	int d = 0;
	struct bsem *b;
	for (b = bsems; b < &bsems[MAX_BSEM]; b++){
		if (b->state == B_UNUSED){
			b->state = B_UNLOCKED;
			return d;
		}
		d++;
	}
	return -1;
}

void
bsem_free(int d){
	if (d < 0 || d >= MAX_BSEM)
		return;
	struct bsem *b = &bsems[d];
	acquire(&b->lock);
	if (b->state != B_UNUSED)
		b->state = B_UNUSED;
	release(&b->lock);
}

void
bsem_down(int d){
	if (d < 0 || d >= MAX_BSEM)
		return;
	struct bsem *b = &bsems[d];
	acquire(&b->lock);
	if (b->state == B_UNUSED){
		release(&b->lock);
		return;
	}
	while (b->state == B_LOCKED){
		sleep(b, &b->lock);
	}
	b->state = B_LOCKED;
	release(&b->lock);
}

void
bsem_up(int d){
  if (d < 0 || d >= MAX_BSEM)
		return;
	struct bsem *b = &bsems[d];
	acquire(&b->lock);
	if (b->state == B_LOCKED){
		b->state = B_UNLOCKED;
		wakeup(b);
	}
	release(&b->lock);
}

