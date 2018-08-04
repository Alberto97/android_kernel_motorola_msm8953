/*
 * Copyright (C) 2015 Motorola Mobility LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/printk.h>
#include <linux/clk.h>
#include <linux/mods/usb_ext_bridge.h>
#ifdef CONFIG_FSUSB42_MUX
#include <linux/fsusb42.h>
#endif

#define USB_ATTACH 0xAA55
#define CFG_ACCESS 0x9937
#define HS_P2_BOOST 0x68CA

#define HS_BOOST_MAX 0x07

static unsigned int boost_val = 0x05;
module_param(boost_val, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(boost_val, "Boost Value for the USB3813 hub");

struct usb3813_info {
	struct i2c_client *client;
	struct device *dev;
	struct gpio hub_reset_n;
	struct gpio hub_int_n;
	struct clk *hub_clk;
	bool   hub_enabled;
	struct delayed_work usb3813_attach_work;
	struct mutex	i2c_mutex;
	struct dentry *debug_root;
	u16 debug_address;
	bool debug_enabled;
	bool debug_attach;
	struct notifier_block usbext_notifier;
	bool enable_controller;
	bool switch_controller;
};

static int usb3813_write_command(struct usb3813_info *info, u16 command)
{
	struct i2c_client *client = to_i2c_client(info->dev);
	struct i2c_msg msg[1];
	u8 data[3];
	int ret;

	data[0] = (command >> 8) & 0xFF;
	data[1] = command & 0xFF;
	data[2] = 0x00;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	mutex_lock(&info->i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&info->i2c_mutex);

	/* i2c_transfer returns number of messages transferred */
	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;

	return 0;
}

static int usb3813_write_cfg_reg(struct usb3813_info *info, u16 reg, u8 val)
{
	struct i2c_client *client = to_i2c_client(info->dev);
	struct i2c_msg msg[1];
	u8 data[8];
	int ret;

	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x05;
	data[3] = 0x00;
	data[4] = 0x01;
	data[5] = (reg >> 8) & 0xFF;
	data[6] = reg & 0xFF;
	data[7] = val;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	mutex_lock(&info->i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&info->i2c_mutex);

	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;

	return usb3813_write_command(info, CFG_ACCESS);
}

static int usb3813_read_cfg_reg(struct usb3813_info *info, u16 reg)
{
	struct i2c_client *client = to_i2c_client(info->dev);
	struct i2c_msg msg[2];
	u8 data[7];
	u8 val[2];
	int ret;

	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x04;
	data[3] = 0x01;
	data[4] = 0x01;
	data[5] = (reg >> 8) & 0xFF;
	data[6] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	mutex_lock(&info->i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, 1);
	mutex_unlock(&info->i2c_mutex);

	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;

	ret = usb3813_write_command(info, CFG_ACCESS);
	if (ret < 0)
		return ret;

	data[0] = 0x00;
	data[1] = 0x04;
	msg[0].len = 2;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = ARRAY_SIZE(val);

	mutex_lock(&info->i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&info->i2c_mutex);

	if (ret < 0)
		return ret;

	return val[1];
}

static void disable_usbc(struct usb3813_info *info, bool disable)
{
	struct power_supply *usbc_psy;
	union power_supply_propval prop = {0,};
	int rc;

	usbc_psy = power_supply_get_by_name("usbc");
	if (!usbc_psy)
		return;

	prop.intval = disable;
	rc = usbc_psy->set_property(usbc_psy,
		POWER_SUPPLY_PROP_DISABLE_USB,
		&prop);
	if (rc < 0)
		dev_err(info->dev,
			"could not set USBC disable property, rc=%d\n", rc);

	power_supply_put(usbc_psy);
}

static int usb3813_host_enable(struct usb3813_info *info, bool enable)
{
	struct power_supply *usb_psy;

	if (info->enable_controller) {

		usb_psy = power_supply_get_by_name("usb_host");
		if (!usb_psy)
			return -ENODEV;

		if (enable)
			power_supply_set_usb_otg(usb_psy, 1);
		else
			power_supply_set_usb_otg(usb_psy, 0);

		power_supply_put(usb_psy);
	} else if (info->switch_controller) {
		usb_psy = power_supply_get_by_name("usb");
		if (!usb_psy)
			return -ENODEV;

		if (enable) {
			disable_usbc(info, true);
#ifdef CONFIG_FSUSB42_MUX
			fsusb42_set_state(FSUSB_STATE_EXT);
#endif
			power_supply_set_usb_otg(usb_psy, 1);
		} else {
			power_supply_set_usb_otg(usb_psy, 0);
#ifdef CONFIG_FSUSB42_MUX
			fsusb42_set_state(FSUSB_OFF);
#endif
			disable_usbc(info, false);
		}

		power_supply_put(usb_psy);
	}

	return 0;
}

