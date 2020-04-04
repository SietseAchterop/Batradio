/*   SPDX-License-Identifier: GPL-2.0
 *
 *   Client program for the batradio device
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "batradio.h"

#define FIQ_PATH	"/dev/batradio"

void intHandler(int dummy) {

  printf("\n Ended with control-C\n");
  // stop driver?
  exit(0);
}

int main(int argc, char *argv[])
{
  struct fiq_buffer *fiq_buf;
  struct stat st;
  void *fiq_addr;
  int fiq_fd;
  int ret = 0, i;
  unsigned long size, start_size;
  char curr, newest, mode;  // mode: 0 = sdr, 1 = batradio

  signal(SIGINT, intHandler);

  // open the batradio device
  fiq_fd = open(FIQ_PATH, O_RDWR);
  if (!fiq_fd) {
    ret = errno;
    perror("Couldn't open fiq file");
    exit(-1);
  }

  fiq_addr = mmap(NULL, FIQ_BUFFER_SIZE, PROT_READ | PROT_WRITE,
		  MAP_SHARED, fiq_fd, 0);
  if (fiq_addr == MAP_FAILED) {
    ret = errno;
    perror("Couldn't map the fiq buffer");
    exit(-2);
  }
  fiq_buf = (struct fiq_buffer*)fiq_addr;

  printf("status %d\n", fiq_buf->status);
 
  // start collecting data
  ret = ioctl(fiq_fd, FIQ_START);
  if (ret) {
    printf("Couldn't start the FIQ\n");
    exit(-3);
  }

  printf("status %d\n", fiq_buf->status);

  // start collecting data
  ret = ioctl(fiq_fd, FIQ_STOP);
  if (ret) {
    printf("Couldn't stop the FIQ\n");
    exit(-3);
  }

  printf("status %d\n", fiq_buf->status);

  // start collecting data
  ret = ioctl(fiq_fd, FIQ_RESET);
  if (ret) {
    printf("Couldn't reset the FIQ\n");
    exit(-3);
  }

  printf("status %d\n", fiq_buf->status);

  printf("NU de LOOP\n");
  while (1)
    {
      usleep(100000);
      printf("status %d\n", fiq_buf->status);
    }
  
  return 0;
}
