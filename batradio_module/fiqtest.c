#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <asm/fiq.h>
#include <asm/pgtable.h>

#include "batradio.h"

static struct platform_device *pdev;

// I/O registers
static uint32_t  *gpiospi;
static uint32_t  *irqtimer;

void short_wait(int v)
{ int w;
  while (v--)
    for (w=0; w<10; w++)
    { w++;
      w--;
    }
}


struct bat_data {
  void __iomem	*fiq_base;
  uint32_t dma_handle;
  unsigned int	irq;
  uint32_t teller;
  unsigned char bufnum;
};

static struct bat_data *batradio_data;
struct fiq_buffer *fiq_buf;

static int batradio_mmap(struct file *file, struct vm_area_struct *vma)
{
  size_t size = vma->vm_end - vma->vm_start;
  phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

  // why offset==0 ?
  printk("offset %x,  size %x\n", offset, size);
  if (offset + size > FIQ_BUFFER_SIZE)
    return -EINVAL;
  
  // + ?
  offset += virt_to_phys(batradio_data->fiq_base);
  printk("phys offset %p   %x\n", batradio_data->fiq_base, offset);
  
  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

  if (remap_pfn_range(vma,
		      vma->vm_start,
		      offset >> PAGE_SHIFT,
		      size,
		      vma->vm_page_prot)) {
    return -EAGAIN;
  }

  fiq_buf->status += 100;

  return 0;
}

static long batradio_ioctl(struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	printk("before FIO ioctl %d\n", fiq_buf->status);

	switch (cmd) {
	case FIQ_START:
		fiq_buf->status = FIQ_STATUS_RUNNING;
		printk("FIO_START %d\n", fiq_buf->status);
		//  led on
		GPIO_CLR = 1<<CNVST;  // led aan
		break;
	case FIQ_STOP:
		fiq_buf->status = FIQ_STATUS_STOPPED;
		printk("FIO_STOP %d\n", fiq_buf->status);
		//  led off
		GPIO_SET = 1<<CNVST;  // led uit
		break;
	case FIQ_RESET:
		fiq_buf->status = FIQ_STATUS_STOPPED;
		//  ...
		printk("FIO_RESET %d\n", fiq_buf->status);
		break;
	default:
		return -ENOTTY;
	};

	return 0;
}

static const struct file_operations batradio_fops = {
	.mmap		= &batradio_mmap,
	.unlocked_ioctl	= &batradio_ioctl,
};

static struct miscdevice batradio_dev = {
	.name	= "batradio",
	.fops	= &batradio_fops,
	.minor	= MISC_DYNAMIC_MINOR,
};

extern unsigned char batradio_handler, batradio_handler_end;

static struct fiq_handler bat_fh = {
	.name	= "batradio_handler"
};

static int toggle = 0;
void c_fiq_handler(void) {

  //  clear timer interrupt
  TIMCINT = 0;

  if (toggle) {
    GPIO_CLR = 1<<CNVST;  // led aan
    toggle = 0;
  }
  else {
    GPIO_SET = 1<<CNVST;  // led uit
    toggle = 1;
  }

}

int init_bat(void)
{
  int ret;
  struct pt_regs regs;
  
  // fail if not on a BCM2835?

  pdev = platform_device_register_simple("batradio__", 0, NULL, 0);
  if (IS_ERR(pdev))
    return PTR_ERR(pdev);
	
  batradio_data = devm_kzalloc(&pdev->dev,
			       sizeof(*batradio_data),
			       GFP_KERNEL);
  if (!batradio_data)
    return -ENOMEM;
  
  /*  why does'nt this work?
  batradio_data->fiq_base = dma_zalloc_coherent(&pdev->dev,
						FIQ_BUFFER_SIZE,
						&batradio_data->dma_handle,
						GFP_KERNEL);
  */
  batradio_data->fiq_base = devm_kzalloc(&pdev->dev,
					 FIQ_BUFFER_SIZE,
					 GFP_KERNEL);
  if (!batradio_data->fiq_base) {
    dev_err(&pdev->dev, "Couldn't allocate memory!\n");
    return -ENOMEM;
  }

  fiq_buf = (struct fiq_buffer *)batradio_data->fiq_base;
  fiq_buf->status = 55;

  dev_info(&pdev->dev,
	   "Allocated pages at address 0x%p, with size %x bytes\n",
	   batradio_data->fiq_base, FIQ_BUFFER_SIZE);
  
  gpiospi = (uint32_t *) ioremap(GPIOSPI_BASE, GPIOSPI_SIZE);
  if (gpiospi == NULL) {
    printk("ioremap: gpiospi error!\n");
    return -ENOMEM;
  }

  // configure CVVST as output
  INP_GPIO(CNVST); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(CNVST);

  //  adc
  //  setup_spi();

  irqtimer = (uint32_t *) ioremap(IRQTIMER_BASE, IRQTIMER_SIZE);
  if (irqtimer == NULL) {
    printk("ioremap: irqtimer error!\n");
    return -ENOMEM;
  }

  printk("init_bat: rar irq 0x%x\n", RAWINT);

  // set ARM timer
  TIMCNTR = 0x003E002;
  TIMLOAD = 10000-1;
  TIMPRED = 500-1;
  TIMCINT = 0;

  /*
Add to the cmdline.txt in /boot

dwc_otg.fiq_fsm_enable=0 dwc_otg.fiq_enable=0 dwc_otg.nak_holdoff=0

It appears that what did the trick has been the order of the fiq disable
commands. dwc_otg.fiq_fsm_enable=0 must be set *before* dwc_otg.fiq_enable=0.
The other way around it finds fiq_fsm_enable true with fiq_enable false, and it
forces fiq_enable true.n short - need to modify the boot options to remove
  */

  ret = claim_fiq(&bat_fh);
  if (ret) {
    printk("batradio: claim_fiq failed.\n");
    return ret;
  }
  
  set_fiq_handler(&batradio_handler,
		  &batradio_handler_end - &batradio_handler);

  regs.ARM_r8 = (long)gpiospi;
  regs.ARM_r9 = (long)irqtimer;
  regs.ARM_r10 = (long)batradio_data->fiq_base;
  set_fiq_regs(&regs);

  // timer interrupt to fiq
  IRQFIQ = 0xA0;
  // start timer with interrupt
  TIMCNTR = 0x003E00A2;
  TIMCINT = 0;

  // create device
  ret = misc_register(&batradio_dev);
  if (ret)
    return ret;

  printk("init_bat 2: basic irq 0x%x\n", BASIRQ);
  printk(KERN_INFO "  Installed batradio module... \n");

  return 0;
}


void exit_bat(void)
{
  misc_deregister(&batradio_dev);
  IRQFIQ = 0x00;
  TIMCNTR = 0x003E0000;
  // gpio
  release_fiq(&bat_fh);
  platform_device_unregister(pdev);
  printk(KERN_INFO "  Removed batradio module... \n");
  return;
}

module_init(init_bat);
module_exit(exit_bat);

MODULE_DESCRIPTION("FIQ handler for the batradio hat on RPI zero");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
