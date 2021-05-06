struct stat;
struct rtcdate;
struct sigaction;               //A2T2.1
struct counting_semaphore;      //A2T4

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int, int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);

//**** A2T2****//
// proc.c
uint sigprocmask (uint); 
int sigaction (uint, const struct sigaction*, struct sigaction*); 
// int sigaction (uint, uint64 , uint64 ); 

void sigret (void); 

// proc.h
struct sigaction{
  void (*sa_handler)(int);
  uint sigmask;
};
//**** end of A2T2 ****//

//**** A2T3****//
int kthread_create (void (*start_func) () , void *stack);
int kthread_id(void);
void kthread_exit(int status);
int kthread_join(int thread_id, int* status);
//**** end of A2T3****//


//**** A2T4****//
int bsem_alloc (void);
void bsem_free (int);
void bsem_down (int);
void bsem_up (int);
//**** end of A2T4****//



// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);


//**** A2T4****//
//Csemaphore.c
int  csem_alloc (struct counting_semaphore*, int);
void csem_free (struct counting_semaphore*);
void csem_down (struct counting_semaphore*);
void csem_up (struct counting_semaphore*);
//**** end of A2T4****//
