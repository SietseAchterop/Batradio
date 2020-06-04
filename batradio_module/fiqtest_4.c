#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <asm/fiq.h>

// for BCM2835 on rpi_zero
#define BCM2708_PERI_BASE        0x20000000
#define GPIOSPI_BASE            (BCM2708_PERI_BASE + 0x200000)  // gpio  0000,   spi  4000
#define GPIOSPI_SIZE             0x5000
#define IRQTIMER_BASE           (BCM2708_PERI_BASE + 0xB000)    //  irqs  B200,  armtimer B400
#define IRQTIMER_SIZE            0x1000

//    Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpiospi+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpiospi+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpiospi+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

// ****  IRQ  ****
#define BASIRQ  *(irqtimer+ 0x200/4)
#define IRQFIQ  *(irqtimer+ 0x200/4 + 3)
#define BASENA  *(irqtimer+ 0x200/4 + 6)
#define BASDIS  *(irqtimer+ 0x200/4 + 9)

// ****  ARM timer  ****
#define TIMLOAD *(irqtimer+0x400/4)
#define TIMVAL  *(irqtimer+0x400/4+1)
#define TIMCNTR *(irqtimer+0x400/4+2)
#define TIMCINT *(irqtimer+0x400/4+3)
#define RAWINT  *(irqtimer+0x400/4+4)
#define MSKINT  *(irqtimer+0x400/4+5)
#define TIMPRED *(irqtimer+0x400/4+7)

// gpio for cs pulse in batradio (of led in testing)
#define CNVST 13

static uint32_t  *gpiospi;
static uint32_t  *irqtimer;

extern unsigned char batradio_handler, batradio_handler_end;

int init_bat(void)
{
  int ret;
  struct pt_regs regs;
  
  gpiospi = (uint32_t *) ioremap(GPIOSPI_BASE, GPIOSPI_SIZE);
  if (gpiospi == NULL) {
    printk("ioremap: gpiospi error!\n");
    return -ENOMEM;
  }

  // configure CNVST as output
  INP_GPIO(CNVST);
  OUT_GPIO(CNVST);

  irqtimer = (uint32_t *) ioremap(IRQTIMER_BASE, IRQTIMER_SIZE);
  if (irqtimer == NULL) {
    printk("ioremap: irqtimer error!\n");
    return -ENOMEM;
  }

  // directly set ARM timer registers
  TIMCNTR = 0x0000000;   // stop timer
  // pre-divider default 126
  TIMLOAD = 100000-1;    // load value
  TIMCINT = 0;           // clear interrupt

  /* Add to the cmdline.txt in /boot
        dwc_otg.fiq_fsm_enable=0 dwc_otg.fiq_enable=0 dwc_otg.nak_holdoff=0

  ret = claim_fiq(&bat_fh);
  if (ret) {
    printk("batradio: claim_fiq failed.\n");
    iounmap(irqtimer); // needed?
    return ret;
  }
  */  
  set_fiq_handler(&batradio_handler, &batradio_handler_end - &batradio_handler);

  // stack already set?
  regs.ARM_r8  = (long)gpiospi;
  regs.ARM_r9  = (long)irqtimer;
  regs.ARM_r10 = (long)0;
  set_fiq_regs(&regs);

  TIMCNTR = 0x000000A2;   // start timer with interrupt, 23 bit counter
  IRQFIQ = 0xC0;         // timer interrupt to fiq directly via register
  //enable_fiq(64);

  ret = 10;
  while (ret) {
    get_fiq_regs(&regs);
    printk("timer %d, raw int pending %x, count %ld\n", TIMVAL, RAWINT, regs.ARM_r10);
    msleep(1);
    ret--;
  }

  printk(KERN_INFO "  Installed batradio module... \n");
  return 0;
}

void exit_bat(void)
{

  printk("timer %d  %x\n", TIMVAL, RAWINT);
  msleep(2);
  printk("timer %d\n", TIMVAL);

  IRQFIQ = 0x00;
  TIMCNTR = 0x003E0000;
  //disable_fiq(64);
  
  //release_fiq(&bat_fh);
  printk(KERN_INFO "  Removed batradio module... \n");
  return;
}

module_init(init_bat);
module_exit(exit_bat);

MODULE_DESCRIPTION("FIQ handler for the batradio hat on RPI zero");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