static int usb3813_hub_attach(struct usb3813_info *info, bool enable)
{
	if (enable == info->hub_enabled)
		return 0;

	dev_info(info->dev, "%sabling the USB38138 hub\n",
			enable ? "En" : "Dis");
	info->hub_enabled = enable;

	if (info->hub_enabled) {
		usb3813_host_enable(info, true);
		if (clk_prepare_enable(info->hub_clk)) {
			dev_err(info->dev, "%s: failed to prepare clock\n",
				__func__);
			return -EFAULT;
		}
		gpio_set_value(info->hub_reset_n.gpio, 1);
		schedule_delayed_work(&info->usb3813_attach_work,
					msecs_to_jiffies(1000));
	} else {
		gpio_set_value(info->hub_reset_n.gpio, 0);
		clk_disable_unprepare(info->hub_clk);
		usb3813_host_enable(info, false);
	}

	return 0;
}

static ssize_t usb3813_enable_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct usb3813_info *info = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->hub_enabled);
}

static ssize_t usb3813_enable_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct usb3813_info *info = dev_get_drvdata(dev);
	unsigned long r, mode;

	r = kstrtoul(buf, 0, &mode);
	if (r) {
		dev_err(dev, "Invalid value = %lu\n", mode);
		return -EINVAL;
	}

	mode = !!mode;

	if (usb3813_hub_attach(info, mode))
		return -EFAULT;
	else
		return count;
}

static DEVICE_ATTR(enable, 0660, usb3813_enable_show, usb3813_enable_store);

static void usb3813_attach_w(struct work_struct *work)
{
	struct usb3813_info *info =
		container_of(work, struct usb3813_info,
		usb3813_attach_work.work);
	int ret;

	if (!info->hub_enabled)
		return;

	ret = usb3813_write_cfg_reg(info, HS_P2_BOOST, boost_val);
	if (ret < 0)
		dev_err(info->dev, "Write HS_P2_BOOST failed (%d)\n", ret);

	ret = usb3813_write_command(info, USB_ATTACH);
	if (ret < 0) {
		dev_err(info->dev, "USB_ATTCH failed (%d)\n", ret);
	}

}

static int get_reg(void *data, u64 *val)
{
	struct usb3813_info *info = data;
	u8 temp = 0;

	if (!info->debug_enabled) {
		dev_err(info->dev, "Enable hub debug before access\n");
		return -ENODEV;
	}

	temp = usb3813_read_cfg_reg(info, info->debug_address);
	*val = temp;

	return 0;
}

