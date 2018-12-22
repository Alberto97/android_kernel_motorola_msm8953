/*
* Simple driver for Intersil Semiconductor ISL98611GWL Backlight driver chip
* Copyright (C) 2015 MMI
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/pwm.h>
#include <linux/platform_data/leds-isl98611.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/notifier.h>
#include <linux/fb.h>

#define ISL98611_NAME				"isl98611"
#define ISL98611_QUEUE_NAME			"backlight-workqueue"
#define ISL98611_MAX_BRIGHTNESS			255
#define ISL98611_DEFAULT_BRIGHTNESS		255
#define ISL98611_HBM_ON_BRIGHTNESS		(ISL98611_MAX_BRIGHTNESS + 2)
#define ISL98611_HBM_OFF_BRIGHTNESS		(ISL98611_MAX_BRIGHTNESS + 1)
#define ISL98611_DEFAULT_TRIGGER		"bkl-trigger"
#define ISL98611_DEFAULT_VN_LEVEL		20
#define ISL98611_DEFAULT_VP_LEVEL		20
#define ISL98611_DEFAULT_PFM			0x87
#define ISL98611_8BITPWM			0x00
#define ISL98611_10BITPWM			0x80
#define ISL98611_100SCALE			63
#define ISL98611_90p62SCALE			57
#define ISL98611_DC_ONLY			0x00
#define ISL98611_20MA				0x00
#define ISL98611_25MA				0x80
#define ISL98611_30MA				0xC0

#define REG_STATUS		0x01
#define REG_ENABLE		0x02
#define REG_VPVNHEAD		0x03
#define REG_VPLEVEL		0x04
#define REG_VNLEVEL		0x05
#define REG_BOOSTCTRL		0x06
#define REG_VNPFM		0x09
#define REG_BRGHT_MSB		0x10
#define REG_BRGHT_LSB		0x11
#define REG_CURRENT		0x12
#define REG_DIMMCTRL		0x13
#define REG_PWMCTRL		0x14
#define REG_PFMCTRL		0x15
#define REG_VLEDFREQ		0x16
#define REG_VLEDCONFIG		0x17
#define REG_MAX			0x17

#define VPLEVEL_MASK		0x1F
#define VNLEVEL_MASK		0x1F
#define RESET_MASK		0x80
#define VPEN_MASK		0x04
#define VNEN_MASK		0x02
#define CABC_MASK		0xF0
#define PWMRES_MASK		0x80
#define CURRENT_MASK		0xC0
#define SCALE_MASK		0x3F
#define TRANS_THRESHOLD_MASK	0x07
#define PFM_MASK		0xFF
#define BRGHT_LSB_MASK		0x07
#define VLED_EN_MASK		0x08
#define RESET_MASK		0x80

#define VLED_ON_VAL		0x08
#define VLED_OFF_VAL		0x00
#define VPON_VAL		0x04
#define VNON_VAL		0x02
#define VPOFF_VAL		0x00
#define VNOFF_VAL		0x00
#define RESET_VAL		0x00
#define NO_RESET_VAL		0x80
#define CABC_VAL		0x80

struct isl98611_chip {
	struct isl98611_platform_data *pdata;
	struct regmap *regmap;
	struct pwm_device *pwmd;
	struct device *dev;
	struct workqueue_struct *ledwq;
	struct work_struct ledwork;
	struct led_classdev cdev;
	struct notifier_block fb_notif;
};

static const struct regmap_config isl98611_regmap = {
	.name = ISL98611_NAME,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
	.cache_type = REGCACHE_NONE,
};

/* i2c access */
static int isl98611_read(struct isl98611_chip *pchip, unsigned int reg)
{
	int rval;
	unsigned int reg_val;

	rval = regmap_read(pchip->regmap, reg, &reg_val);
	if (rval < 0)
		return rval;
	return reg_val & 0xFF;
}

static int isl98611_write(struct isl98611_chip *pchip,
			 unsigned int reg, unsigned int data)
{
	int rc;
	rc = regmap_write(pchip->regmap, reg, data);
	if (rc < 0)
		dev_err(pchip->dev, "i2c failed to write reg %#x", reg);
	return rc;
}

static int isl98611_update(struct isl98611_chip *pchip,
			  unsigned int reg, unsigned int mask,
			  unsigned int data)
{
	int rc;
	rc = regmap_update_bits(pchip->regmap, reg, mask, data);
	if (rc < 0)
		dev_err(pchip->dev, "i2c failed to update reg %#x", reg);
	return rc;
}

