#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#define DIE(x) {perror(x); exit(1);}
int main(int argc, char ** argv) {
  char buf[1024*11];
  int fd;
  int i,j,cnt;
  off_t s;  
  if(argc != 3) {
    printf("usage: bench file cnt\n");
    exit(1);
  }
  fd = open(argv[1],O_RDONLY);
  if(fd == -1) DIE("open");
  s = lseek(fd, 0, SEEK_END);
  if(s == -1) DIE("lseek");
  cnt=atoi(argv[2]);
  printf("%4d%%",0);
  fflush(stdout);
  for(i=0; i < 100; i++) {
    for(j=0; j < cnt; j++) {
      uint64_t _ = rand() % (10*1024);
      uint64_t l = (rand() & 0xFFFFll) << 0 | (rand() & 0xFFFFll) << 16 | (rand() & 0xFFFFll) << 32 | (rand() & 0xFFFFll) << 48;
      l %= (s-_);
      if(lseek(fd,l,SEEK_SET) == -1) DIE("lseek");
      if(read(fd,buf,_) != _) DIE("read");
    }
    printf("\r%4d%%",i);
    fflush(stdout);
  }
  printf("\n");
  return 0;
}
