#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc >= 2) {
    fprintf(2, "freemem: Too many arguments\n");
    exit(1);
  }

  int num = -1;

  if (freemem(&num) < 0) {
    fprintf(2, "freemem failed!\n");
    exit(1);
  }

  printf("Number of bytes of free memory: %d\n", num);
  exit(0);
}