/* initialize chip */
static int isl98611_chip_init(struct isl98611_chip *pchip)
{
	int rval = 0;
	struct isl98611_platform_data *pdata = pchip->pdata;

	if (!pdata->no_reset)
		rval |= isl98611_update(pchip, REG_ENABLE,
			RESET_MASK, RESET_VAL);

	if (pdata->cabc_off)
		isl98611_update(pchip, REG_DIMMCTRL, CABC_MASK, 0x00);

	/* set VN/VP power supply levels */
	if (pdata->vp_level != 0) {
		rval |= isl98611_update(pchip, REG_VPLEVEL,
			VPLEVEL_MASK, pdata->vp_level);
		rval |= isl98611_update(pchip, REG_ENABLE, VPEN_MASK, VPON_VAL);
	} else
		rval |= isl98611_update(pchip, REG_ENABLE,
			VPEN_MASK, VPOFF_VAL);

	if (pdata->vn_level != 0) {
		rval |= isl98611_update(pchip, REG_VNLEVEL,
			VNLEVEL_MASK, pdata->vn_level);
		rval |= isl98611_update(pchip, REG_ENABLE, VNEN_MASK, VNON_VAL);
	} else
		rval |= isl98611_update(pchip, REG_ENABLE,
			VNEN_MASK, VNOFF_VAL);

	rval |= isl98611_update(pchip, REG_CURRENT,
			CURRENT_MASK, pdata->led_current);
	rval |= isl98611_update(pchip, REG_CURRENT,
			SCALE_MASK, pdata->cur_scale);

	if (pdata->pfm_value != ISL98611_DEFAULT_PFM)
		rval |= isl98611_update(pchip, REG_PFMCTRL,
			PFM_MASK, pdata->pfm_value);

	/* LSB brightness is set to 0x7 by default. Clear it */
	rval |= isl98611_update(pchip, REG_BRGHT_LSB, BRGHT_LSB_MASK, 0);

	rval |= isl98611_update(pchip, REG_DIMMCTRL,
		TRANS_THRESHOLD_MASK, pdata->dimm_threshold);

	return rval;
}

/* set brightness */
static void isl98611_brightness_set(struct work_struct *work)
{
	struct isl98611_chip *pchip = container_of(work,
		struct isl98611_chip, ledwork);
	unsigned int level = pchip->cdev.brightness;
	static int old_level = -1;
	struct isl98611_platform_data *pdata = pchip->pdata;
	int rc;

	if (pdata->hbm_on && (level > ISL98611_MAX_BRIGHTNESS)
		&& (-1 != old_level)) {
		int scale, base, rval = 0;
		static int pre_hbm_level = -1;
		switch (level) {
		case ISL98611_HBM_OFF_BRIGHTNESS:
			if (-1 == pre_hbm_level) {
				dev_err(pchip->dev, "Wrong HBM sequence");
				return;
			}
			base = pdata->led_current;
			scale = pdata->cur_scale;
			level = pre_hbm_level;
			dev_info(pchip->dev, "HBM OFF level %d", pre_hbm_level);
		break;
		case ISL98611_HBM_ON_BRIGHTNESS:
			base = pdata->hbm_led_current;
			scale = pdata->hbm_cur_scale;
			pre_hbm_level = old_level;
			level = ISL98611_MAX_BRIGHTNESS;
			dev_info(pchip->dev, "HBM ON level %d", pre_hbm_level);
		break;
		default:
			dev_err(pchip->dev, "HBM error level %d", level);
			return;
		break;
		}
		rval |= isl98611_update(pchip, REG_CURRENT, CURRENT_MASK, base);
		rval |= isl98611_update(pchip, REG_CURRENT, SCALE_MASK, scale);
		if (rval)
			dev_err(pchip->dev, "Falied HBM update rval %d", rval);
	}

	/* set configure pwm input on first brightness command */
	if (old_level == -1 && !pdata->cabc_off) {
		dev_info(pchip->dev, "Enabling CABC");
		isl98611_update(pchip, REG_PWMCTRL,
			PWMRES_MASK, pdata->pwm_res);
		isl98611_update(pchip, REG_DIMMCTRL, CABC_MASK, CABC_VAL);
	}

	if (level != old_level && old_level == 0) {
		rc = isl98611_update(pchip, REG_ENABLE,
			VLED_EN_MASK | RESET_MASK, VLED_ON_VAL | NO_RESET_VAL);
		printk_ratelimited(KERN_INFO
			"isl98611 backlight on %s\n", (rc ? "FAILED" : ""));
	}
	 else if (level == 0 && old_level != 0) {
		rc = isl98611_update(pchip, REG_ENABLE,
			VLED_EN_MASK | RESET_MASK, VLED_OFF_VAL | NO_RESET_VAL);
		printk_ratelimited(KERN_INFO
			"isl98611 backlight off %s\n", (rc ? "FAILED" : ""));
	}
	old_level = level;

	isl98611_write(pchip, REG_BRGHT_MSB, level);

	return;
}

