#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>

static int batradio_mmap(struct file *file, struct vm_area_struct *vma)
{  return 0; }
static long batradio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{ return 0; }

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
  int ret;
  printk(KERN_INFO "Probe with bat_probe\n");
  //   .....
  ret = misc_register(&batradio_dev);
  return 0;
}

static int bat_remove(struct platform_device *pdev)
{ return 0; }

static struct platform_driver bat_driver = {
	.probe	= bat_probe,
	.remove	= bat_remove,
	.driver = {
		.name = "batfiq",
		.owner = THIS_MODULE,
	},
};

int myinit(void)
{   printk(KERN_INFO "  Start my driver.... \n");
    platform_driver_register(&bat_driver);
    return 0;
}

void myexit(void)
{   printk(KERN_INFO "  Stopped my driver... \n");
    platform_driver_unregister(&bat_driver);
    return;
}

module_init(myinit);
module_exit(myexit);

MODULE_DESCRIPTION("FIQ handler for the batradio hat on RPI zero");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
