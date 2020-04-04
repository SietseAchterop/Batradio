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

int init_bat(void)
{
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
