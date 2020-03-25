#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static int my_probe(struct platform_device *pdev)
{
pr_info("funcion %s() called\n", __func__);
return 0;
}

static int my_remove(struct platform_device *pdev)
{
pr_info("function %s() called\n", __func__);
return 0;
}

static const struct of_device_id my_of_id_table[] =
  {
   { .compatible = "brcm,bcm2835" },
   { }
  };

static struct platform_driver my_driver =
  {
   .driver = {
	      .name = "batradio",
	      .of_match_table = my_of_id_table,
	      },
   .probe = my_probe,
   .remove = my_remove,
  };

module_platform_driver(my_driver);

MODULE_DESCRIPTION("Test platform driver");
MODULE_AUTHOR("Sietse Achterop");
MODULE_LICENSE("GPL");
