/*   SPDX-License-Identifier: GPL-2.0
 *
 *   Client program for the batradio device
 *     compile with
 *          . setenvzero.sh (or setenv7.sh)
 *          arm-linux-gnueabihf-gcc batradio_client.c -o batradio_client
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "batradio.h"

#define FIQ_PATH	"/dev/batradio"

int       mem_fd;
volatile uint32_t *irqtimer;

static inline unsigned asm_get_cpsr(void)
{
  unsigned long retval;
  asm volatile (" mrs  %0, cpsr" : "=r" (retval) : /* no inputs */  );
  return retval;
}

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

   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit(-1);
   }

  /* mmap irqtimer */
   irqtimer = (uint32_t *)mmap(
      NULL,             //Any adddress in our space will do
      1024,             //Map length
      PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
      MAP_SHARED,       //Shared with other processes
      mem_fd,           //File to map
      IRQTIMER_BASE         //Offset to GPIO peripheral
   );

   if (irqtimer == MAP_FAILED) {
      printf("mmap error %d\n", (int)irqtimer);//errno also set!
      exit(-1);
   }

   printf("load, val, raw irq %d  %d  0x%x\n", TIMLOAD, TIMVAL, RAWINT);


  // open the batradio device
  fiq_fd = open(FIQ_PATH, O_RDWR);
  if (!fiq_fd) {
    printf("Couldn't open batradio device, error %d\n", errno);
    exit(-1);
  }


  fiq_addr = mmap(NULL, FIQ_BUFFER_SIZE, PROT_READ | PROT_WRITE,
		  MAP_SHARED, fiq_fd, 0);
  if (fiq_addr == MAP_FAILED) {
    printf("Couldn't map the fiq buffer, error: %d\n", errno);
    exit(-2);
  }
  fiq_buf = (struct fiq_buffer*)fiq_addr;

  printf("status %d\n", fiq_buf->status);
  usleep(100000);
 
  // start collecting data
  ret = ioctl(fiq_fd, FIQ_START);
  if (ret) {
    printf("Couldn't start the FIQ\n");
    exit(-3);
  }

  printf("led on, status %d\n", fiq_buf->status);
  usleep(10000);

  // start collecting data
  ret = ioctl(fiq_fd, FIQ_STOP);
  if (ret) {
    printf("Couldn't stop the FIQ\n");
    exit(-3);
  }

  printf("led off, status %d\n", fiq_buf->status);

  // start collecting data
  ret = ioctl(fiq_fd, FIQ_RESET);
  if (ret) {
    printf("Couldn't reset the FIQ\n");
    exit(-3);
  }

  printf("status %d, cpsr  0x%x\n", fiq_buf->status,   asm_get_cpsr());


  printf("Now in the LOOP\n");

  //  TIMCINT = 0;
  //  TIMLOAD = 10000-1;
  
  i = 20;
  while (i)
    {
      printf("val, raw irq, cpsr: %u  0x%x  0x%x\n", (uint32_t)TIMVAL, RAWINT, asm_get_cpsr());
      printf("basic pending irq: 0x%x\n", BASIRQ);
      printf("fiq register: 0x%x\n", IRQFIQ);
      printf("bradio: enable basic  irq: 0x%x\n", BASENA);
      printf("bradio: disable basic irq: 0x%x\n", BASDIS);
      ret = ioctl(fiq_fd, FIQ_START);
      if (ret) {
	printf("Couldn't start the FIQ\n");
	exit(-3);
      }
      printf("status: %d\n", fiq_buf->status);
      printf("nul: %d\n", fiq_buf->data[0]);
      printf("drie: %d\n", fiq_buf->data[3]);
      usleep(10000);
      ret = ioctl(fiq_fd, FIQ_STOP);
      if (ret) {
	printf("Couldn't stop the FIQ\n");
	exit(-3);
      }
  
      printf("status: %d\n", fiq_buf->status);
      printf("nul: %d\n", fiq_buf->data[0]);
      printf("drie: %d\n", fiq_buf->data[3]);
      usleep(10000);
      printf("status %d\n", fiq_buf->status);
      i--;
    }
  
  return 0;
}