static int set_reg(void *data, u64 val)
{
	struct usb3813_info *info = data;
	u8 temp;
	int ret;

	if (!info->debug_enabled) {
		dev_err(info->dev, "Enable hub debug before access\n");
		return -ENODEV;
	}

	temp = (u8) val;
	ret = usb3813_write_cfg_reg(info, info->debug_address, temp);
	if (ret < 0)
		dev_err(info->dev, "Write to 0x%04x failed (%d)\n",
						info->debug_address,
						ret);
	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(usb3813_reg_fops, get_reg, set_reg, "0x%02llx\n");

static int usb3813_dbg_enable_show(void *data, u64 *val)
{
	struct usb3813_info *info = data;

	*val = (u64)info->debug_enabled;
	return 0;
}

static int usb3813_dbg_enable_write(void *data, u64 val)
{
	struct usb3813_info *info = data;

	if (info->hub_enabled) {
		dev_err(info->dev, "Cannot enable debugging, HUB active\n");
		return -EBUSY;
	}

	if (val) {
		info->debug_enabled = true;
		if (clk_prepare_enable(info->hub_clk)) {
			dev_err(info->dev, "%s: failed to prepare clock\n",
								__func__);
			return -EFAULT;
		}
		gpio_set_value(info->hub_reset_n.gpio, 1);
		info->debug_enabled = true;
	} else {
		gpio_set_value(info->hub_reset_n.gpio, 0);
		clk_disable_unprepare(info->hub_clk);
		info->debug_enabled = false;
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(usb3813_dbg_fops, usb3813_dbg_enable_show,
		usb3813_dbg_enable_write, "%llu\n");

static int usb3813_dbg_attach_write(void *data, u64 val)
{
	struct usb3813_info *info = data;
	int ret = 0;

	if (!info->debug_enabled) {
		dev_err(info->dev, "Debug not enabled\n");
		return -EINVAL;
	}

	if (val)
		ret = usb3813_write_command(info, USB_ATTACH);
	else {
		gpio_set_value(info->hub_reset_n.gpio, 0);
		mdelay(10);
		gpio_set_value(info->hub_reset_n.gpio, 1);
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(usb3813_attach_fops, NULL,
		usb3813_dbg_attach_write, "%llu\n");

int usb3813_debug_init(struct usb3813_info *info)
{
	struct dentry *ent;

	info->debug_root = debugfs_create_dir("usb3813", NULL);

	if (!info->debug_root) {
		dev_err(info->dev, "Couldn't create debug dir\n");
		return -EINVAL;
	}

	ent = debugfs_create_x16("address", S_IFREG | S_IWUSR | S_IRUGO,
				info->debug_root, &(info->debug_address));
	if (!ent) {
		dev_err(info->dev, "Error creating address entry\n");
		return -EINVAL;
	}

	ent = debugfs_create_file("data", S_IFREG | S_IWUSR | S_IRUGO,
				info->debug_root, info, &usb3813_reg_fops);
	if (!ent) {
		dev_err(info->dev, "Error creating data entry\n");
		return -EINVAL;
	}

	ent = debugfs_create_file("enable_dbg", S_IFREG | S_IWUSR | S_IRUGO,
				info->debug_root, info, &usb3813_dbg_fops);
	if (!ent) {
		dev_err(info->dev, "Error creating enable_dbg entry\n");
		return -EINVAL;
	}

	ent = debugfs_create_file("attach", S_IFREG | S_IWUSR | S_IRUGO,
				info->debug_root, info, &usb3813_attach_fops);
	if (!ent) {
		dev_err(info->dev, "Error creating attach entry\n");
		return -EINVAL;
	}

	return 0;
}

static int usb_ext_notifier_call(struct notifier_block *nb,
		unsigned long val,
		void *v)
{
	struct usb3813_info *info = container_of(nb,
				struct usb3813_info, usbext_notifier);

	struct usb_ext_status *status = v;

	/* If Mod has detached, disconnect the hub */
	if (status->proto == USB_EXT_PROTO_UNKNOWN && !val) {
		usb3813_hub_attach(info, 0);
		return NOTIFY_DONE;
	}

	/* We don't care about anything other than USB2.0 Host */
	if (status->proto != USB_EXT_PROTO_2_0 ||
		status->type != USB_EXT_REMOTE_DEVICE)
		return NOTIFY_DONE;

	usb3813_hub_attach(info, !!val);
	return NOTIFY_OK;
}

static int usb3813_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	int ret = 0;
	enum of_gpio_flags flags;
	struct usb3813_info *info;
	struct device_node *np = client->dev.of_node;

	if (!np) {
		dev_err(&client->dev, "No OF DT node found.\n");
		return -ENODEV;
	}

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	info->dev = &client->dev;
	i2c_set_clientdata(client, info);

	mutex_init(&info->i2c_mutex);
	info->hub_enabled = 0;

	info->hub_reset_n.gpio = of_get_gpio_flags(np, 0, &flags);
	info->hub_reset_n.flags = flags;
	of_property_read_string_index(np, "gpio-labels", 0,
				      &info->hub_reset_n.label);
	dev_dbg(&client->dev, "GPIO: %d  FLAGS: %ld  LABEL: %s\n",
		info->hub_reset_n.gpio, info->hub_reset_n.flags,
		info->hub_reset_n.label);

	ret = gpio_request_one(info->hub_reset_n.gpio,
			       info->hub_reset_n.flags,
			       info->hub_reset_n.label);
	if (ret) {
		dev_err(&client->dev, "failed to request GPIO\n");
		goto fail_info;
	}

	ret = gpio_export(info->hub_reset_n.gpio, 1);
	if (ret)
		dev_err(&client->dev, "Failed to export GPIO %s: %d\n",
			info->hub_reset_n.label,
			info->hub_reset_n.gpio);

	ret = gpio_export_link(&client->dev,
		       info->hub_reset_n.label,
		       info->hub_reset_n.gpio);
	if (ret)
		dev_err(&client->dev, "Failed to link GPIO %s: %d\n",
			info->hub_reset_n.label,
			info->hub_reset_n.gpio);

	info->hub_int_n.gpio = of_get_gpio_flags(np, 1, &flags);
	info->hub_int_n.flags = flags;
	of_property_read_string_index(np, "gpio-labels", 1,
				      &info->hub_int_n.label);
	dev_dbg(&client->dev, "GPIO: %d  FLAGS: %ld  LABEL: %s\n",
		info->hub_int_n.gpio, info->hub_int_n.flags,
		info->hub_int_n.label);

	ret = gpio_request_one(info->hub_int_n.gpio,
			       info->hub_int_n.flags,
			       info->hub_int_n.label);
	if (ret) {
		dev_err(&client->dev, "failed to request GPIO\n");
		goto fail_gpio_reset;
	}

	ret = gpio_export(info->hub_int_n.gpio, 1);
	if (ret)
		dev_err(&client->dev, "Failed to export GPIO %s: %d\n",
			info->hub_int_n.label,
			info->hub_int_n.gpio);

	ret = gpio_export_link(&client->dev,
		       info->hub_int_n.label,
		       info->hub_int_n.gpio);
	if (ret)
		dev_err(&client->dev, "Failed to link GPIO %s: %d\n",
			info->hub_int_n.label,
			info->hub_int_n.gpio);

	info->hub_clk = devm_clk_get(&client->dev, "hub_clk");
	if (IS_ERR(info->hub_clk)) {
		dev_err(&client->dev, "%s: failed to get clock.\n", __func__);
		ret = PTR_ERR(info->hub_clk);
		goto fail_gpio;
	}

	info->enable_controller = of_property_read_bool(np, "enable-usbhost");
	info->switch_controller = of_property_read_bool(np, "switch-usbhost");

	INIT_DELAYED_WORK(&info->usb3813_attach_work, usb3813_attach_w);

	info->usbext_notifier.notifier_call = usb_ext_notifier_call;
	ret = usb_ext_register_notifier(&info->usbext_notifier, NULL);
	if (ret) {
		dev_err(&client->dev, "couldn't register usb ext notifier\n");
		goto fail_clk;
	}

	ret = device_create_file(&client->dev, &dev_attr_enable);
	if (ret) {
		dev_err(&client->dev, "Unable to create enable file\n");
		goto fail_usb_ext;
	}

	usb3813_debug_init(info);

	dev_info(&client->dev, "Done probing usb3813\n");
	return 0;

fail_usb_ext:
	usb_ext_unregister_notifier(&info->usbext_notifier);
fail_clk:
	clk_put(info->hub_clk);
fail_gpio:
	gpio_free(info->hub_int_n.gpio);
fail_gpio_reset:
	gpio_free(info->hub_reset_n.gpio);
fail_info:
	kfree(info);
	return ret;
}

static int usb3813_remove(struct i2c_client *client)
{
	struct usb3813_info *info = i2c_get_clientdata(client);

	cancel_delayed_work(&info->usb3813_attach_work);
	usb_ext_unregister_notifier(&info->usbext_notifier);
	gpio_free(info->hub_reset_n.gpio);
	gpio_free(info->hub_int_n.gpio);
	if (info->hub_enabled)
		clk_disable_unprepare(info->hub_clk);
	clk_put(info->hub_clk);
	device_remove_file(&client->dev, &dev_attr_enable);
	debugfs_remove_recursive(info->debug_root);
	kfree(info);
	return 0;
}

static const struct of_device_id usb3813_of_tbl[] = {
	{ .compatible = "microchip,usb3813", .data = NULL},
	{},
};

static const struct i2c_device_id usb3813_id[] = {
	{"usb3813-hub", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, usb3813_id);

static struct i2c_driver usb3813_driver = {
	.driver		= {
		.name		= "usb3813",
		.owner		= THIS_MODULE,
		.of_match_table	= usb3813_of_tbl,
	},
	.probe		= usb3813_probe,
	.remove		= usb3813_remove,
	.id_table	= usb3813_id,
};
module_i2c_driver(usb3813_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Motorola Mobility LLC");
MODULE_DESCRIPTION("usb3813-hub driver");
MODULE_ALIAS("i2c:usb3813-hub");
