#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

void
dummy(){
	return;
}

void
handler(int signum) {
  printf("- first user handler, handling signal %d \n", signum);
}

void
handler2(int signum ) {
	printf("- second user handler, handling signal %d \n", signum);
}

void
test_kill(){
	int pid;
	int xstate = 0;
	if ((pid = fork()) == 0){
		while(1){};
	}
	//parent
	if ((kill(pid, 32) == 0) ||
			(kill(-2, SIGKILL) == 0) ||
			(kill(pid, SIGKILL) != 0)){
		xstate = 1;
	}
	wait(0);
	exit(xstate);
}

void
test_sigstop_sigcont() {
	int pid = fork();
	int xstate = 0;
	if (pid==0){
		int i = 0; 
		while (i < 100)
			i++;
	}
	else {
		// allow child process enough time to get into infinte loop
		sleep(10); 
		if (kill(pid, SIGSTOP) < 0)
			xstate = 1;
		sleep(10);
		if (kill(pid, SIGCONT) < 0)
			xstate = 1;
	}
	exit(xstate);
}

void
test_sigaction() {
	//not for use - just to avoid address 0
	struct sigaction* x = malloc(sizeof(struct sigaction));
	x->sa_handler = dummy;
	x->sigmask = 0;

	int pid;
	struct sigaction* siguser_action = malloc(sizeof(struct sigaction));
	siguser_action->sa_handler = handler;
	siguser_action->sigmask = 25;

	fprintf(2, "* note that user handler 1 should be printed out!\n");
	
	int xstate = 0;
	pid = fork();
	if (pid==0) {
		sigaction(3, siguser_action, 0);
		sleep(50);
	}
	//parent
	else {
		sleep(10);
		if (kill(pid, 3) < 0)
			xstate = 1;
		sleep(100);
		exit(xstate);
	}
}

void
test_infinite_loop_and_kill(){
	int xstate = 0;
	int pid = fork();
	if(pid == 0){
		while(1){}; //should not sleep for that long
	}
	else{
		if (kill(pid, SIGKILL) < 0)
			xstate = 1;
	}
	wait(0);
	exit(xstate);
}

void
test_stop_cont_kill(){
	int xstate = 0;

	int pids[5];
	for(int i=0;i<5;i++){
		if((pids[i] = fork()) == 0){ 
			while(1){};
		}
	}	
	// parent
	// send sigstop to all children
	for(int i=0; i<5; i++){
		if (kill(pids[i],SIGSTOP) < 0)
			xstate = 1;
	}
  // send sigcont to all children
  for(int i=0; i<5; i++){
		if (kill(pids[i],SIGCONT) < 0)
			xstate = 1;
	}
  // send sigkill to all children
  for(int i=0; i<5; i++){
		if (kill(pids[i],SIGKILL) < 0)
			xstate = 1;
	}

	for(int i=0; i<5; i++)
		wait(0);

	exit(xstate);
}

void
test_cont_before_stop(){
	int xstate = 0;
	int pid = fork();
	//child 1
	if(pid == 0){
		while(1){};
	}
	//parent 
	for(int i=0; i<4; i++){
	if (kill(pid,SIGCONT) < 0)
		xstate = 1;
	if (kill(pid,SIGSTOP) < 0)
		xstate = 1;
	}
	if (kill(pid, SIGKILL) < 0)
		xstate = 1;;
	wait(0);
	exit(xstate);
}

void
test_custom_handler(){
	//not for use - just to avoid address 0
	struct sigaction* x = malloc(sizeof(struct sigaction));
	x->sa_handler = dummy;
	x->sigmask = 0;

	struct sigaction* act = malloc(sizeof(struct sigaction));
	act->sa_handler = handler;
	act->sigmask = 0;
	
	int xstate = 0;
	int pid1,pid2;
	//child1
	if((pid1 = fork()) == 0){
		sigaction(5, act, 0);
		while(1){};
	}
	//child2
	if((pid2 = fork()) == 0)
		while(1){};

	//parent
	fprintf(2, "* note that user handler 1 should be printed out!\n");
	if (kill(pid2,SIGKILL) < 0)
		xstate = 1;
	int status = 0;
	wait(&status);
	if (kill(pid1,5) <0)
		xstate = 1;
	sleep(100);
	if (kill(pid1,SIGKILL) < 0)
		xstate = 1;
	wait(0);
	exit(xstate );
}

