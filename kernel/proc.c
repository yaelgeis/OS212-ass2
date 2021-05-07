#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

int nexttid = 1;
struct spinlock tid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
static void freethread(struct thread *t);
struct thread* allocthread(struct proc* p);


extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each thread's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.

// No need to acquire p->lock because thr program hasn't strted running yet
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    struct thread *t;
    for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      int t_idx= (int)(t - p->threads);
      int p_idx = (int)(p - proc);
      uint64 va = KSTACK(p_idx*NTHREAD + t_idx);
      kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    } 
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&tid_lock, "nexttid");
  initlock(&wait_lock, "wait_lock");
  init_bsems();

  // No need to acquire p->lock because it is the first process
  for(p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    struct thread* t; 
    // initialize each proc threads table
    for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
      initlock(&t->lock, "thread");
      int t_idx = (int)(t - p->threads);
      int p_idx = (int)(p - proc);
      t->kstack = KSTACK(p_idx*NTHREAD + t_idx);
    }
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  struct thread *t = mythread();
  if (t == 0) 
    return 0;
  return t->parent;
}


// A2T3: Return the current struct thread *, or zero if none.
struct thread*
mythread(void) {
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}


int
alloctid() {
  int tid;
  
  acquire(&tid_lock);
  tid = nexttid;
  nexttid = nexttid + 1;
  release(&tid_lock);

  return tid;
}


// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found_proc;
    } else {
      release(&p->lock);
    }
  }
  return 0;

  found_proc: //p->lock is acquired

  p->pid = allocpid();
  p->state = USED;
  
  if((p->threads_trapframe = kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // if((p->threads_trapframe_backup = kalloc()) == 0){
  //   freeproc(p);
  //   release(&p->lock);
  //   return 0;
  // }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);

  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  p->freeze = 0;
  p->pending_signals = 0;
  p->signal_mask = 0;


  // Default handler for all signals
  for (int i = 0; i<sizeof(p->signal_handlers)/sizeof(void*); i++){
    p->signal_handlers[i] = (void*)SIG_DFL; 
    p->handlers_sigmasks[i] = 0;
  }
  //**** end of A2T2.1 ****//
  return p;
}


// Look in the threads table of the given proc for an UNUSED threads.
// If found, initialize state required to run in the kernel,
// and return with t->lock held.
// If there are no free threads, or a memory allocation fails, return 0.
struct thread*
allocthread(struct proc* p)
{ 
  struct thread* t; 
  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    acquire(&t->lock);
    if(t->state == T_UNUSED) {
      goto found_thread;
    } else {
      release(&t->lock);
    }
  }
  return 0;

  found_thread: // t->lock is acquired

  t->trapframe = p->threads_trapframe + (sizeof(struct trapframe) * (t - p->threads));
  // p->threads_trapframe_backup = p->threads_trapframe_backup + (sizeof(struct trapframe) * (t - p->threads));

  t->parent = p;
  t->tid = alloctid();
  t->state = T_USED;
  t->signal_handling = 0;


  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;
  t->context.sp = t->kstack + PGSIZE;

  //**** A2T2.1 ****//
  if((t->user_trapframe_backup = (struct trapframe *)kalloc()) == 0){
    freethread(t);
    release(&t->lock);
    return 0;
  }

  t->backup_address = t->user_trapframe_backup;

  return t;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  struct thread* t; 
  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    acquire(&t->lock);
    freethread(t);
    release(&t->lock);
  }

  if(p->threads_trapframe)
    kfree((void*)p->threads_trapframe);
  p->threads_trapframe = 0;


  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// free a thread structure and the data hanging from it,
// including user pages.
// t->lock must be held.
static void
freethread(struct thread *t)
{
  if(t->backup_address)
    kfree((void*)t->backup_address);
  t->user_trapframe_backup = 0;
  t->backup_address = 0;
  t->parent = 0;
  t->trapframe = 0;
  t->chan = 0;
  t->killed = 0;
  t->xstate = 0;
  t->signal_handling = 0;
  t->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->threads_trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p = allocproc();
  struct thread* t = allocthread(p);
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  t->trapframe->epc = 0;       // user program counter
  t->trapframe->sp = PGSIZE ;  // user stack pointer
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  t->state = T_RUNNABLE;
  release(&t->lock);

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.

// Need to acquire p->lock because p->pagetable is a shared resource of
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();
  acquire(&p->lock);
  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      release(&p->lock);
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  release(&p->lock);
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  struct thread *t = mythread();
  struct thread *nt;

  if ((nt = allocthread(np)) == 0){
    freeproc(np);
    release(&np->lock);
    return 0;
  }

  // copy saved user registers.
  *(nt->trapframe) = *(t->trapframe);

  // Cause fork to return 0 in the child.
  nt->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++){
    if(p->ofile[i]){
      np->ofile[i] = filedup(p->ofile[i]);
    }
  }
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  //**** A2T2.1 ****//
  np->signal_mask = p->signal_mask;
  for (int i = 0; i<sizeof(p->signal_handlers)/sizeof(void*); i++){
    np->signal_handlers[i] = p->signal_handlers[i];
    np->handlers_sigmasks[i] = p->handlers_sigmasks[i];
  }
   //**** end of A2T2.1 ****//

  nt->state = T_RUNNABLE;
  release(&nt->lock);

  release(&np->lock);
  

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}


// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  struct thread* currthread = mythread();

  struct thread* t;
  for(t = p->threads; t < &p->threads[NTHREAD]; t++){
    if (t != currthread){
      acquire(&t->lock);
      if (t->state != T_UNUSED && t->state != T_ZOMBIE){
        t->killed = 1;
        if (t->state == T_SLEEPING)
          t->state = T_RUNNABLE;
      }
      release(&t->lock);
    }
  }

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  wakeup(currthread);

  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  release(&p->lock);

  acquire(&currthread->lock);
  currthread->xstate = status;
  currthread->state = T_ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU thread scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a thread to run.
//  - swtch to start running that thread.
//  - eventually that thread transfers control
//    via swtch back to the scheduler.

// No need to acquire p->lock because we want to allow running few threads of the same process
void
scheduler(void)
{
  struct proc *p;
  struct thread *t;
  struct cpu *c = mycpu();
  c->thread = 0;
  
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      if(p->state == USED) {
        for(t = p->threads; t < &p->threads[NTHREAD]; t++){
          acquire(&t->lock);
          if (t->state == T_RUNNABLE){
            // Switch to chosen thread.  It is the thread's job
            // to release its lock and then reacquire it
            // before jumping back to us.
            t->state = T_RUNNING;
            c->thread = t;
            swtch(&c->context, &t->context);

            // thread is done running for now.
            // It should have changed its t->state before coming back.
            c->thread = 0;
          }
          release(&t->lock);
        }

      }
    }
  }
}

