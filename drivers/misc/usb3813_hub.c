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
#include <linux/regulator/consumer.h>
#include <linux/mods/usb_ext_bridge.h>
#include <linux/platform_data/slimport_device.h>

#define USB_ATTACH 0xAA55
#define CFG_ACCESS 0x9937
#define HS_P2_BOOST 0x68CA

#define HS_BOOST_MAX 0x07

static unsigned int boost_val = 0x05;
module_param(boost_val, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(boost_val, "Boost Value for the USB3813 hub");

static bool ignore_typec;
module_param(ignore_typec, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(ignore_typec, "Ignore TypeC when enabling Hub");

struct usb3813_info {
	struct i2c_client *client;
	struct device *dev;
	struct gpio hub_reset_n;
	struct gpio hub_int_n;
	struct clk *hub_clk;
	bool   hub_enabled;
	struct delayed_work usb3813_attach_work;
	struct mutex	i2c_mutex;
	struct mutex	enable_mutex;
	struct dentry *debug_root;
	u16 debug_address;
	bool debug_enabled;
	bool debug_attach;
	struct notifier_block usbext_notifier;
	struct notifier_block psy_notifier;
	bool enable_controller;
	bool switch_controller;
	struct regulator *vdd_hsic;
	bool hsic_enabled;
	struct power_supply *usb_psy;
	bool   mod_enabled;
	struct usb_ext_status ext_state;
	struct delayed_work usb3813_enable_work;
	bool   factory_mode;
};

static int set_hsic_state(struct usb3813_info *info, bool enable);

/* Returns the status of USB device/host connected via Type-C */
static bool is_typec_usb_present(struct usb3813_info *info)
{
	if (power_supply_get_usb_owner(info->usb_psy) == PSY_USB_OWNER_USBC)
		return true;
	else
		return false;
}

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

static int usb3813_device_enable(struct usb3813_info *info, bool enable)
{
	struct power_supply *usb_psy = info->usb_psy;

	dev_dbg(info->dev, "%s - enable = %d\n", __func__, enable);

	if (enable) {
		/* Device mode is supported only over the USB3 i/f */
		power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_EXT_3);
		/* If EXT took ownership, switch to device mode */
		if (power_supply_get_usb_owner(usb_psy) == PSY_USB_OWNER_EXT_3) {
			power_supply_set_supply_type(usb_psy,
					POWER_SUPPLY_TYPE_USB);
			power_supply_set_present(usb_psy, 1);
		} else
			return -EBUSY;
	} else if (power_supply_get_usb_owner(usb_psy) != PSY_USB_OWNER_USBC) {
		power_supply_set_supply_type(usb_psy,
				POWER_SUPPLY_TYPE_UNKNOWN);
		power_supply_set_present(usb_psy, 0);
		power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_NONE);
	}

	return 0;
}

static int usb3813_2_0_host_enable(struct usb3813_info *info, bool enable)
{
	struct power_supply *usb_psy;

	dev_dbg(info->dev, "%s - enable = %d\n", __func__, enable);

	if (info->enable_controller) {

		/* Enable Dedicated Controller Only Once */
		usb_psy = power_supply_get_by_name("usb_host");
		if (!usb_psy)
			return -ENODEV;

		if (enable)
			power_supply_set_usb_otg(usb_psy,
					POWER_SUPPLY_USB_OTG_ENABLE_DATA);
		else
			power_supply_set_usb_otg(usb_psy,
					POWER_SUPPLY_USB_OTG_DISABLE);

		power_supply_put(usb_psy);
	} else if (info->switch_controller) {
		usb_psy = info->usb_psy;

		if (enable) {
			/* Set the owner to EXT_2 since this is a USB2 host */
			power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_EXT_2);
			/* If EXT took ownership, switch to host mode */
			if (power_supply_get_usb_owner(usb_psy) ==
						PSY_USB_OWNER_EXT_2)
				power_supply_set_usb_otg(usb_psy,
					POWER_SUPPLY_USB_OTG_ENABLE_DATA);
			else
				return -EBUSY;
		} else if (power_supply_get_usb_owner(usb_psy) !=
						PSY_USB_OWNER_USBC) {
			/* Disable if USBC has not already taken over */
			power_supply_set_usb_otg(usb_psy,
					POWER_SUPPLY_USB_OTG_DISABLE);
			power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_NONE);
		}
	}
	return 0;
}

