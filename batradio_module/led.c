/*
  Led control on rpi zero

  Temporary turn normal led control off
    # Set the Pi Zero ACT LED trigger to 'none'.
    echo none | sudo tee /sys/class/leds/led0/trigger

    # Turn off the Pi Zero ACT LED.
    echo 1 | sudo tee /sys/class/leds/led0/brightness

 */

// gpio for cs pulse in batradio
#define LED 13

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

struct timespec st, et;

// also for BCM2835 on rpi_zero
#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

int  mem_fd;

// I/O access
char   *gpio_map;
#define gpio  ((volatile unsigned *)gpio_map)

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

#define GET_GPIO(g) (*(gpio+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

#define GPIO_PULL *(gpio+37) // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio+38) // Pull up/pull down clock

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

   close(mem_fd);

}

void restore_io()
{
  munmap(gpio_map,BLOCK_SIZE);
}

void short_wait(int v)
{ int w;
  while (v--)
    for (w=0; w<10; w++)
    { w++;
      w--;
    }
}

void intHandler(int dummy) {

  printf("\n Ended with control-C\n");
  GPIO_SET = 1<<LED;
  restore_io();
  exit(0);
}

void mysleep(int n) {
  volatile int cnt = n, cnt2;
  while (cnt > 0) {
    cnt--;
    cnt2 = n;
    while (cnt2 > 0) cnt2--;
  }
}

int main(int argc, char **argv)
{ char dummy;
  int i, d, t;
  #define N 10000
  int arr[N];
  
  signal(SIGINT, intHandler);
  
  setup_io();
    
  printf("GPIO_map:  0x%p\n", gpio_map);
  printf("GPIO:  0x%p\n", gpio);

 // configure status led
  INP_GPIO(LED); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(LED);

  // read data
  //  clock_gettime(CLOCK_MONOTONIC, &st);

#define ASSEM 1

  while(1) {

#if ASSEM==1
    // gokje, controleren dat r3 de waarde in gpio_map bevat!
    asm volatile (
		  "mov  r5, #0x2000 \n"
		  :: [gpiobase] "r" (gpio_map)
		  : "memory", "cc", "r5");
    asm volatile (
		  "str  r5, [r3, #28] \n"
		  "push {r3} \n"
		      );
    mysleep(10000);
    asm volatile (
		  "pop {r3} \n"
		  "str  r5, [r3, #40] \n"
		  "push {r3} \n"
		      );
    mysleep(10000);
    asm volatile (
		  "pop {r3} \n"
		      );
#else
    GPIO_SET = 1<<LED;  // led uit
    usleep(100000);
    GPIO_CLR = 1<<LED;  // led aan
    usleep(100000);    
#endif
    
  }

  clock_gettime(CLOCK_MONOTONIC, &et);
  t = (et.tv_sec - st.tv_sec)*1000000 + (et.tv_nsec - st.tv_nsec)/1000;
  printf("elapsed %f microseconds per cycle.\n",  (float)t/(float)N);

  restore_io();
  return 0;
}


