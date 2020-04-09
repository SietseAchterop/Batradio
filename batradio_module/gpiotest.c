#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <asm/pgtable.h>

#include "batradio.h"

static struct platform_device *pdev;

volatile uint32_t  *gpio;


struct bat_data {
  void __iomem	*fiq_base;
  uint32_t dma_handle;
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
		GPIO_CLR = 1<<LED;  // led aan
		break;
	case FIQ_STOP:
		fiq_buf->status = FIQ_STATUS_STOPPED;
		printk("FIO_STOP %d\n", fiq_buf->status);
		//  led off
		GPIO_SET = 1<<LED;  // led uit
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

int init_bat(void)
{
  int ret;
  
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
  
   /* mmap GPIO */
  gpio = (uint32_t *) ioremap(GPIO_BASE, GPIO_SIZE);
  if (gpio == NULL) {
    printk("ioremap error!\n");
    return -ENOMEM;
  }

 // configure status led
  INP_GPIO(LED); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(LED);

  // create device
  ret = misc_register(&batradio_dev);
  if (ret)
    return ret;

  printk(KERN_INFO "  Installed batradio module... \n");

  return 0;
}


void exit_bat(void)
{
  misc_deregister(&batradio_dev);
  platform_device_unregister(pdev);
  printk(KERN_INFO "  Removed batradio module... \n");
  return;
}

module_init(init_bat);
module_exit(exit_bat);

MODULE_DESCRIPTION("FIQ handler for the batradio hat on RPI zero");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
