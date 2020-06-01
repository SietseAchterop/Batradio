#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <asm/fiq.h>
#include <asm/pgtable.h>

#include "batradio.h"

// I/O registers
static uint32_t  *gpiospi;
static uint32_t  *irqtimer;

static struct platform_device *pdev;

struct bat_data {
  void __iomem	*fiq_base;
  uint32_t dma_handle;
  uint32_t teller;
  unsigned char bufnum;
};

static struct bat_data *batradio_data;
struct fiq_buffer *fiq_buf;

struct fiq_buffer bufbuf;

struct pt_regs regs;
static char fiqstack[4096];

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
  switch (cmd) {
  case FIQ_START:
    get_fiq_regs(&regs);
    printk("ioctr_start, count %ld\n", regs.ARM_r10);
    TIMLOAD = 100000-1;
    TIMCINT = 0;
    TIMCNTR = 0x000000A2;
    fiq_buf->status = 23;
    //  ...
    break;
  case FIQ_STOP:
    TIMCNTR = 0x00000000;
    IRQFIQ = 0x00;
    TIMCINT = 0;
    get_fiq_regs(&regs);
    printk("ioctr_stop, count %ld\n", regs.ARM_r10);
    fiq_buf->status = 7;
    //  ...
    break;
  case FIQ_RESET:
    printk("ioctl reset\n");
    get_fiq_regs(&regs);
    printk("ioctr_reset, count %ld\n", regs.ARM_r10);
    //  ...
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

/*
static struct fiq_handler bat_fh = {
	.name	= "batradio_handler"
};
*/
int init_bat(void)
{
  int ret;
  
  pdev = platform_device_register_simple("batradio__", 0, NULL, 0);
  if (IS_ERR(pdev))
    return PTR_ERR(pdev);
	
  batradio_data = devm_kzalloc(&pdev->dev,
			       sizeof(*batradio_data),
			       GFP_KERNEL);
  if (!batradio_data)
    return -ENOMEM;
  
  /*
  // nodig (cma=20 in /boot/cmdline.txt lijkt niet nodig)
  pdev->dev.coherent_dma_mask = 0xffffffff;
 
  batradio_data->fiq_base = dma_zalloc_coherent(&pdev->dev,
						FIQ_BUFFER_SIZE,
						&batradio_data->dma_handle,
						GFP_KERNEL);
  if (!batradio_data->fiq_base) {
    dev_err(&pdev->dev, "Couldn't allocate memory!\n");
    return -ENOMEM;
  }

  fiq_buf = (struct fiq_buffer *)batradio_data->fiq_base;
  fiq_buf->status = 56;

  dev_info(&pdev->dev,
	   "Allocated pages at address 0x%p, with size %x bytes\n",
	   batradio_data->fiq_base, FIQ_BUFFER_SIZE);
  
  */

  fiq_buf = &bufbuf;
  fiq_buf->status = 56;
  
  gpiospi = (uint32_t *) ioremap(GPIOSPI_BASE, GPIOSPI_SIZE);
  if (gpiospi == NULL) {
    printk("ioremap: gpiospi error!\n");
    return -ENOMEM;
  }

  // configure GPIO13 as output
  INP_GPIO(CNVST); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(CNVST);

  irqtimer = (uint32_t *) ioremap(IRQTIMER_BASE, IRQTIMER_SIZE);
  if (irqtimer == NULL) {
    printk("ioremap: irqtimer error!\n");
    return -ENOMEM;
  }

  /*
Add to the cmdline.txt in /boot

dwc_otg.fiq_fsm_enable=0 dwc_otg.fiq_enable=0 dwc_otg.nak_holdoff=0

It appears that what did the trick has been the order of the fiq disable
commands. dwc_otg.fiq_fsm_enable=0 must be set *before* dwc_otg.fiq_enable=0.
The other way around it finds fiq_fsm_enable true with fiq_enable false, and it
forces fiq_enable true.n short - need to modify the boot options to remove
  */

  // we assume nothing else uses ARM timer or FIQ
  set_fiq_handler(&batradio_handler,
		  &batradio_handler_end - &batradio_handler);

  regs.ARM_r8  = (long)gpiospi;
  regs.ARM_r9  = (long)irqtimer;
  regs.ARM_r10 = (long)0;        //batradio_data->fiq_base;
  regs.ARM_sp  = (long)&fiqstack[sizeof(fiqstack)];     // needed?
  set_fiq_regs(&regs);

  // start timer with interrupt
  TIMLOAD = 100000-1;
  TIMCINT = 0;
  TIMCNTR = 0x000000A2;
  // timer interrupt to fiq
  IRQFIQ = 0xC0;

  ret = 10;
  while (ret) {
    get_fiq_regs(&regs);
    printk("timer %d, raw int pending %x, count %ld\n", TIMVAL, RAWINT, regs.ARM_r10);
    msleep(1);
    ret--;
  }

  // create device
  ret = misc_register(&batradio_dev);
  if (ret)
    return ret;

  printk(KERN_INFO "  Installed batradio module... \n");

  return 0;
}


void exit_bat(void)
{
  printk(KERN_INFO "  Remove batradio module... \n");

  misc_deregister(&batradio_dev);

  TIMCNTR = 0x00000000;
  IRQFIQ = 0x00;
  TIMCINT = 0;
  
  platform_device_unregister(pdev);

  /*
  dma_free_coherent(&pdev->dev,
		    FIQ_BUFFER_SIZE,
		    batradio_data->fiq_base,
		    batradio_data->dma_handle);
  */
  return;
}

module_init(init_bat);
module_exit(exit_bat);

MODULE_DESCRIPTION("FIQ handler for the batradio hat on RPI zero");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
