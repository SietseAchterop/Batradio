/*   SPDX-License-Identifier: GPL-2.0
 *
 */

// #include <sys/types.h>
#include <linux/ioctl.h>

// for BCM2835 on rpi_zero
#define BCM2708_PERI_BASE        0x20000000
#define GPIOSPI_BASE            (BCM2708_PERI_BASE + 0x200000)  // gpio  0000,   spi  4000
#define GPIOSPI_SIZE             0x5000
#define IRQTIMER_BASE           (BCM2708_PERI_BASE + 0xB000)    //  irqs  B200,  armtimer B400
#define IRQTIMER_SIZE            0x1000

//  I/O access macro's. Note the 32 bit access!
// **** GPIO  ****
// GPGSELi
//    Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpiospi+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpiospi+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpiospi+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

// GPSET0
#define GPIO_SET *(gpiospi+7)  // sets   bits which are 1 ignores bits which are 0
// GPCLR0
#define GPIO_CLR *(gpiospi+10) // clears bits which are 1 ignores bits which are 0

// GPLEV0
#define GET_GPIO(g) (*(gpiospi+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

// GPPUD
#define GPIO_PULL *(gpiospi+37) // Pull up/pull down
// GPPUDCLK0
#define GPIO_PULLCLK0 *(gpiospi+38) // Pull up/pull down clock

// gpio for cs pulse in batradio
#define CNVST 13
// idem for led
#define LED   26

// ****  SPI  ****
//       SPI macros and constants. Only the few we need
//
#define SPI0_CNTLSTAT *(gpiospi + 0x4000/4+0)
#define SPI0_FIFO     *(gpiospi + 0x4000/4+1)
#define SPI0_CLKSPEED *(gpiospi + 0x4000/4+2)

#define SPI0_CS_DONE         0x00010000 // SPI transfer done. WRT to CLR!
#define SPI0_CS_ACTIVATE     0x00000080 // Activate: be high before starting
#define SPI0_CS_CS_POLARIT   0x00000040 // Chip selects active high
#define SPI0_CS_CLRFIFOS     0x00000030 // Clear BOTH FIFOs (auto clear bit)
#define SPI0_CS_CHIPSEL1     0x00000001 // Use chip select 1
#define SPI0_CS_CHIPSELN     0x00000003 // No chip select (e.g. use GPIO pin)
#define SPI0_CS_CLRALL      (SPI0_CS_CLRFIFOS|SPI0_CS_DONE)


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


#define FIQ_DATA_SIZE           (8 * 32 * 1024)
#define FIQ_BUFFER_SIZE		(2 * FIQ_DATA_SIZE + 4096)

#define FIQ_IOC_MAGIC            'p'
#define FIQ_START		_IO(FIQ_IOC_MAGIC, 0xb0)
#define FIQ_STOP		_IO(FIQ_IOC_MAGIC, 0xb1)
#define FIQ_RESET		_IO(FIQ_IOC_MAGIC, 0xb2)

#define FIQ_STATUS_STOPPED	(0)
#define FIQ_STATUS_RUNNING	(1 << 0)
#define FIQ_STATUS_ERR_URUN	(1 << 1)

struct fiq_buffer {
  u_int16_t data[FIQ_DATA_SIZE];   // 8 buffers for 32 msec of samples
  // pointer to latest available full buffer
  // char of int?
  unsigned char bufp;
  unsigned char onoff;
  unsigned char status;

  //  ...
};

