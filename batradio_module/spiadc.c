/*
  read from  mcp33131 16 bit adc
  derived from C example from https://elinux.org/RPi_GPIO_Code_Samples
                         and gertboard examples

  Na gebruikt doet python versie het niet meer goed.
  Wat laat ik niet goed achter?
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

struct timespec st, et;

#include "spi.h"

// also for BCM2835 on rpi_zero
#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define SPI0_BASE                (BCM2708_PERI_BASE + 0x204000) /* SPI0 controller */


#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

int  mem_fd;

// I/O access
char *gpio_mem_orig, *gpio_mem, *gpio_map;
volatile unsigned *gpio;

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

#define GET_GPIO(g) (*(gpio+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

#define GPIO_PULL *(gpio+37) // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio+38) // Pull up/pull down clock

// SPI constants, from gb_stuff
char *spi0_mem_orig, *spi0_mem, *spi0_map;
volatile unsigned *spi0;

void intHandler(int dummy) {

  printf("\n Ended with control-C\n");
  restore_io();
  exit(0);
}

int main(int argc, char **argv)
{ char dummy;
  int i, d, t;
  #define N 10000
  int arr[N];
  
  signal(SIGINT, intHandler);
  
  setup_io();
  setup_gpio();
  setup_spi();
    
 // configure CNVST
  INP_GPIO(13); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(13);

  // read data
  clock_gettime(CLOCK_MONOTONIC, &st);
  i = 0;
  while(i<N) {
    // start acquisition  (700 nsec)
    GPIO_SET = 1<<13;
    short_wait(3);
    // stop acquisition
    GPIO_CLR = 1<<13;

    arr[i] = read_adc();
    short_wait(1);
    i++;
  }

  clock_gettime(CLOCK_MONOTONIC, &et);
  t = (et.tv_sec - st.tv_sec)*1000000 + (et.tv_nsec - st.tv_nsec)/1000;
  printf("elapsed %f microseconds per cycle.\n",  (float)t/(float)N);

  i = 0;
  while (i< N) {
    printf("%d\n", arr[i]);
    i++;
  }
  
  restore_io();
  return 0;

}


//
// Set up a memory regions to access GPIO
//
void setup_io()
{ unsigned long extra;
  
   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit(-1);
   }

   /* mmap GPIO */
   gpio_map = mmap(
      NULL,             //Any adddress in our space will do
      BLOCK_SIZE,       //Map length
      PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
      MAP_SHARED,       //Shared with other processes
      mem_fd,           //File to map
      GPIO_BASE         //Offset to GPIO peripheral
   );

   if (gpio_map == MAP_FAILED) {
      printf("mmap error %d\n", (int)gpio_map);//errno also set!
      exit(-1);
   }

   gpio = (volatile unsigned *)gpio_map;

   /*
    * mmap SPI0
    */
   if ((spi0_mem_orig = malloc(BLOCK_SIZE + (PAGE_SIZE-1))) == NULL) {
      printf("allocation error \n");
      exit (-1);
   }
   extra = (unsigned long)spi0_mem_orig % PAGE_SIZE;
   if (extra)
     spi0_mem = spi0_mem_orig + PAGE_SIZE - extra;
   else
     spi0_mem = spi0_mem_orig;

   spi0_map = (unsigned char *)mmap(
      (caddr_t)spi0_mem,
      BLOCK_SIZE,
      PROT_READ|PROT_WRITE,
      MAP_SHARED|MAP_FIXED,
      mem_fd,
      SPI0_BASE
   );

   if ((long)spi0_map < 0) {
      printf("spi0 mmap error %d\n", spi0_map);
      exit (-1);
   }
   spi0 = (volatile unsigned *)spi0_map;

   close(mem_fd);

}

void restore_io()
{
  munmap(spi0_map,BLOCK_SIZE);
  munmap(gpio_map,BLOCK_SIZE);
  // free memory
  free(spi0_mem_orig);
  free(gpio_mem_orig);
} // restore_io

// from gb_spi.c

// For D to A we only need the SPI bus and SPI chip select B
void setup_gpio()
{
   INP_GPIO(7);  SET_GPIO_ALT(7,0);
   INP_GPIO(9);  SET_GPIO_ALT(9,0);
   INP_GPIO(10); SET_GPIO_ALT(10,0);
   INP_GPIO(11); SET_GPIO_ALT(11,0);
} // setup_gpio


void setup_spi()
{
  // Want to have 1 MHz SPI clock.
  // Assume 250 Mhz system clock
  // So divide 250MHz system clock by 250 to get 1MHz45
  SPI0_CLKSPEED = 10;   // 4 werkt ook, maar geeft met 10 al problemen met logic analyzer, dan 50 doen
  //SPI0_CLKSPEED = 125;

  // clear FIFOs and all status bits
  SPI0_CNTLSTAT = SPI0_CS_CLRALL;
  SPI0_CNTLSTAT = SPI0_CS_DONE; // make sure done bit is cleared
} // setup_spi()


void short_wait(int v)
{ int w;
  while (v--)
    for (w=0; w<10; w++)
    { w++;
      w--;
    }
}

//
// Read a value from MCP33131 adc
//
int read_adc()
{ unsigned char v1,v2;
  int status, res;
  // Set up for single ended, MS comes out first
  v1 = 0xD0;  // | (chan<<5);
  // Delay to make sure chip select is high for a short while
  short_wait(0);

  // Enable SPI interface: No SC and set activate bit
  SPI0_CNTLSTAT = SPI0_CS_CHIPSELN|SPI0_CS_ACTIVATE;

  // Write the command into the FIFO so it will
  // be transmitted out of the SPI interface to the ADC
  // We need a 16-bit transfer so we send a command byte
  // folowed by a dummy byte
  SPI0_FIFO = v1;
  SPI0_FIFO = 0; // dummy

  // wait for SPI to be ready
  // This will take about 16 micro seconds
  do {
     status = SPI0_CNTLSTAT;
  } while ((status & SPI0_CS_DONE)==0);
  SPI0_CNTLSTAT = SPI0_CS_DONE; // clear the done bit

  // Data from the ADC chip should now be in the receiver
  // read the received data
  v1 = SPI0_FIFO;
  v2 = SPI0_FIFO;
  res = (int16_t)((v1<<8) | v2);
  return res;
}

//
// Write 12 bit value to MCP4821 DAC 
//
void write_dac(int val)
{ char v1,v2,dummy;
  int status;
  val &= 0xFFF;  // force value in 12 bits

  // Build the first byte: write, channel 0 or 1 bit
  // and the 4 most significant data bits
  v1 = 0x30 | (val>>8);
  // Remain the Least Significant 8 data bits
  v2 = val & 0xFF;

  // Delay to have CS high for a short while
  short_wait(1);
  
  // Enable SPI: Use CS 1 and set activate bit
  SPI0_CNTLSTAT = SPI0_CS_CHIPSEL1|SPI0_CS_ACTIVATE;

  // send the values
  SPI0_FIFO = v1;
  SPI0_FIFO = v2;

  // wait for SPI to be ready
  // This will take about 16 micro seconds
  do {
     status = SPI0_CNTLSTAT;
  } while ((status & SPI0_CS_DONE)==0);
  SPI0_CNTLSTAT = SPI0_CS_DONE; // clear the done bit


  // For every transmit there is also data coming back
  // We MUST read that received data from the FIFO
  // even if we do not use it!
  dummy = SPI0_FIFO;
  dummy = SPI0_FIFO;
}