void
test_two_custom_handlers(){
  //not for use - just to avoid address 0
	struct sigaction* x = malloc(sizeof(struct sigaction));
	x->sa_handler = dummy;
	x->sigmask = 0;

	struct sigaction* act1 = malloc(sizeof(struct sigaction));
	act1->sa_handler = handler;
	act1->sigmask = 0;

	struct sigaction* act2 = malloc(sizeof(struct sigaction));
	act2->sa_handler = handler2;
	act2->sigmask = 0;
	
	int xstate = 0;
	int pid1,pid2;
	//child1;
	if((pid1 = fork()) == 0){ 
		sigaction(5, act1, 0);
		while(1){}
	}
	//child2
	if((pid2 = fork()) == 0){ //child2
		sigaction(5, act2, 0);
		while(1){}
	}
	
	//parent
	fprintf(2, "* note that both user handler 1 and user handler 2 should be printed!\n");
	if (kill(pid1,5) < 0)
		xstate = 1;
	sleep(10);
	if (kill(pid1, SIGKILL) < 0)
		xstate = 1;
	int status = 0;
	wait(&status);

	if (kill(pid2,5) < 0)
		xstate = 1;
	sleep(10);
	if (kill(pid2,SIGKILL) < 0)
		xstate = 1;

	wait(0);
	
	exit(xstate);
}

void
test_sigprocmask(){
  //not for use - just to avoid address 0
	struct sigaction* x = malloc(sizeof(struct sigaction));
	x->sa_handler = dummy;
	x->sigmask = 0;

	struct sigaction* act = malloc(sizeof(struct sigaction));
	act->sa_handler = handler;
	act->sigmask = 0;

	fprintf(2, "* note that nothing should be printed out during the test!\n");
	int xstate = 0;
	int pid = fork();
	if(pid == 0){
		sigprocmask((1 << 5));
		sigaction(5, act, 0);
		while(1){};
	}
	//parent
	sleep(100);
	if (kill(pid,5) < 0)
		xstate = 1;
	sleep(100);
	if (kill(pid, 5) < 0)
		xstate = 1; 
	sleep(100);
	if (kill(pid,SIGKILL) < 0)
		xstate = 1;
	wait(0);
  exit(xstate);
}


// run each test in its own process. run returns 1 if child's exit()
// indicates success.
int
run(void f(char *), char *s) {
  int pid;
  int xstatus;

  printf("\n%s:\n", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    f(s);
    exit(0);
  } else {
    wait(&xstatus);
    if(xstatus != 0) 
      printf("FAILED\n");
    else
      printf("OK\n");
    return xstatus == 0;
  }
}


int main(){
	struct test {
    void (*f)(char *);
    char *s;
  }
	tests[] = {
	{test_kill,"test kill"},
	{test_sigstop_sigcont,"test sigstop sigcont"},
	{test_sigaction,"test sigaction"},
	{test_infinite_loop_and_kill,"test infinite loop and kill"},
	{test_stop_cont_kill,"test stop cont kill"},
	{test_cont_before_stop,"test cont before stop"},
	{test_custom_handler,"test custom handler"},
	{test_two_custom_handlers,"test two custom handlers"},
	{test_sigprocmask,"test sigprocmask"},
	{0,0}
	};

	printf("TESTS STARTING\n");

  int fail = 0;
  for (struct test *t = tests; t->s != 0; t++) {
		if(!run(t->f, t->s))
			fail ++;
  }

  if(fail == 10){
    printf("\nALL TESTS FAILED\n");
    exit(1);
	}

	if (fail > 0){
    printf("\n%d TESTS FAILED\n", fail);
    exit(1);
	}
	printf("\nALL TESTS PASSED\n");
	exit(0);
}