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
    goto out;
  }

  fiq_addr = mmap(NULL, FIQ_BUFFER_SIZE, PROT_READ | PROT_WRITE,
		  MAP_SHARED, fiq_fd, 0);
  if (fiq_addr == MAP_FAILED) {
    ret = errno;
    perror("Couldn't map the fiq buffer");
    goto out_close_fiq;
  }
  fiq_buf = (struct fiq_buffer*)fiq_addr;

  /*
   *  here our code
   *  2 modes:
   *     batradio: no need to read everything
   *               maybe 10msec each 0.1 seconds enough for the spectra.
   *     sdr: read everything, but directly select a band of 10 kHz from the signal
   *
   *  do calculations directly in the fiq_buffer?
   */

  // start collecting data
  ret = ioctl(fiq_fd, FIQ_START);
  if (ret) {
    printf("Couldn't start the FIQ\n");
    goto out_munmap_fiq;
  }

  /* currently processing and newest available full buffers
     newest should be at least 2 "behind" curr, to leave room
     for buffer to be filled by the driver. Probably 3 to get
     some more reaction time.
  */
  mode = 0;
  curr = 0;
  
  while (1)
    {
      // await a filled buffer
      while(curr == fiq_buf->bufp) {
	usleep(5000);
      }
      // prevent overruns
      newest = fiq_buf->bufp;
      if (curr < newest) newest -= 8;
      if ((curr - newest) < 3) {
	printf("drop current buffer to keep up\n");
	curr = (curr+1)%8;	
      }

      // process buffer
      printf("Processing buffer %d in mode %d\n", curr, mode);
      if (mode) {
	// batradio: full spectrum 

      } else {
	// sdr: select selected band and downsample

      }

      // next buffer to process
      curr = (curr+1)%8;

    }    

  // stop collecting data
  ioctl(fiq_fd, FIQ_STOP);


out_munmap_fiq:
	munmap(fiq_addr, FIQ_BUFFER_SIZE);
out_close_fiq:
	close(fiq_fd);
out:
	return ret;
}
