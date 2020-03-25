#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

int leds_open(struct inode *inode,struct file *filp)
{
	printk("leds device opened success!\n");
	return 0;
}
 
int leds_release(struct inode *inode,struct file *filp)
{
	printk("leds device closed success!\n");
	return 0;
}
 
long leds_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
	printk("debug: leds_ioctl cmd is %d\n" , cmd);
	return 0;
}
 
static struct file_operations leds_ops = {
	.owner = THIS_MODULE,
	.open = leds_open,
	.release = leds_release,
	.unlocked_ioctl = leds_ioctl,
};
 
static struct miscdevice leds_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.fops = &leds_ops,
	.name = "leds", //This name will be displayed under the /dev directory
 
};
 
 
static int __init leds_init(void)
{
 
	int ret, i;
	char *banner = "leds Initialize\n";
 
	printk(banner);
 
	ret = misc_register(&leds_dev);
 
	if(ret<0)
	{
		printk("leds: register device failed!\n");
		goto exit;
 
	}
 
	return 0;
	exit:
	misc_deregister(&leds_dev);
	return ret;
 
}
 
 
 
static void __exit leds_exit(void)
{
  printk("Removed batradio module\n");
  misc_deregister(&leds_dev);
}
 
module_init(leds_init);
module_exit(leds_exit);
 
MODULE_LICENSE("Dual BSD/GPL");
 
 
 
