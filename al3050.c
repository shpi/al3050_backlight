/*
 * Single Wire driver for Diodes AL3050 backlight driver chip
 *
 * Copyright (C) 2020 SHPI GmbH
 * Author: Lutz Harder  <harder.lutz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * datasheet: https://www.diodes.com/assets/Datasheets/AL3050.pdf
 *
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>


#define ADDRESS_AL3050		0x5800
#define T_DELAY_NS 		100000
#define T_DETECTION_NS 		450000
#define T_START_NS		4000
#define T_EOS_NS		4000
#define T_RESET_MS		4
#define T_LOGIC_1_NS		4000
#define T_LOGIC_0_NS		9000
#define T_LOGIC_NS		13000
#define BRIGHTNESS_MAX		31
#define BRIGHTNESS_BITMASK	0x1f
#define RFA_BMASK		0x80

// RFA inserted later, needs pullup, not in standard GPIOLIB

struct al3050_platform_data {

	struct device *fbdev;
};

struct al3050_bl_data {
	struct device *fbdev;
	struct gpio_desc *gpiod;
	int last_brightness;
	int power;
};

static void al3050_init(struct backlight_device *bl)
{
        struct al3050_bl_data *alb = bl_get_data(bl);

        /* Single Wire init *
         *   - detect delay : 100us
         *   - detect time  : 450us
         */

        gpiod_direction_output(alb->gpiod,0);
        mdelay(T_RESET_MS);
        gpiod_direction_output(alb->gpiod,1);
        ndelay(T_DELAY_NS);
        gpiod_direction_output(alb->gpiod,0);
        ndelay(T_DETECTION_NS);
        gpiod_direction_output(alb->gpiod,1);
}


static int al3050_backlight_set_value(struct backlight_device *bl)
{
        struct al3050_bl_data *alb = bl_get_data(bl);
        unsigned int data, max_bmask, addr_bmask;

        max_bmask = 0x1 << 16;
        /* command size  */
        addr_bmask = max_bmask >> 8;

	data = ADDRESS_AL3050 | (bl->props.brightness & BRIGHTNESS_BITMASK);

	/* t_start : high before adress */

	gpiod_direction_output(alb->gpiod,1);
	ndelay(T_START_NS);

	for (max_bmask >>= 1; max_bmask > 0x0; max_bmask >>= 1) {
		int t_low;
		if(data & max_bmask)
			t_low = T_LOGIC_1_NS;
		else
			t_low = T_LOGIC_0_NS;

		gpiod_direction_output(alb->gpiod,0);
		ndelay(t_low);
		gpiod_direction_output(alb->gpiod,1);
		ndelay(T_LOGIC_NS - t_low);
		if (max_bmask == addr_bmask) {
				gpiod_direction_output(alb->gpiod, 0);
				/* t_eos : low after address  */
				ndelay(T_EOS_NS);
				gpiod_direction_output(alb->gpiod, 1);
				/* t_start : high before data */
				ndelay(T_START_NS);
			}
	}
	gpiod_direction_output(alb->gpiod,0);
	/* t_eos : low after command byte */
	ndelay(T_EOS_NS);
	gpiod_direction_output(alb->gpiod,1);
	alb->last_brightness = bl->props.brightness;
	return bl->props.brightness;
}

static int al3050_backlight_update_status(struct backlight_device *bl)
{
        struct al3050_bl_data *alb = bl_get_data(bl);


        if (bl->props.power != FB_BLANK_UNBLANK ||
            bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
        {
                        gpiod_direction_output(alb->gpiod,0);
                        bl->props.brightness = 0;
                        alb->power = 1;
                        return 0;
        }

        else
	{
                if (alb->power == 1)
                        {
                        printk(KERN_INFO "AL3050 reinit.");
                        al3050_init(bl);
                        alb->power = bl->props.power;
                        bl->props.brightness = alb->last_brightness;
                        al3050_backlight_set_value(bl);
                        return 0;
                        }
                else
                {
                return al3050_backlight_set_value(bl);
                }
        }
}

/*
static int al3050_backlight_check_fb(struct backlight_device *bl,
				   struct fb_info *info)
{
	struct al3050_bl_data *alb = bl_get_data(bl);

	return alb->fbdev == NULL || alb->fbdev == info->dev;
}
*/


static const struct backlight_ops al3050_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= al3050_backlight_update_status,
	//.check_fb	= al3050_backlight_check_fb,
};


static struct of_device_id al3050_backlight_of_match[] = {
	{ .compatible = "al3050_bl" },
	{ }
};

MODULE_DEVICE_TABLE(of, al3050_backlight_of_match);

static int al3050_backlight_probe(struct platform_device *pdev)

{   struct device *dev = &pdev->dev;
	struct al3050_bl_data *alb;
	struct backlight_properties props;
	struct al3050_platform_data *pdata = dev_get_platdata(dev);
	struct backlight_device *bl;
	int ret;

	alb = devm_kzalloc(dev, sizeof(*alb), GFP_KERNEL);
	if (alb == NULL)
		return -ENOMEM;


	if(pdata)
		alb->fbdev = pdata->fbdev;

	alb->gpiod = devm_gpiod_get(dev, NULL, GPIOD_ASIS);
		if (IS_ERR(alb->gpiod)) {
			ret = PTR_ERR(alb->gpiod);
			if (ret != -EPROBE_DEFER)
				dev_err(dev,"Error: The gpios parameter is wrong.\n");
			return ret;
	}

	memset(&props, 0, sizeof(props));
	props.brightness = BRIGHTNESS_MAX;
	props.max_brightness = BRIGHTNESS_MAX;
	props.type = BACKLIGHT_RAW;
	alb->power = 0;
	bl = devm_backlight_device_register
	 (dev, dev_name(dev), dev, alb, &al3050_bl_ops, &props);

	if (IS_ERR(bl))
			return PTR_ERR(bl);

	platform_set_drvdata(pdev, bl);
	al3050_init(bl);
	dev_info(dev,"AL3050 backlight is initialized\n");
	return 0;
}


static struct platform_driver al3050_backlight_driver = {
	.driver		= {
		.name		= "al3050_bl",
		.of_match_table	= al3050_backlight_of_match,
	},
	.probe		= al3050_backlight_probe,
};

module_platform_driver(al3050_backlight_driver);

MODULE_DESCRIPTION("Single Wire AL3050 Backlight Driver");
MODULE_AUTHOR("Lutz Harder, SHPI GmbH");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:al3050_bl");
