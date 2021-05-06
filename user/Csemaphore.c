#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "Csemaphore.h"

int csem_alloc (struct counting_semaphore* cs, int val){
	if (cs == 0 || val < 0)
		return -1;
  int s1, s2;
	if( (s1 = bsem_alloc()) < 0 || (s2 = bsem_alloc()) < 0 )
		return -1;
	// s1, s2 are initialized to 1 at first (since their initial state is unlocked)
	if (val == 0)
		bsem_down(s2);
	cs->s1 = s1;
	cs->s2 = s2;
	cs->value = val;
	return 0;
}

void csem_free (struct counting_semaphore* cs){
	if (cs == 0) return;
	bsem_free(cs->s1);
	bsem_free(cs->s2);
}

void csem_down (struct counting_semaphore* cs){
	if (cs == 0) return;
	bsem_down(cs->s2);
	bsem_down(cs->s1);
	cs->value--;
	if (cs->value > 0)
		bsem_up(cs->s2);
	bsem_up(cs->s1);
}

void csem_up (struct counting_semaphore* cs){
	bsem_down(cs->s1);
	cs->value++;
	if(cs->value == 1)
		bsem_up(cs->s2);
	bsem_up(cs->s1);
}
