/* test program for rpi zero,  BCM2835  */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#define BCM2708_PERI_BASE    0x20000000
#define ARMINTTIMER          (BCM2708_PERI_BASE + 0xB000)
uint32_t* armintaddr;
#define timaddr  (armintaddr+(0x400)/4)

//
void c_irq_handler ( void )
{
  // clear interrupt
  *(timaddr+3) = 0;

  *(timaddr+0) = 200000-1;
}

int main() {

  armintaddr = (uint32_t*)mmap(
        NULL,
        0x1000,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        open("/dev/mem", O_RDWR | O_SYNC),
	ARMINTTIMER
    );
  if (armintaddr == MAP_FAILED) {
    perror("Couldn't map armintaddr");
    exit(-1);
  }

  // setup fiq interrupt
  //   ik moet het nu overbrengen naar een kernel module om de fiq code te kunnen gebruiken.

  // stop timer,  set pre-scaler
  *(timaddr+2) = 0x003E0000;
  // set load
  *(timaddr+0) = 100000-1;
  // set pre-divider
  *(timaddr+7) = 100;
  // clear interrupt
  *(timaddr+3) = 0;
  // start timer
  *(timaddr+2) = 0x003E0282;
    
  int i = 20;
  while (i) {
    printf("ARM timer value = %d, %d, %x, %x,   %u\n", *(timaddr + 0), *(timaddr + 1), *(timaddr + 2), *(timaddr + 4), *(timaddr + 8));
    usleep(10000);
    i--;
  }

  return 0;
}