static int usb3813_3_1_host_enable(struct usb3813_info *info, bool enable)
{
	struct power_supply *usb_psy = info->usb_psy;

	dev_dbg(info->dev, "%s - enable = %d\n", __func__, enable);

	if (enable) {
		/* Set the owner to USB3 */
		power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_EXT_3);
		/* If EXT took ownership, switch to host mode */
		if (power_supply_get_usb_owner(usb_psy) ==
					PSY_USB_OWNER_EXT_3)
			power_supply_set_usb_otg(usb_psy,
				POWER_SUPPLY_USB_OTG_ENABLE_DATA);
		else
			return -EBUSY;
	} else if (power_supply_get_usb_owner(usb_psy) !=
					PSY_USB_OWNER_USBC) {
		/* Disable if USBC has not already taken over */
		power_supply_set_usb_otg(usb_psy,
				POWER_SUPPLY_USB_OTG_DISABLE);
		power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_NONE);
	}

	return 0;
}

static int usb3813_dual_host_enable(struct usb3813_info *info, bool enable)
{
	struct power_supply *usb_psy = info->usb_psy;
	struct power_supply *usbhost_psy;

	dev_dbg(info->dev, "%s - enable = %d\n", __func__, enable);

	/* First handle the secondary controller if available */
	if (info->enable_controller) {

		/* Enable/Disable the  Dedicated Secondary Controller */
		usbhost_psy = power_supply_get_by_name("usb_host");
		if (!usbhost_psy) {
			dev_err(info->dev, "%s - unable to find usbhost\n",
					__func__);
			return -ENODEV;
		}

		if (enable)
			power_supply_set_usb_otg(usbhost_psy,
					POWER_SUPPLY_USB_OTG_ENABLE_DATA);
		else
			power_supply_set_usb_otg(usbhost_psy,
					POWER_SUPPLY_USB_OTG_DISABLE);
	}

	/* Now handle the Primary USB controller */
	if (enable) {
		/* Set the owner to USBEXT DUAL */
		power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_EXT_DUAL);
		/* If EXT took ownership, switch to host mode */
		if (power_supply_get_usb_owner(usb_psy) ==
					PSY_USB_OWNER_EXT_DUAL)
			power_supply_set_usb_otg(usb_psy,
				POWER_SUPPLY_USB_OTG_ENABLE_DATA);
		else
			return -EBUSY;
	} else if (power_supply_get_usb_owner(usb_psy) !=
					PSY_USB_OWNER_USBC) {
		/* Disable if USBC has not already taken over */
		power_supply_set_usb_otg(usb_psy,
				POWER_SUPPLY_USB_OTG_DISABLE);
		power_supply_set_usb_owner(usb_psy, PSY_USB_OWNER_NONE);
	}

	return 0;
}

static int usb3813_host_enable(struct usb3813_info *info, bool enable)
{
	dev_dbg(info->dev, "%s - enable = %d, proto = %d\n",
			__func__, enable, info->ext_state.proto);

	/* Enable the appropriate controller based on protocol */
	if (info->ext_state.proto == USB_EXT_PROTO_2_0)
		return usb3813_2_0_host_enable(info, enable);
	else if (info->ext_state.proto == USB_EXT_PROTO_3_1)
		return usb3813_3_1_host_enable(info, enable);
	else if (info->ext_state.proto == USB_EXT_PROTO_DUAL)
		return usb3813_dual_host_enable(info, enable);
	else
		return -EINVAL;
}

