/*
 * Single Wire driver for Diodes AL3050 Backlight driver chip
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
#define T_LOGIC_NS		T_LOGIC_1_NS + T_LOGIC_0_NS
#define BRIGHTNESS_MAX		31
#define BRIGHTNESS_BMASK	0x1f
#define RFA_BMASK		0x80
#define RFA_ACK_WAIT            3500
#define RFA_MAX_ACK_TIME        900000





struct al3050_platform_data {

	struct device *fbdev;
};

struct al3050_bl_data {
	struct device *fbdev;
	struct gpio_desc *gpiod;
	int last_brightness;
	int power;
        int rfa_en;
};

static void al3050_init(struct backlight_device *bl)
{
        struct al3050_bl_data *pchip = bl_get_data(bl);
        unsigned long flags;
        local_irq_save(flags);
        gpiod_direction_output(pchip->gpiod,0);
        mdelay(T_RESET_MS);
        gpiod_direction_output(pchip->gpiod,1);
        ndelay(T_DELAY_NS);
        gpiod_direction_output(pchip->gpiod,0);
        ndelay(T_DETECTION_NS);
        gpiod_direction_output(pchip->gpiod,1);
        local_irq_restore(flags);


}


static int al3050_backlight_set_value(struct backlight_device *bl)
{
        struct al3050_bl_data *pchip = bl_get_data(bl);
        unsigned int data, max_bmask, addr_bmask;
        unsigned long flags;

        max_bmask = 0x1 << 16;
        /* command size */
        addr_bmask = max_bmask >> 8;


	data = ADDRESS_AL3050 | (bl->props.brightness & BRIGHTNESS_BMASK);

        if (pchip->rfa_en)
		data |= RFA_BMASK;


	/* t_start : 4us high before data byte */
        local_irq_save(flags);
	gpiod_direction_output(pchip->gpiod,1);
	ndelay(T_START_NS);

	for (max_bmask >>= 1; max_bmask > 0x0; max_bmask >>= 1) {
		int t_low;
		if(data & max_bmask)
			t_low = T_LOGIC_1_NS;
		else
			t_low = T_LOGIC_0_NS;

		gpiod_direction_output(pchip->gpiod,0);
		ndelay(t_low);
		gpiod_direction_output(pchip->gpiod,1);
		ndelay(T_LOGIC_NS - t_low);

		if (max_bmask == addr_bmask) {
				gpiod_direction_output(pchip->gpiod, 0);
				/* t_eos : low after address byte */
				ndelay(T_EOS_NS);
				gpiod_direction_output(pchip->gpiod, 1);
				/* t_start : high before data byte */
				ndelay(T_START_NS);
			}


	}
	gpiod_direction_output(pchip->gpiod,0);

	/* t_eos : 4us low after address byte */

	ndelay(T_EOS_NS);

        if (pchip->rfa_en) {
		int max_ack_time = RFA_MAX_ACK_TIME;
		gpiod_direction_input(pchip->gpiod);
		/* read acknowledge from chip */
		while (max_ack_time > 0) {
			if (gpiod_get_value(pchip->gpiod) == 0)
				break;
			max_ack_time -= RFA_ACK_WAIT;
		}
		if (max_ack_time <= 0) {
			dev_err(pchip->fbdev,"AL3050 : no ack \n"); 
                        al3050_init(bl);
                       }
		else
			ndelay(max_ack_time);
	}
	gpiod_direction_output(pchip->gpiod, 1);
	local_irq_restore(flags);

	pchip->last_brightness = bl->props.brightness;

	return 0;
}


static int al3050_backlight_update_status(struct backlight_device *bl)
{
        struct al3050_bl_data *pchip = bl_get_data(bl);


        if (bl->props.power != FB_BLANK_UNBLANK ||
            bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK)) 
        {

                        gpiod_direction_output(pchip->gpiod,0);
                        bl->props.brightness = 0;
                        pchip->power = 1;
                        return 0;
        }

        else {

                if (pchip->power == 1)  
                        {
                        printk(KERN_ERR "AL3050 init.");
                        al3050_init(bl);
                        pchip->power = bl->props.power;
                        bl->props.brightness = pchip->last_brightness;
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
	struct al3050_bl_data *pchip = bl_get_data(bl);

	return pchip->fbdev == NULL || pchip->fbdev == info->dev;
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

{       struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct al3050_bl_data *pchip;
	struct backlight_properties props;
	struct al3050_platform_data *pdata = dev_get_platdata(dev);
	struct backlight_device *bl;
	int ret;
        u32 value;

	pchip = devm_kzalloc(dev, sizeof(*pchip), GFP_KERNEL);

	if (pchip == NULL)
		return -ENOMEM;


	if(pdata)
		pchip->fbdev = pdata->fbdev;

	pchip->gpiod = devm_gpiod_get(dev, NULL, GPIOD_ASIS);
		if (IS_ERR(pchip->gpiod)) {
			ret = PTR_ERR(pchip->gpiod);
			if (ret != -EPROBE_DEFER)
				dev_err(dev,"Error: The gpios parameter is missing or invalid.\n");
			return ret;
	}

        ret = of_property_read_u32(node, "rfa_en", &value);
	if (ret < 0)
		pchip->rfa_en = 0;
	pchip->rfa_en = value;


	memset(&props, 0, sizeof(props));
	props.brightness = BRIGHTNESS_MAX;
	props.max_brightness = BRIGHTNESS_MAX;
	props.type = BACKLIGHT_RAW;
	pchip->power = 0;
	bl = devm_backlight_device_register
	 (dev, dev_name(dev), dev, pchip, &al3050_bl_ops, &props);

	if (IS_ERR(bl))
			return PTR_ERR(bl);



	platform_set_drvdata(pdev, bl);

	/* Single Wire init *
	 * Single Wire Detection Window
	 *   - detect delay : 100us
	 *   - detect time  : 450us
	 */

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
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:al3050_bl");