static enum led_brightness isl98611_led_get(struct led_classdev *led_cdev)
{
	struct isl98611_chip *pchip;
	int rc;

	pchip = container_of(led_cdev, struct isl98611_chip, cdev);
	rc = isl98611_read(pchip, REG_BRGHT_MSB);
	return led_cdev->brightness;
}

static void isl98611_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct isl98611_chip *pchip;

	pchip = container_of(led_cdev, struct isl98611_chip, cdev);
	if (value < LED_OFF) {
		dev_err(pchip->dev, "Invalid brightness value");
		return;
	}

	led_cdev->brightness = value;
	queue_work(pchip->ledwq, &pchip->ledwork);
}


#ifdef CONFIG_OF
static int isl98611_dt_init(struct i2c_client *client,
	struct isl98611_platform_data *pdata)
{
	struct device_node *np = client->dev.of_node;
	int rc;

	rc = of_property_read_string(np, "linux,name", &pdata->name);
	if (rc) {
		dev_err(&client->dev, "No linux name provided");
		return rc;
	}

	pdata->trigger = ISL98611_DEFAULT_TRIGGER;
	of_property_read_string(np, "linux,default-trigger", &pdata->trigger);

	pdata->no_reset = of_property_read_bool(np, "intersil,no-reset");

	pdata->cabc_off = of_property_read_bool(np, "intersil,cabc-disable");

	pdata->hbm_on = of_property_read_bool(np, "intersil,hbm-on");

	pdata->default_on = of_property_read_bool(np, "intersil,default-on");
	pdata->init_level = ISL98611_DEFAULT_BRIGHTNESS;
	if (pdata->default_on)
		of_property_read_u32(np, "intersil,init-level",
			&pdata->init_level);

	pdata->vp_level = ISL98611_DEFAULT_VP_LEVEL;
	of_property_read_u32(np, "intersil,vp", &pdata->vp_level);

	pdata->vn_level = ISL98611_DEFAULT_VN_LEVEL;
	of_property_read_u32(np, "intersil,vn", &pdata->vn_level);

	pdata->led_current = ISL98611_25MA;
	of_property_read_u32(np, "intersil,led-current", &pdata->led_current);

	pdata->cur_scale = ISL98611_90p62SCALE;
	of_property_read_u32(np, "intersil,current-scale", &pdata->cur_scale);

	pdata->pfm_value = ISL98611_DEFAULT_PFM;
	of_property_read_u32(np, "intersil,pfm-value", &pdata->pfm_value);

	if (pdata->hbm_on) {
		pdata->hbm_led_current = pdata->led_current;
		of_property_read_u32(np, "intersil,hbm-led-current",
			&pdata->hbm_led_current);

		pdata->hbm_cur_scale = pdata->cur_scale;
		of_property_read_u32(np, "intersil,hbm-current-scale",
			&pdata->hbm_cur_scale);
	}

	pdata->pwm_res = ISL98611_10BITPWM;
	of_property_read_u32(np, "intersil,pwm-resolution", &pdata->pwm_res);

	pdata->dimm_threshold = ISL98611_DC_ONLY;
	of_property_read_u32(np, "intersil,dimm-threshold",
		&pdata->dimm_threshold);

	/* get regulator  to power the I2C switch */
	pdata->supply_name = NULL;
	rc = of_property_read_string(np, "intersil,switch-supply",
		&pdata->supply_name);
	dev_info(&client->dev, "I2C switch supply: %s\n",
		(rc ? "not provided" : pdata->supply_name));

	return 0;

}

static const struct of_device_id of_isl98611_leds_match[] = {
	{ .compatible = "intersil,isl98611", },
	{},
};

#else
static int isl98611_dt_init(struct i2c_client *client,
	struct isl98611_platform_data *pdata)
{
	return ERR_PTR(-ENODEV);
}

static const struct of_device_id of_isl98611_leds_match;

#endif

static int isl98611_fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	struct isl98611_chip *pchip = container_of(self, struct isl98611_chip,
		fb_notif);
	int blank;

	dev_dbg(pchip->dev, "%s+\n", __func__);

	blank = *(int *)evdata->data;

	if (event == FB_EVENT_BLANK && blank == FB_BLANK_UNBLANK) {
		int regval;
		regval = isl98611_read(pchip, REG_BRGHT_LSB);
		if (regval) {
			dev_err(pchip->dev,
				"%s: REG_BRGHT_LSB is an unexpected value: %d\n",
				__func__, regval);
			dev_err(pchip->dev, "%s: Re-initialize the chip\n",
				__func__);
			pchip->cdev.brightness = 0;
			isl98611_write(pchip, REG_BRGHT_MSB, 0);
			isl98611_chip_init(pchip);
		}
	}

	dev_dbg(pchip->dev, "%s-\n", __func__);

	return 0;
}