static void usb3813_send_uevent(struct usb3813_info *info)
{
	struct kobj_uevent_env *env;
	bool usb_present = is_typec_usb_present(info);

	env = kzalloc(sizeof(*env), GFP_KERNEL);
	if (!env)
		return;

	if (info->ext_state.active)
		add_uevent_var(env, "EXT_USB_STATE=ATTACHED");
	else
		add_uevent_var(env, "EXT_USB_STATE=DETACHED");

	if (info->mod_enabled)
		add_uevent_var(env, "EXT_USB=ALLOWED");
	else
		add_uevent_var(env, "EXT_USB=DISABLED");

	if (info->ext_state.proto == USB_EXT_PROTO_2_0)
		add_uevent_var(env, "EXT_USB_PROTOCOL=USB2.0");
	else if (info->ext_state.proto == USB_EXT_PROTO_3_1)
		add_uevent_var(env, "EXT_USB_PROTOCOL=USB3.1");
	else if (info->ext_state.proto == USB_EXT_PROTO_DUAL)
		add_uevent_var(env, "EXT_USB_PROTOCOL=DUAL_SPEED USB");

	if (info->ext_state.type == USB_EXT_REMOTE_DEVICE)
		add_uevent_var(env, "EXT_USB_MODE=DEVICE");
	else if (info->ext_state.type == USB_EXT_REMOTE_HOST)
		add_uevent_var(env, "EXT_USB_MODE=HOST");

	add_uevent_var(env, "MAIN_USB=ALLOWED");

	if (usb_present)
		add_uevent_var(env, "MAIN_USB_STATE=ATTACHED");
	else
		add_uevent_var(env, "MAIN_USB_STATE=DETACHED");

	kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, env->envp);
	kfree(env);
}

static int usb3813_hub_attach(struct usb3813_info *info, bool enable)
{
	if (enable == info->hub_enabled)
		return 0;

	dev_info(info->dev, "%sabling the USB38138 hub\n",
			enable ? "En" : "Dis");

	info->hub_enabled = enable;

	if (info->hub_enabled) {
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

#define MAX_WAIT_COUNT 100
static ssize_t usb3813_enable_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct usb3813_info *info = dev_get_drvdata(dev);
	unsigned long r, mode;
	int wait_count = 0;

	r = kstrtoul(buf, 0, &mode);
	if (r) {
		dev_err(dev, "Invalid value = %lu\n", mode);
		return -EINVAL;
	}

	mode = !!mode;

	if (mode == info->hub_enabled)
		return -EINVAL;

	/* Disable Type C first when enabling*/
	if (info->switch_controller && mode) {
		disable_usbc(info, true);
		/* Wait until ownership changes */
		while (is_typec_usb_present(info) &&
				wait_count++ < MAX_WAIT_COUNT)
			msleep(20);

		if (wait_count >= MAX_WAIT_COUNT)
			dev_err(dev, "changing ownership timed out\n");
	}

	if (usb3813_2_0_host_enable(info, mode)) {
		dev_err(dev, "Host %sable failed\n", mode ? "En" : "Dis");
		return -EFAULT;
	}

	if (usb3813_hub_attach(info, mode)) {
		dev_err(dev, "Hub %stach failed\n", mode ? "At" : "De");
		return -EFAULT;
	}

	/* Enable Type C last when disabling*/
	if (info->switch_controller && !mode)
		disable_usbc(info, false);

	return count;
}

static DEVICE_ATTR(enable, 0660, usb3813_enable_show, usb3813_enable_store);

static ssize_t usb3813_hsic_vdd_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct usb3813_info *info = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->hsic_enabled);
}

static ssize_t usb3813_hsic_vdd_store(struct device *dev,
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

	if (set_hsic_state(info, mode))
		return -EFAULT;
	else
		return count;
}

static DEVICE_ATTR(hsic_vdd, 0660,
			usb3813_hsic_vdd_show, usb3813_hsic_vdd_store);

