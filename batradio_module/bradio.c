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

#define BCM2708_PERI_BASE  0x20000000
#define IRQREGS           (BCM2708_PERI_BASE + 0xB200)
#define ARMTIMER          (BCM2708_PERI_BASE + 0xB400)
uint32_t irqregs  = IRQREGS;
uint32_t armtimer = ARMTIMER;

static struct platform_device *pdev;

struct bat_data {
  void __iomem	*fiq_base;
  uint32_t dma_handle;
  uint32_t teller;
  unsigned char bufnum;
};

static struct bat_data *batradio_data;

static int batradio_mmap(struct file *file, struct vm_area_struct *vma)
{
  struct fiq_buffer *fiq_buf = (struct fiq_buffer *)batradio_data->fiq_base;
  size_t size = vma->vm_end - vma->vm_start;
  unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

  printk("batradio mmap: 0x%p\n",fiq_buf);

  if (offset + size > FIQ_BUFFER_SIZE)
    return -EINVAL;

  // __pa zou niet gebruikt mogen worden volgens memory.h
  offset += __pa(batradio_data->fiq_base);

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
	struct fiq_buffer *fiq_buf = (struct fiq_buffer *)batradio_data->fiq_base;

	switch (cmd) {
	case FIQ_START:
		fiq_buf->status = FIQ_STATUS_RUNNING;
		//  ...
		printk("FIO_START %d\n", fiq_buf->status);
		break;
	case FIQ_STOP:
		fiq_buf->status = FIQ_STATUS_STOPPED;
		//  ...
		printk("FIO_STOP %d\n", fiq_buf->status);
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
  struct fiq_buffer *fiq_buf;
  
  pdev = platform_device_register_simple("batradio__", 0, NULL, 0);
  if (IS_ERR(pdev))
    return PTR_ERR(pdev);
	
  batradio_data = devm_kzalloc(&pdev->dev,
			       sizeof(*batradio_data),
			       GFP_KERNEL);
  if (!batradio_data)
    return -ENOMEM;
  
  batradio_data->fiq_base = dma_zalloc_coherent(&pdev->dev,
						FIQ_BUFFER_SIZE,
						&batradio_data->dma_handle,
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

    platform_device_unregister(pdev);

    dma_free_coherent(&pdev->dev,
		      FIQ_BUFFER_SIZE,
		      batradio_data->fiq_base,
		      batradio_data->dma_handle);

    return;
}

module_init(init_bat);
module_exit(exit_bat);

MODULE_DESCRIPTION("FIQ handler for the batradio hat on RPI zero");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
