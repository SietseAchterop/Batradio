#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

static int batradio_mmap(struct file *file, struct vm_area_struct *vma)
{ printk(KERN_INFO "batradio_mmap\n");
  return 0;
}
static long batradio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{ printk(KERN_INFO "batradio_ioctl\n");
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
static int bat_probe(struct platform_device *pdev)
{
  printk(KERN_INFO "Probe with bat_probe\n");
  //   .....
  return 0;
}

static int bat_remove(struct platform_device *pdev)
 { printk(KERN_INFO "bat_remove\n");
   return 0;
 }

static struct platform_driver bat_driver = {
	.probe	= bat_probe,
	.remove	= bat_remove,
	.driver = {
		   .name = "batradio",    // verband met miscdevice?
		   .owner = THIS_MODULE,
	},
};

int myinit(void)
{ int ret;
  platform_driver_register(&bat_driver);
  ret = misc_register(&batradio_dev);
  if (ret) {
    printk(KERN_INFO "  Failed to register misc device\n");
    return ret;
  }
  printk(KERN_INFO "  Started my driver.... \n");
  return 0;
}

void myexit(void)
{
  misc_register(&batradio_dev);
  platform_driver_unregister(&bat_driver);
  printk(KERN_INFO "  Stopped my driver... \n");
  return;
}

module_init(myinit);
module_exit(myexit);

MODULE_DESCRIPTION("FIQ handler for the batradio hat on RPI zero");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