static void usb3813_attach_w(struct work_struct *work)
{
	struct usb3813_info *info =
		container_of(work, struct usb3813_info,
		usb3813_attach_work.work);
	int ret;

	if (!info->hub_enabled)
		return;

	dev_dbg(info->dev, "%s\n", __func__);
	/* Reset the slimport since USB2 shares lines */
	slimport_reset_standby();

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

static int set_hsic_state(struct usb3813_info *info, bool enable)
{
	int ret;

	if (!info->vdd_hsic)
		return 0;

	if (enable && !info->hsic_enabled) {
		ret = regulator_enable(info->vdd_hsic);

		if (ret)
			dev_err(info->dev, "Unable to enable vdd_hsic\n");
		else
			info->hsic_enabled = true;

	} else if (!enable && info->hsic_enabled) {
		ret = regulator_disable(info->vdd_hsic);

		if (ret)
			dev_err(info->dev, "Unable to disable vdd_hsic\n");
		else
			info->hsic_enabled = false;
	}

	return ret;
}

static void usb3813_enable_w(struct work_struct *work)
{
	struct usb3813_info *info =
		container_of(work, struct usb3813_info,
		usb3813_enable_work.work);
	bool enable = info->mod_enabled;

	mutex_lock(&info->enable_mutex);
	dev_dbg(info->dev, "%s - mod_enabled  = %d, mod_attached = %d\n",
		__func__, enable, info->ext_state.active);

	/* Enable host or device based on the type */
	if (info->ext_state.type == USB_EXT_REMOTE_DEVICE)
		usb3813_host_enable(info, enable);
	else
		usb3813_device_enable(info, enable);

	/* Enable HSIC if path is Bridge */
	if (info->ext_state.proto == USB_EXT_PROTO_2_0 &&
			info->ext_state.path == USB_EXT_PATH_BRIDGE)
		set_hsic_state(info, enable);

	/* Enable the Hub if it is USB2.0 */
	if (info->ext_state.proto == USB_EXT_PROTO_2_0 ||
		info->ext_state.proto == USB_EXT_PROTO_DUAL)
		usb3813_hub_attach(info, enable);

	usb3813_send_uevent(info);
	mutex_unlock(&info->enable_mutex);
}

static int usb3813_validate_ext_params(struct usb_ext_status *status)
{
	if (status->proto == USB_EXT_PROTO_DUAL) {
		/* For Dual speed devices, we only work in host mode */
		if (status->type != USB_EXT_REMOTE_DEVICE)
			return -EINVAL;
	} else if (status->proto == USB_EXT_PROTO_2_0) {
		/*
		 * We support both Enterprise and Bridge as the path for
		 * USB 2.0 Ext Class, but only if the remote is a USB
		 * peripheral.
		 */
		if (status->path == USB_EXT_PATH_UNKNOWN)
			return -EINVAL;
		else if (status->type != USB_EXT_REMOTE_DEVICE)
			return -EINVAL;
	} else if (status->proto == USB_EXT_PROTO_3_1) {
		/*
		 * For USB 3.1 Ext Class, both USB hosts and
		 * peripherals are supported.
		 */
		if (status->type == USB_EXT_REMOTE_UNKNOWN)
			return -EINVAL;
	} else
		return -EINVAL;

	return 0;
}

static int usb_ext_notifier_call(struct notifier_block *nb,
		unsigned long val,
		void *v)
{
	struct usb3813_info *info = container_of(nb,
				struct usb3813_info, usbext_notifier);
	struct usb_ext_status *status = v;

	dev_info(info->dev, "%s - val = %lu, proto = %d, type = %d, path = %d\n",
			__func__, val,
			status->proto, status->type, status->path);


	if (val == info->ext_state.active) {
		dev_dbg(info->dev, "Spurious EXT notification\n");
		return NOTIFY_DONE;
	}

	if (val) {
		if (usb3813_validate_ext_params(status)) {
			dev_err(info->dev, "Invalid EXT parameters\n");
			return NOTIFY_DONE;
		}
		memcpy(&info->ext_state, status, sizeof(struct usb_ext_status));
	} else
		info->ext_state.active = 0;

	if (!ignore_typec && is_typec_usb_present(info)) {
		dev_info(info->dev, "Type C USB present, Ignore EXT\n");
		/* Notify User space of conflict */
		usb3813_send_uevent(info);
		return NOTIFY_OK;
	}

	cancel_delayed_work_sync(&info->usb3813_enable_work);
	info->mod_enabled = info->ext_state.active;
	usb3813_enable_w(&info->usb3813_enable_work.work);

	return NOTIFY_OK;
}

static int psy_notifier_call(struct notifier_block *nb,
		unsigned long val,
		void *v)
{
	struct usb3813_info *info = container_of(nb,
				struct usb3813_info, psy_notifier);
	struct power_supply *psy = v;
	bool usb_present;
	bool work = 0;

	if (val != PSY_EVENT_PROP_CHANGED || !psy)
		return NOTIFY_OK;

	if (ignore_typec || psy != info->usb_psy)
		return NOTIFY_OK;

	/* We don't care about usb events if a mod is not attached */
	if (!info->ext_state.active) {
		dev_dbg(info->dev, "No mod attached, Ignore\n");
		return NOTIFY_OK;
	}

	usb_present = is_typec_usb_present(info);

	dev_info(info->dev, "%s - usb = %d, hub = %d, mod = %d\n",
		__func__, usb_present, info->hub_enabled, info->mod_enabled);

	/*
	 * Detach Mod USB if type C USB is present.
	 * Otherwise if Mod is attached and type C USB is removed,
	 * re-enable the Mod USB if it is not already enabled.
	 */
	if (info->mod_enabled && usb_present) {
		info->mod_enabled = false;
		work = 1;
	} else if (info->ext_state.active && !usb_present &&
				!info->mod_enabled) {
		info->mod_enabled = true;
		work = 1;
	}

	if (work)
		schedule_delayed_work(&info->usb3813_enable_work, 0);

	return NOTIFY_OK;
}

static bool usb3813_factory_mode(void)
{
#ifdef CONFIG_USB3813_DEBUG
	return true;
#else
	struct device_node *np = of_find_node_by_path("/chosen");
	bool factory = false;

	if (np)
		factory = of_property_read_bool(np, "mmi,factory-cable");

	of_node_put(np);

	return factory;
#endif
}

static int usb3813_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	int ret = 0;
	enum of_gpio_flags flags;
	struct usb3813_info *info;
	struct device_node *np = client->dev.of_node;
	struct power_supply *usb_psy;

	if (!np) {
		dev_err(&client->dev, "No OF DT node found.\n");
		return -ENODEV;
	}

	usb_psy = power_supply_get_by_name("usb");
	if (!usb_psy) {
		dev_err(&client->dev, "USB supply not found, deferring probe\n");
		return -EPROBE_DEFER;
	}

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	info->dev = &client->dev;
	i2c_set_clientdata(client, info);

	mutex_init(&info->i2c_mutex);
	mutex_init(&info->enable_mutex);
	info->hub_enabled = 0;
	info->usb_psy = usb_psy;

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
	info->factory_mode = usb3813_factory_mode();

	info->vdd_hsic = devm_regulator_get(&client->dev, "vdd-hsic");
	if (IS_ERR(info->vdd_hsic))
		dev_err(&client->dev, "unable to get hsic supply\n");

	INIT_DELAYED_WORK(&info->usb3813_attach_work, usb3813_attach_w);
	INIT_DELAYED_WORK(&info->usb3813_enable_work, usb3813_enable_w);

	info->usbext_notifier.notifier_call = usb_ext_notifier_call;
	ret = usb_ext_register_notifier(&info->usbext_notifier, NULL);
	if (ret) {
		dev_err(&client->dev, "couldn't register usb ext notifier\n");
		goto fail_clk;
	}

	info->psy_notifier.notifier_call = psy_notifier_call;
	ret = power_supply_reg_notifier(&info->psy_notifier);
	if (ret) {
		dev_err(&client->dev, "Unable to register psy_notifier\n");
		goto fail_usb_ext;
	}

	if (info->factory_mode) {
		ret = device_create_file(&client->dev, &dev_attr_enable);
		if (ret) {
			dev_err(&client->dev, "Unable to create enable file\n");
			goto fail_psy;
		}

		ret = device_create_file(&client->dev, &dev_attr_hsic_vdd);
		if (ret) {
			dev_err(&client->dev, "Unable to create hsic_vdd file\n");
			goto fail_psy;
		}
	}

	usb3813_debug_init(info);

	dev_info(&client->dev, "Done probing usb3813\n");
	return 0;

fail_psy:
	power_supply_unreg_notifier(&info->psy_notifier);
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
	power_supply_put(usb_psy);
	return ret;
}

static int usb3813_remove(struct i2c_client *client)
{
	struct usb3813_info *info = i2c_get_clientdata(client);

	cancel_delayed_work(&info->usb3813_attach_work);
	cancel_delayed_work(&info->usb3813_enable_work);
	usb_ext_unregister_notifier(&info->usbext_notifier);
	power_supply_unreg_notifier(&info->psy_notifier);
	gpio_free(info->hub_reset_n.gpio);
	gpio_free(info->hub_int_n.gpio);
	if (info->hub_enabled)
		clk_disable_unprepare(info->hub_clk);
	clk_put(info->hub_clk);
	devm_regulator_put(info->vdd_hsic);
	if (info->factory_mode) {
		device_remove_file(&client->dev, &dev_attr_enable);
		device_remove_file(&client->dev, &dev_attr_hsic_vdd);
	}
	debugfs_remove_recursive(info->debug_root);
	power_supply_put(info->usb_psy);
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