static int isl98611_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct isl98611_platform_data *pdata = dev_get_platdata(&client->dev);
	struct isl98611_chip *pchip;
	int rc;
	unsigned int status;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "fail : i2c functionality check");
		return -EOPNOTSUPP;
	}

	pchip = devm_kzalloc(&client->dev, sizeof(struct isl98611_chip),
			     GFP_KERNEL);
	if (!pchip)
		return -ENOMEM;

	if (!pdata) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct isl98611_platform_data), GFP_KERNEL);
		if (pdata == NULL)
			return -ENOMEM;

		rc = isl98611_dt_init(client, pdata);
		if (rc)
			return rc;
	}

	/* if configured request/enable the regulator for the I2C switch */
	if (pdata->supply_name) {
		pdata->vreg = devm_regulator_get(&client->dev,
			pdata->supply_name);
		if (IS_ERR(pdata->vreg)) {
			if (PTR_ERR(pdata->vreg) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			else
				return PTR_ERR(pdata->vreg);
		}
		rc = regulator_enable(pdata->vreg);
		if (rc) {
			dev_err(&client->dev, "regulator %s enable err %d\n",
				pdata->supply_name, rc);
			return rc;
		}
	}


	i2c_set_clientdata(client, pchip);
	pchip->pdata = pdata;
	pchip->dev = &client->dev;

	pchip->regmap = devm_regmap_init_i2c(client, &isl98611_regmap);
	if (IS_ERR(pchip->regmap)) {
		rc = PTR_ERR(pchip->regmap);
		dev_err(&client->dev, "fail : allocate reg. map: %d", rc);
		return rc;
	}

	/* chip initialize */
	rc = isl98611_chip_init(pchip);
	if (rc < 0) {
		dev_err(&client->dev, "fail : init chip");
		return rc;
	}

	/* led classdev register */
	pchip->cdev.brightness_set = isl98611_led_set;
	pchip->cdev.brightness_get = isl98611_led_get;
	if (pdata->hbm_on)
		pchip->cdev.max_brightness = ISL98611_HBM_ON_BRIGHTNESS;
	else
		pchip->cdev.max_brightness = ISL98611_MAX_BRIGHTNESS;
	pchip->cdev.name = pdata->name;
	pchip->cdev.default_trigger = pdata->trigger;

	/* need single thread queue to keep requests in sequence */
	pchip->ledwq = create_singlethread_workqueue(ISL98611_QUEUE_NAME);
	if (!pchip->ledwq) {
		rc = -ENOMEM;
		dev_err(&client->dev, "unable to create workqueue");
		return rc;
	}

	INIT_WORK(&pchip->ledwork, isl98611_brightness_set);

	rc = led_classdev_register(&client->dev, &pchip->cdev);
	if (rc) {
		dev_err(&client->dev, "unable to register led rc=%d", rc);
		goto classerr;
	}

	if (pdata->default_on)
		isl98611_led_set(&pchip->cdev, pdata->init_level);

	pchip->fb_notif.notifier_call = isl98611_fb_notifier_callback;
	rc = fb_register_client(&pchip->fb_notif);
	if (rc) {
		dev_err(&client->dev,
			"Error registering fb_notifier: %d\n", rc);
		goto fberr;
	}

	status = isl98611_read(pchip, REG_STATUS);
	dev_info(pchip->dev, "probe success chip hw status %#x", status);

	return 0;

fberr:
	led_classdev_unregister(&pchip->cdev);
classerr:
	destroy_workqueue(pchip->ledwq);
	return rc;
}

static int isl98611_remove(struct i2c_client *client)
{
	struct isl98611_chip *pchip = i2c_get_clientdata(client);

	fb_unregister_client(&pchip->fb_notif);
	led_classdev_unregister(&pchip->cdev);
	destroy_workqueue(pchip->ledwq);

	return 0;
}

static const struct i2c_device_id isl98611_id[] = {
	{ISL98611_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, isl98611_id);

static struct i2c_driver isl98611_i2c_driver = {
	.driver = {
		.name = ISL98611_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(of_isl98611_leds_match),
		  },
	.probe = isl98611_probe,
	.remove = isl98611_remove,
	.id_table = isl98611_id,
};

module_i2c_driver(isl98611_i2c_driver);

MODULE_DESCRIPTION("Intersil  Backlight driver for ISL98611");
MODULE_LICENSE("GPL v2");
