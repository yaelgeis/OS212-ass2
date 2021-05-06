#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  
  if(argc < 3 || argc % 2 == 0){
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }
  int i;
  for(i=1; i<argc; i+=2)
    kill(atoi(argv[i]), atoi(argv[i+1]));
  exit(0);
}
