/*
 * Freescale MXS power debug
 *
 * Copyright (c) 2015 Stefan Wahren
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/debugfs.h>
#include <linux/power/mxs_power.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#ifdef CONFIG_DEBUG_FS

static int
mxs_power_ctrl_mx28_show(struct seq_file *s, void *what)
{
	struct mxs_power_data *data = s->private;
	u32 value;
	int ret = regmap_read(data->regmap, HW_POWER_5VCTRL, &value);

	if (ret)
		return ret;

	seq_printf(s, "PSWITCH_MID_TRAN: %x\n", (value >> 27) & 1);
	seq_printf(s, "DCDC4P2_BO_IRQ: %x\n", (value >> 24) & 1);
	seq_printf(s, "ENIRQ_DCDC4P2_BO: %x\n", (value >> 23) & 1);
	seq_printf(s, "VDD5V_DROOP_IRQ: %x\n", (value >> 22) & 1);
	seq_printf(s, "ENIRQ_VDD5V_DROOP: %x\n", (value >> 21) & 1);
	seq_printf(s, "PSWITCH_IRQ: %x\n", (value >> 20) & 1);
	seq_printf(s, "PSWITCH_IRQ_SRC: %x\n", (value >> 19) & 1);
	seq_printf(s, "POLARITY_PSWITCH: %x\n", (value >> 18) & 1);
	seq_printf(s, "ENIRQ_PSWITCH: %x\n", (value >> 17) & 1);
	seq_printf(s, "POLARITY_DC_OK: %x\n", (value >> 16) & 1);
	seq_printf(s, "DC_OK_IRQ: %x\n", (value >> 15) & 1);
	seq_printf(s, "ENIRQ_DC_OK: %x\n", (value >> 14) & 1);
	seq_printf(s, "BATT_BO_IRQ: %x\n", (value >> 13) & 1);
	seq_printf(s, "ENIRQBATT_BO: %x\n", (value >> 12) & 1);
	seq_printf(s, "VDDIO_BO_IRQ: %x\n", (value >> 11) & 1);
	seq_printf(s, "ENIRQ_VDDIO_BO: %x\n", (value >> 10) & 1);
	seq_printf(s, "VDDA_BO_IRQ: %x\n", (value >> 9) & 1);
	seq_printf(s, "ENIRQ_VDDA_BO: %x\n", (value >> 8) & 1);
	seq_printf(s, "VDDD_BO_IRQ: %x\n", (value >> 7) & 1);
	seq_printf(s, "ENIRQ_VDDD_BO: %x\n", (value >> 6) & 1);
	seq_printf(s, "POLARITY_VBUSVALID: %x\n", (value >> 5) & 1);
	seq_printf(s, "VBUSVALID_IRQ: %x\n", (value >> 4) & 1);
	seq_printf(s, "ENIRQ_VBUS_VALID: %x\n", (value >> 3) & 1);
	seq_printf(s, "POLARITY_VDD5V_GT_VDDIO: %x\n", (value >> 2) & 1);
	seq_printf(s, "VDD5V_GT_VDDIO_IRQ: %x\n", (value >> 1) & 1);
	seq_printf(s, "ENIRQ_VDD5V_GT_VDDIO: %x\n", value & 1);

	return 0;
}

static int
mxs_power_5vctrl_mx28_show(struct seq_file *s, void *what)
{
	struct mxs_power_data *data = s->private;
	u32 value;
	int ret = regmap_read(data->regmap, HW_POWER_5VCTRL, &value);

	if (ret)
		return ret;

	seq_printf(s, "VBUSDROOP_TRSH: %x\n", (value >> 28) & 3);
	seq_printf(s, "HEADROOM_ADJ: %x\n", (value >> 24) & 7);
	seq_printf(s, "PWD_CHARGE_4P2: %x\n", (value >> 20) & 3);
	seq_printf(s, "CHARGE_4P2_ILIMIT: %x\n", (value >> 12) & 0x3F);
	seq_printf(s, "VBUSVALID_TRSH: %x\n", (value >> 8) & 7);
	seq_printf(s, "PWDN_5VBRNOUT: %x\n", (value >> 7) & 1);
	seq_printf(s, "ENABLE_LINREG_ILIMIT: %x\n", (value >> 6) & 1);
	seq_printf(s, "DCDC_XFER: %x\n", (value >> 5) & 1);
	seq_printf(s, "VBUSVALID_5VDETECT: %x\n", (value >> 4) & 1);
	seq_printf(s, "VBUSVALID_TO_B: %x\n", (value >> 3) & 1);
	seq_printf(s, "ILIMIT_EQ_ZERO: %x\n", (value >> 2) & 1);
	seq_printf(s, "PWRUP_VBUS_CMPS: %x\n", (value >> 1) & 1);
	seq_printf(s, "ENABLE_DCDC: %x\n", value & 1);

	return 0;
}

static int
mxs_power_vddd_mx28_show(struct seq_file *s, void *what)
{
	struct mxs_power_data *data = s->private;
	u32 value;
	int ret = regmap_read(data->regmap, HW_POWER_VDDDCTRL, &value);

	if (ret)
		return ret;

	seq_printf(s, "ADJTN: %x\n", (value >> 28) & 0xf);
	seq_printf(s, "PWDN_BRNOUT: %x\n", (value >> 23) & 1);
	seq_printf(s, "DISABLE_STEPPING: %x\n", (value >> 22) & 1);
	seq_printf(s, "ENABLE_LINREG: %x\n", (value >> 21) & 1);
	seq_printf(s, "DISABLE_FET: %x\n", (value >> 20) & 1);
	seq_printf(s, "LINREG_OFFSET: %x\n", (value >> 16) & 3);
	seq_printf(s, "BO_OFFSET: %x\n", (value >> 8) & 7);
	seq_printf(s, "TRG: %x\n", value & 0x1f);

	return 0;
}

static int
mxs_power_vdda_mx28_show(struct seq_file *s, void *what)
{
	struct mxs_power_data *data = s->private;
	u32 value;
	int ret = regmap_read(data->regmap, HW_POWER_VDDACTRL, &value);

	if (ret)
		return ret;

	seq_printf(s, "PWDN_BRNOUT: %x\n", (value >> 19) & 1);
	seq_printf(s, "DISABLE_STEPPING: %x\n", (value >> 18) & 1);
	seq_printf(s, "ENABLE_LINREG: %x\n", (value >> 17) & 1);
	seq_printf(s, "DISABLE_FET: %x\n", (value >> 16) & 1);
	seq_printf(s, "LINREG_OFFSET: %x\n", (value >> 12) & 3);
	seq_printf(s, "BO_OFFSET: %x\n", (value >> 8) & 7);
	seq_printf(s, "TRG: %x\n", value & 0x1f);

	return 0;
}

static int
mxs_power_vddio_mx28_show(struct seq_file *s, void *what)
{
	struct mxs_power_data *data = s->private;
	u32 value;
	int ret = regmap_read(data->regmap, HW_POWER_VDDIOCTRL, &value);

	if (ret)
		return ret;

	seq_printf(s, "ADJTN: %x\n", (value >> 20) & 0xf);
	seq_printf(s, "PWDN_BRNOUT: %x\n", (value >> 18) & 1);
	seq_printf(s, "DISABLE_STEPPING: %x\n", (value >> 17) & 1);
	seq_printf(s, "DISABLE_FET: %x\n", (value >> 16) & 1);
	seq_printf(s, "LINREG_OFFSET: %x\n", (value >> 12) & 3);
	seq_printf(s, "BO_OFFSET: %x\n", (value >> 8) & 7);
	seq_printf(s, "TRG: %x\n", value & 0x1f);

	return 0;
}

static int
mxs_power_sts_mx28_show(struct seq_file *s, void *what)
{
	struct mxs_power_data *data = s->private;
	u32 value;
	int ret = regmap_read(data->regmap, HW_POWER_STS, &value);

	if (ret)
		return ret;

	seq_printf(s, "PWRUP_SOURCE %x\n", (value >> 24) & 0x1F);
	seq_printf(s, "PSWITCH %x\n", (value >> 20) & 3);
	seq_printf(s, "THERMAL_WARNING %x\n", (value >> 19) & 1);
	seq_printf(s, "VDDMEM_BO %x\n", (value >> 18) & 1);
	seq_printf(s, "AVALID0_STATUS %x\n", (value >> 17) & 1);
	seq_printf(s, "BVALID0_STATUS %x\n", (value >> 16) & 1);
	seq_printf(s, "VBUSVALID0_STATUS %x\n", (value >> 15) & 1);
	seq_printf(s, "SESSEND0_STATUS %x\n", (value >> 14) & 1);
	seq_printf(s, "BATT_BO %x\n", (value >> 13) & 1);
	seq_printf(s, "VDD5V_FAULT %x\n", (value >> 12) & 1);
	seq_printf(s, "CHRGSTS %x\n", (value >> 11) & 1);
	seq_printf(s, "DCDC_4P2_BO %x\n", (value >> 10) & 1);
	seq_printf(s, "DC_OK %x\n", (value >> 9) & 1);
	seq_printf(s, "VDDIO_BO %x\n", (value >> 8) & 1);
	seq_printf(s, "VDDA_BO %x\n", (value >> 7) & 1);
	seq_printf(s, "VDDD_BO %x\n", (value >> 6) & 1);
	seq_printf(s, "VDD5V_GT_VDDIO %x\n", (value >> 5) & 1);
	seq_printf(s, "VDD5V_DROOP %x\n", (value >> 4) & 1);
	seq_printf(s, "AVALID0 %x\n", (value >> 3) & 1);
	seq_printf(s, "BVALID0 %x\n", (value >> 2) & 1);
	seq_printf(s, "VBUSVALID0 %x\n", (value >> 1) & 1);
	seq_printf(s, "SESSEND0 %x\n", value & 1);

	return 0;
}

static int
mxs_power_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, mxs_power_ctrl_mx28_show, inode->i_private);
}

static int
mxs_power_5vctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, mxs_power_5vctrl_mx28_show, inode->i_private);
}

static int
mxs_power_vddd_open(struct inode *inode, struct file *file)
{
	return single_open(file, mxs_power_vddd_mx28_show, inode->i_private);
}

static int
mxs_power_vdda_open(struct inode *inode, struct file *file)
{
	return single_open(file, mxs_power_vdda_mx28_show, inode->i_private);
}

static int
mxs_power_vddio_open(struct inode *inode, struct file *file)
{
	return single_open(file, mxs_power_vddio_mx28_show, inode->i_private);
}

static int
mxs_power_sts_open(struct inode *inode, struct file *file)
{
	return single_open(file, mxs_power_sts_mx28_show, inode->i_private);
}

static const struct file_operations mxs_power_ctrl_ops = {
	.open = mxs_power_ctrl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations mxs_power_5vctrl_ops = {
	.open = mxs_power_5vctrl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations mxs_power_vddd_ops = {
	.open = mxs_power_vddd_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations mxs_power_vdda_ops = {
	.open = mxs_power_vdda_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations mxs_power_vddio_ops = {
	.open = mxs_power_vddio_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations mxs_power_sts_ops = {
	.open = mxs_power_sts_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void
mxs_power_init_device_debugfs(struct mxs_power_data *data)
{
	struct dentry *device_root;

	device_root = debugfs_create_dir("mxs_power", NULL);
	data->device_root = device_root;

	if (IS_ERR(device_root) || !device_root) {
		pr_warn("failed to create debugfs directory for %s\n",
			"mxs_power");
		return;
	}
	debugfs_create_file("ctrl", S_IFREG | S_IRUGO, device_root, data,
			    &mxs_power_ctrl_ops);
	debugfs_create_file("5vctrl", S_IFREG | S_IRUGO, device_root, data,
			    &mxs_power_5vctrl_ops);
	debugfs_create_file("vddd", S_IFREG | S_IRUGO, device_root, data,
			    &mxs_power_vddd_ops);
	debugfs_create_file("vdda", S_IFREG | S_IRUGO, device_root, data,
			    &mxs_power_vdda_ops);
	debugfs_create_file("vddio", S_IFREG | S_IRUGO, device_root, data,
			    &mxs_power_vddio_ops);
	debugfs_create_file("sts", S_IFREG | S_IRUGO, device_root, data,
			    &mxs_power_sts_ops);
}

void
mxs_power_remove_device_debugfs(struct mxs_power_data *data)
{
	debugfs_remove_recursive(data->device_root);
}

#else /* CONFIG_DEBUG_FS */

void
mxs_power_init_device_debugfs(struct mxs_power_data *data)
{
}

void
mxs_power_remove_device_debugfs(struct mxs_power_data *data)
{
}

#endif