// Switch to scheduler.  Must hold only t->lock
// and have changed thread->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be thread->intena and thread->noff, but that would
// break in the few places where a lock is held but
// there's no thread.
void
sched(void)
{
  int intena;
  struct thread *t = mythread();

  if(!holding(&t->lock))
    panic("sched t->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(t->state == T_RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct thread* t = mythread();
  acquire(&t->lock);
  t->state = T_RUNNABLE;
  sched();
  release(&t->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding t->lock from scheduler.
  release(&mythread()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct thread *t = mythread();
  
  // Must acquire t->lock in order to
  // change t->state and then call sched.
  // Once we hold t->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks t->lock),
  // so it's okay to release lk.
  acquire(&t->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  t->chan = chan;
  t->state = T_SLEEPING;

  sched();
  
  // if(t->killed){
  //   release(&t->lock);
  //   kthread_exit(0);
  // }
  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&t->lock);
  acquire(lk);
}

// Wake up all threads sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;
  struct thread *t;
  for(p = proc; p < &proc[NPROC]; p++) {
    for(t = p->threads; t< &p->threads[NTHREAD]; t++){
      acquire(&t->lock);
      if(t->state == T_SLEEPING && t->chan == chan) 
        t->state = T_RUNNABLE;
      release(&t->lock);
    }
  }
}

// Need to acquire p->lock because we are modifying proc fields that multiple threads might access the same time
int
kill(int pid, int signum)
{

  if (signum < 0 || signum > 31)
    return -1;

  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      int should_kill = 0;
      p->pending_signals |= 1<<signum;
      if ((p->signal_mask & 1<<signum) != 0){ 
        if ((p->signal_handlers[signum] == (void*)SIGKILL) ||
            // If the signal handler is default and signum != sigcont, sigkill, then the signal handler should be sigkill
            (p->signal_handlers[signum] == (void*)SIG_DFL && signum != SIGCONT && signum != SIGSTOP)){ 
          should_kill = 1;
        }
      }
      // If the singal is sigkill or the signal handler is sigkill, wake up the process
      if (signum == SIGKILL || should_kill){
        struct thread *t;
        for (t = p->threads; t < &p->threads[NTHREAD]; t++){
          acquire(&t->lock);
          if (t->state == T_SLEEPING){
            t->state = T_RUNNABLE;
            release(&t->lock);
            break;
          }
          release(&t->lock);
        }
        release(&p->lock);
        return 0;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

//**** A2T2 ****// 
uint 
sigprocmask (uint sigmask){
  struct proc* p = myproc();
  acquire(&p->lock);
  int old_mask = p->signal_mask;

  p->signal_mask = sigmask;
  // if (copyin(p->pagetable, (char*)&p->signal_mask, sigmask, sizeof(uint)) < 0){
  //   release(&p->lock);
  //   return -1;
  // }
  release(&p->lock);
  return old_mask;
}

int
sigaction(int signum, uint64 act,  uint64 oldact)
{
  struct proc* p = myproc();
  int sigmask;
  void* sa_handler;
  copyin(p->pagetable, (char*)&sa_handler, act, sizeof(void*));
  copyin(p->pagetable, (char*)&sigmask, act + sizeof(void*), sizeof(int));
  if (act == 0 ||
      // Any positive value is valid for sigmask
      (sigmask < 0) ||  
      // check if signum is valid
      (signum < 0 || signum > 31) ||
      // SIGKILL and SIGSTOP cannot be modified, blocked, or ignored
      signum == SIGKILL || 
      signum == SIGSTOP || 
      ((sigmask & (1 << SIGKILL)) != 0) || 
      ((sigmask & (1 << SIGSTOP)) != 0) )
    return -1;

  acquire(&p->lock);

  if (oldact != 0){
    if ((copyout(p->pagetable, oldact, (char*)&p->signal_handlers[signum], sizeof(void*) < 0)) ||
        (copyout(p->pagetable, oldact+sizeof(void*), (char*)&p->handlers_sigmasks[signum], sizeof(uint) < 0))) {
          release(&p->lock);
          return -1;
    }
  }
  p->signal_handlers[signum] = sa_handler;
  p->handlers_sigmasks[signum] = sigmask;

  release(&p->lock);
  return 0;
}

void
sigret(void)
{
  struct proc *p = myproc();
  struct thread *t = mythread();
  if ((copyin(p->pagetable,(char*)t->trapframe, (uint64)t->user_trapframe_backup, sizeof (struct trapframe)))<0)
    panic("cannot restore trapframe");
  p->signal_mask = p->sigmask_backup;
  t->signal_handling = 0;
}
//**** end of A2T2.1 ****//


//**** A2T2.4 ****//

void handle_sigstop(struct proc* p, int signum){
  // Setting back to currentmask
  p->sigmask_backup = p->signal_mask;
  // Seting mask to be the appropriate sigmask
  p->signal_mask = p->handlers_sigmasks[signum];
  p->freeze = 1;
  p->signal_mask = p->sigmask_backup;
  yield();
}

void handle_sigkill(struct proc* p, int signum){
  // Setting back to currentmask
  p->sigmask_backup = p->signal_mask;
  // Seting mask to be the appropriate sigmask
  p->signal_mask = p->handlers_sigmasks[signum];
  p->killed = 1;
  p->pending_signals &= ~(1 << signum); // turn signal off
  p->signal_mask = p->sigmask_backup;
}

void handle_sigcont(struct proc* p, int signum){
  // Setting back to currentmask
  p->sigmask_backup = p->signal_mask;
  // Seting mask to be the appropriate sigmask
  p->signal_mask = p->handlers_sigmasks[signum];
  p->freeze = 0;
  p->pending_signals &= ~(1 << signum); // turn signal off
  p->pending_signals &= ~(1 << SIGSTOP); // turn signal off
  p->signal_mask = p->sigmask_backup;
}

void handle_sigignore(struct proc* p, int signum){
  // Setting back to currentmask
  p->sigmask_backup = p->signal_mask;
  // Seting mask to be the appropriate sigmask
  p->signal_mask = p->handlers_sigmasks[signum];
  p->pending_signals &= ~(1 << signum); // turn signal off
  p->signal_mask = p->sigmask_backup;
}

void user_handler(struct proc* p, struct thread* t, int signum){
  t->signal_handling = 1;
  p->sigmask_backup = p->signal_mask;
  p->signal_mask = p->handlers_sigmasks[signum];
  p->pending_signals &= ~(1 << signum); // turn signal bit off

  //save trap frame:
  // make space for trapframe
  t->trapframe->sp -= sizeof(struct trapframe); 
  // back up registers to stack
  copyout(p->pagetable, t->trapframe->sp, (char *)t->trapframe, sizeof(struct trapframe)); 
  //make user_trapframe_backup to point the location of the old trapframe in the stack  
  t->user_trapframe_backup = (struct trapframe*)t->trapframe->sp;

  // make space for the injected code
  uint labelSize = sigret_end - sigret_start;
  t->trapframe->sp -= labelSize;
  //push the code
  copyout(p->pagetable, t->trapframe->sp, (char *)sigret_start, labelSize); 
  
  // Store arguments values at dedicated registers
  t->trapframe->a0 = signum;
  t->trapframe->ra = t->trapframe->sp;
  t->trapframe->epc = (uint64)p->signal_handlers[signum];
}

void handle_signals(void){
  struct proc* p = myproc();
  struct thread* t = mythread();
  acquire(&t->lock);
  if (t->signal_handling){
    release(&t->lock);
    return;
  }
  release(&t->lock);

  for (int i = 0; i<sizeof(p->signal_handlers)/sizeof(void*); i = i+1){
    // if signal number i is not turned on
    if((p->pending_signals & (1 << i)) == 0)
      continue;

    //SIGSTOP, SIGKILL shouldn't be ignored
    if (i == SIGKILL){
      handle_sigkill(p, i);
    }
    if (i == SIGSTOP){
      handle_sigstop(p, i);
    }

    // check if the signal should be ignored
    if((p->signal_mask & (1 << i)) != 0){ 
      continue;
    }

    // if the handler of signal i is SIG_DFL
    // (if the signal is sigkill or sigstop, it was already handled)
    if(p->signal_handlers[i] == (void *)SIG_DFL){
      if(i == SIGCONT)
        handle_sigcont(p, i);  
      else
        handle_sigkill(p, i);
    }

    // if the handler of signal i is SIGKILL
    else if(p->signal_handlers[i] == (void *)SIGKILL){
      handle_sigkill(p, i);
    }

    // if the handler of signal i is SIGSTOP
    else if(p->signal_handlers[i] == (void *)SIGSTOP){
      handle_sigstop(p, i);
    }

   // if the handler of signal i is SIGCONT
    else if(p->signal_handlers[i] == (void *)SIGCONT){
      handle_sigcont(p, i);
    }

    // if the handler of signal i is SIG_IGN
    else if(p->signal_handlers[i] == (void *)SIG_IGN){
      handle_sigignore(p, i);
    }

    // the signal handler was set by the user
    else{
      user_handler(p, t, i);
    }
  }
}
//**** A2T2.4 ****//


//**** A2T3 ****//
int
kthread_create (uint64 start_func, uint64 stack){
  if (start_func == 0 || stack == 0)
    return -1;
  
  struct thread* t = allocthread(myproc());

  if (t == 0)
    return -1;
  
  struct thread* currthread = mythread();

  *(t->trapframe) = *(currthread->trapframe);
  t->trapframe->sp = stack + MAX_STACK_SIZE - 16; 
  t->trapframe->epc = start_func;
  t->state = T_RUNNABLE;
  int tid = t->tid;
  release(&t->lock);

  return tid;
}

int
kthread_id(void){
  struct thread* t = mythread();
  if (t == 0)
    return -1;
  return t->tid;

}

int count_running_threads(struct proc *p, struct thread* currthread){
  struct thread *t;
  int count = 0;
  for (t = p->threads; t < &p->threads[NTHREAD]; t++) {
    if(t != currthread){
      acquire(&t->lock);
      // check if exists a thread in state runnable / running / sleeping
      if (t->state != T_UNUSED && t->state != T_ZOMBIE) {
        count++;
      }
      release(&t->lock);
    }
  }
  return count;
}


void
kthread_exit(int status){

  struct thread *currthread = mythread();  
  struct proc *p = myproc();

  int count = count_running_threads(p, currthread);

  if (count == 0 ) //this was the last thread running
    exit(status);
  
  else{
    acquire(&currthread->lock);
    currthread->state = T_ZOMBIE;
    currthread->xstate = status;
    release(&currthread->lock);
    
    wakeup(currthread);

    acquire(&currthread->lock);
    sched();
    release(&currthread->lock);
  }
}

 

int
kthread_join(int thread_id, uint64 status){
  struct proc* p = myproc();
  struct thread* currthread = mythread();

  struct thread* t;
  for (t = p->threads; t < &p->threads[NTHREAD]; t++) {
    if (t != currthread){
      acquire(&t->lock);
      if (t->tid == thread_id && t->state != T_UNUSED)
          goto found_tid;
      else
        release(&t->lock);
    }
  }
  // hasn't found thread
  return -1; 

  //t is the thread we are waiting for, t->lock is still acquired
  found_tid:
  while (t->state != T_ZOMBIE) 
    sleep(t, &t->lock);
  
  // after the thread became zombie we clean it (the thread the we have been waiting for)
  if(status != 0 && copyout(p->pagetable, status, (char *)&t->xstate, sizeof(t->xstate)) < 0) {
    release(&t->lock);
    return -1;
  }
  freethread(t); 
  release(&t->lock);
  return 0;
}
//**** end of A2T3 ****//