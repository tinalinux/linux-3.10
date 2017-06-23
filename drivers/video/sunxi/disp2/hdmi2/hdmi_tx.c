/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/switch.h>

#include "hdmi_tx.h"
/*#include "hdmi_test.h"*/

#define DDC_PIN_ACTIVE "active"
#define DDC_PIN_SLEEP "sleep"

static dev_t devid;
static struct cdev *hdmi_cdev;

static struct class *hdmi_class;
static struct device *hdev;

static struct hdmi_tx_drv	*hdmi_drv;

u32 hdmi_hpd_mask;
static u32 hdmi_clk_enable_mask;
static u32 hdmi_enable_mask;

u32 hdmi_printf;

#if defined(CONFIG_SWITCH) || defined(CONFIG_ANDROID_SWITCH)
static struct switch_dev hdmi_switch_dev = {
	.name = "hdmi",
};
#endif


struct hdmi_tx_drv *get_hdmi_drv(void)
{
	return hdmi_drv;
}


/**
 * @short List of the devices
 * Linked list that contains the installed devices
 */
static LIST_HEAD(devlist_global);

#ifdef CONFIG_AW_AXP
int hdmi_power_enable(char *name)
{
	struct regulator *regu = NULL;
	int ret = -1;

	regu = regulator_get(NULL, name);
	if (IS_ERR(regu)) {
		pr_err("%s: some error happen, fail to get regulator %s\n", __func__, name);
		goto exit;
	}

	/* enalbe regulator */
	ret = regulator_enable(regu);
	if (0 != ret) {
		pr_err("%s: some error happen, fail to enable regulator %s!\n", __func__, name);
		goto exit1;
	} else {
		HDMI_INFO_MSG("suceess to enable regulator %s!\n", name);
	}

exit1:
	/* put regulater, when module exit */
	regulator_put(regu);
exit:
	return ret;
}

int hdmi_power_disable(char *name)
{
	struct regulator *regu = NULL;
	int ret = 0;
	regu = regulator_get(NULL, name);
	if (IS_ERR(regu)) {
		pr_err("%s: some error happen, fail to get regulator %s\n", __func__, name);
		goto exit;
	}

	/*disalbe regulator*/
	ret = regulator_disable(regu);
	if (0 != ret) {
		pr_err("%s: some error happen, fail to disable regulator %s!\n", __func__, name);
		goto exit1;
	} else {
		HDMI_INFO_MSG("suceess to disable regulator %s!\n", name);
	}

exit1:
	/*put regulater, when module exit*/
	regulator_put(regu);
exit:
	return ret;
}
#else
int hdmi_power_enable(char *name) {return 0; }
int hdmi_power_disable(char *name) {return 0; }
#endif


static void hdcp_handler(struct work_struct *work)
{
	LOG_TRACE();
	mutex_lock(&hdmi_drv->hdcp_mutex);

	hdcp_handler_core();

	mutex_unlock(&hdmi_drv->hdcp_mutex);
}

static void hpd_handler(struct work_struct *work)
{
	mutex_lock(&hdmi_drv->hpd_mutex);

#if defined(CONFIG_SWITCH) || defined(CONFIG_ANDROID_SWITCH)
	switch_set_state(&hdmi_switch_dev, hdmi_core_get_hpd_state());
#endif
	hpd_handler_core();

	mutex_unlock(&hdmi_drv->hpd_mutex);
}

static irqreturn_t dwc_hdmi_tx_handler(int irq, void *dev_id)
{
	u32 drv_irq_type;

	drv_irq_type = hdmi_core_get_irq_type();

	switch (drv_irq_type) {
	case HDCP_IRQ:
		pr_info("hdmi tx hdcp interrupt\n");
		queue_work(hdmi_drv->hdmi_workqueue,
				&hdmi_drv->hdcp_work);

		break;

	case HPD_IRQ:
		pr_info("hdmi hpd interrupt\n");
		queue_work(hdmi_drv->hdmi_workqueue,
				&hdmi_drv->hpd_work);
		break;

	default:
		break;
	}

	return IRQ_HANDLED;
}


static int register_interrupts(struct hdmi_tx_drv *dev)
{
	int ret = 0;

	ret = devm_request_threaded_irq(dev->parent_dev, dev->irq,
				dwc_hdmi_tx_handler, NULL, IRQF_SHARED,
				"dwc_hdmi_tx_handler", dev);
	if (ret) {
		pr_err("%s:Could not register dwc_hdmi_tx interrupt,IRQ number=%d\n",
							FUNC_NAME, dev->irq);
	}

	return ret;
}

static void release_interrupts(struct hdmi_tx_drv *dev)
{
	if (dev->irq)
		devm_free_irq(dev->parent_dev, dev->irq, dev);
}

static s32 hdmi_enable(void)
{
	s32 ret = 0;
	struct pinctrl_state *state;

	LOG_TRACE();
	mutex_lock(&hdmi_drv->ctrl_mutex);
	if (hdmi_enable_mask == 1) {
		mutex_unlock(&hdmi_drv->ctrl_mutex);
		return 0;
	}

	/*enable clock*/
	if (hdmi_clk_enable_mask == 0) {
		ret = clk_prepare_enable(hdmi_drv->hdmi_clk);
		if (ret != 0)
			pr_info("hdmi clk enable failed!\n");
		resistor_calibration_core(hdmi_drv->hdmi_core, 0x10004, 0x80c00000);

		ret = clk_prepare_enable(hdmi_drv->hdmi_ddc_clk);
		if (ret != 0)
			HDMI_ERROR_MSG("hdmi ddc clk enable failed!\n");

		hdmi_clk_enable_mask = 1;
	}


	/*pin configuration for ddc*/
	if (hdmi_drv->pctl != NULL) {
		state = pinctrl_lookup_state(hdmi_drv->pctl, DDC_PIN_ACTIVE);
		if (IS_ERR(state))
			pr_info("pinctrl_lookup_state for HDMI2.0 SCL fail\n");
		ret = pinctrl_select_state(hdmi_drv->pctl, state);
		if (ret < 0)
			pr_info("pinctrl_select_state for HDMI2.0 DDC fail\n");
	}

	ret = hdmi_enable_core();

	hdmi_enable_mask = 1;
	mutex_unlock(&hdmi_drv->ctrl_mutex);

	return ret;
}

static s32 hdmi_disable(void)
{
	s32 ret;


	LOG_TRACE();
	mutex_lock(&hdmi_drv->ctrl_mutex);
	if (hdmi_enable_mask == 0) {
		mutex_unlock(&hdmi_drv->ctrl_mutex);
		return 0;
	}

	ret = hdmi_disable_core();

	mutex_unlock(&hdmi_drv->ctrl_mutex);

	return ret;
}
#if defined(CONFIG_SND_SUNXI_SOC_HDMIAUDIO)
s32 hdmi_audio_enable(u8 enable, u8 channel)
{
	s32 ret = 0;

	mutex_lock(&hdmi_drv->ctrl_mutex);
	ret = hdmi_core_audio_enable(enable, channel);
	mutex_unlock(&hdmi_drv->ctrl_mutex);

	return ret;
}
#endif

static s32 hdmi_suspend(void)
{
	s32 ret;
	struct pinctrl_state *state;

	LOG_TRACE();
	mutex_lock(&hdmi_drv->ctrl_mutex);

	/*if (hdmi_enable_mask == 0) {
		mutex_unlock(&hdmi_drv->ctrl_mutex);
		return 0;
	}*/
	/*ret = hdmi_suspend_core();*/
	if (hdmi_clk_enable_mask == 1) {
		clk_disable_unprepare(hdmi_drv->hdmi_clk);
		clk_disable_unprepare(hdmi_drv->hdmi_ddc_clk);

		hdmi_clk_enable_mask = 0;
	}
	/*pin configuration for ddc*/
	if (hdmi_drv->pctl != NULL) {
		state = pinctrl_lookup_state(hdmi_drv->pctl, DDC_PIN_SLEEP);
		if (IS_ERR(state))
			pr_info("pinctrl_lookup_state for HDMI2.0 SCL fail\n");
		ret = pinctrl_select_state(hdmi_drv->pctl, state);
		if (ret < 0)
			pr_info("pinctrl_select_state for HDMI2.0 DDC fail\n");
	}

	hdmi_enable_mask = 0;

	mutex_unlock(&hdmi_drv->ctrl_mutex);

	return ret;
}

static s32 hdmi_resume(void)
{
	s32 ret;
	struct pinctrl_state *state;

	LOG_TRACE();
	mutex_lock(&hdmi_drv->ctrl_mutex);
	/*enable clock*/
	if (hdmi_clk_enable_mask == 0) {
		ret = clk_prepare_enable(hdmi_drv->hdmi_clk);
		if (ret != 0)
			pr_info("hdmi clk enable failed!\n");
		resistor_calibration_core(hdmi_drv->hdmi_core, 0x10004, 0x80c00000);

		ret = clk_prepare_enable(hdmi_drv->hdmi_ddc_clk);
		if (ret != 0)
			HDMI_ERROR_MSG("hdmi ddc clk enable failed!\n");

		hdmi_clk_enable_mask = 1;
	}


	/*pin configuration for ddc*/
	if (hdmi_drv->pctl != NULL) {
		state = pinctrl_lookup_state(hdmi_drv->pctl, DDC_PIN_ACTIVE);
		if (IS_ERR(state))
			pr_info("pinctrl_lookup_state for HDMI2.0 SCL fail\n");
		ret = pinctrl_select_state(hdmi_drv->pctl, state);
		if (ret < 0)
			pr_info("pinctrl_select_state for HDMI2.0 DDC fail\n");
	}

	ret = hdmi_enable_core();
	/*ret = hdmi_resume_core();*/
	hdmi_enable_mask = 1;

	mutex_unlock(&hdmi_drv->ctrl_mutex);

	return ret;
}

static int hdmi_tx_init(struct platform_device *pdev)
{
	int ret = 0;
	int phy_model = 301;
	uintptr_t reg_base = 0x0;
	struct disp_device_func disp_func;
#if defined(CONFIG_SND_SUNXI_SOC_HDMIAUDIO)
	__audio_hdmi_func audio_func;
#if defined(CONFIG_SND_SUNXI_SOC_AUDIOHUB_INTERFACE)
	__audio_hdmi_func audio_func_muti;
#endif
#endif

	/*hdmi_printf = 1;*/
	LOG_TRACE();

	hdmi_drv = kmalloc(sizeof(struct hdmi_tx_drv), GFP_KERNEL);
	if (!hdmi_drv) {
		pr_err("%s:Could not allocated hdmi_tx_dev\n", FUNC_NAME);
		return -ENOMEM;
	}

	memset(hdmi_drv, 0, sizeof(struct hdmi_tx_drv));


	hdmi_drv->pdev = pdev;
	/* Update the device node*/
	hdmi_drv->parent_dev = &pdev->dev;

	 /*value = disp_boot_para_parse("boot_disp");*/


	/* iomap */
	reg_base = (uintptr_t __force)of_iomap(pdev->dev.of_node, 0);
	if (0 == reg_base) {

		pr_err("unable to map hdmi registers\n");
		ret = -EINVAL;
		goto free_mem;
	}
	hdmi_core_set_base_addr(reg_base);
	hdmi_drv->reg_base = reg_base;


	/*Get DCC GPIO*/
	hdmi_drv->pctl = pinctrl_get(&pdev->dev);
	if (IS_ERR(hdmi_drv->pctl))
		pr_err("ERROR: pinctrl_get for HDMI2.0 DDC fail\n");

	/* get clk */
	hdmi_drv->hdmi_clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(hdmi_drv->hdmi_clk)) {
		dev_err(&pdev->dev, "fail to get clk for hdmi\n");
		goto free_mem;
	}

	hdmi_drv->hdmi_ddc_clk = of_clk_get(pdev->dev.of_node, 1);
	if (IS_ERR(hdmi_drv->hdmi_ddc_clk)) {
		dev_err(&pdev->dev, "fail to get clk for hdmi ddc\n");
		goto free_mem;
	}

	hdmi_drv->hdmi_hdcp_clk = of_clk_get(pdev->dev.of_node, 2);
	if (IS_ERR(hdmi_drv->hdmi_hdcp_clk)) {
		dev_err(&pdev->dev, "fail to get clk for hdmi hdcp\n");
		goto free_mem;
	}

	/*pr_info("get hdmi ddc clk: %d", hdmi_ddc_clk->rate_hz);*/
	/*hdmi_cec_clk = of_clk_get(pdev->dev.of_node, 3);
	if (IS_ERR(hdmi_cec_clk)) {
		dev_err(&pdev->dev, "fail to get clk for hdmi ddc\n");
		goto free_mem;
	}*/

	/*ret = disp_sys_script_get_item("hdmi", "hdmi_power", (int*)hdmi_power, 2);
	if (2 == ret) {
		if (hdmi_power_used) {
			pr_info("[HDMI] power %s\n", hdmi_power);
			mutex_lock(&mlock);
			ret = hdmi_power_enable(hdmi_power);
			power_enable_count ++;
			mutex_unlock(&mlock);
			if (0 != ret) {
				power_enable_count --;
				dev_err(&pdev->dev, "fail to enable hdmi power %s\n", hdmi_power);
				goto err_power;
			}
		}
	}*/


	/*Get IRQ numbers from device tree*/
	hdmi_drv->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (hdmi_drv->irq <= 0) {
		pr_err("%s:IRQ number %d invalid.\n", FUNC_NAME,
							hdmi_drv->irq);
		goto free_mem;
	}

	/*Now that everything is fine, let's add it to device list*/
	list_add_tail(&hdmi_drv->devlist, &devlist_global);

	hdmi_drv->hdmi_core = kmalloc(sizeof(struct hdmi_tx_core), GFP_KERNEL);
	if (!hdmi_drv->hdmi_core) {
		pr_err("%s:Could not allocated hdmi_tx_core\n", __func__);
		goto free_mem;
	}

	if (hdmi_tx_core_init(hdmi_drv->hdmi_core, phy_model)) {
		pr_err("Application init failed\n");
		goto free_mem;
	}

	mutex_init(&hdmi_drv->ctrl_mutex);
	mutex_init(&hdmi_drv->hpd_mutex);
	mutex_init(&hdmi_drv->hdcp_mutex);

	/*INIT_WORK(&hdmi_drv->hdmi_work, dwc_hdmi_tx_handler_func);*/
	INIT_WORK(&hdmi_drv->hpd_work, hpd_handler);
	INIT_WORK(&hdmi_drv->hdcp_work, hdcp_handler);
	hdmi_drv->hdmi_workqueue = create_workqueue("hdmi_hpd_workqueue");

	/*Register interrupts*/
	if (register_interrupts(hdmi_drv))
		goto free_mem;
	memset(&disp_func, 0, sizeof(struct disp_device_func));

#if defined(CONFIG_SWITCH) || defined(CONFIG_ANDROID_SWITCH)
	switch_dev_register(&hdmi_switch_dev);
#endif

	hdmi_drv->hdmi_core->hpd_state = HDMI_State_Wait_Hpd;

	disp_func.enable = hdmi_enable;
	disp_func.disable = hdmi_disable;
	/*disp_func.set_mode = hdmi_set_display_mode;*/
	disp_func.mode_support = hdmi_mode_support;
	disp_func.get_HPD_status = hdmi_get_HPD_status;
	disp_func.get_input_csc = hdmi_core_get_csc_type;
	disp_func.get_video_timing_info = hdmi_get_video_timming_info;
	disp_func.suspend = hdmi_suspend;
	disp_func.resume = hdmi_resume;
	disp_func.set_static_config = set_static_config;
	disp_func.get_static_config = get_static_config;
	disp_func.set_dynamic_config = set_dynamic_config;
	disp_func.get_dynamic_config = get_dynamic_config;
	disp_set_hdmi_func(&disp_func);

#if defined(CONFIG_SND_SUNXI_SOC_HDMIAUDIO)
	audio_func.hdmi_audio_enable = hdmi_audio_enable;
	audio_func.hdmi_set_audio_para = hdmi_set_audio_para;
	audio_set_hdmi_func(&audio_func);
	HDMI_INFO_MSG("audio_set_hdmi_func\n");

#if defined(CONFIG_SND_SUNXI_SOC_AUDIOHUB_INTERFACE)
	audio_func_muti.hdmi_audio_enable = hdmi_audio_enable;
	audio_func_muti.hdmi_set_audio_para = hdmi_set_audio_para;
	audio_set_muti_hdmi_func(&audio_func_muti);
#endif
 #endif
	HDMI_INFO_MSG("HDMI2.0 DRIVER PROBE END\n");

	return ret;

free_mem:
	kfree(hdmi_drv->hdmi_core);
	kfree(hdmi_drv);
	pr_info("Free core and drv memory\n");
	return -1;
}


static int hdmi_tx_exit(struct platform_device *pdev)
{
	struct hdmi_tx_drv *dev;
	struct list_head *list;

	while (!list_empty(&devlist_global)) {
		list = devlist_global.next;
		list_del(list);
		dev = list_entry(list, struct hdmi_tx_drv, devlist);

		if (dev == NULL)
			continue;
	}
	/* Unregister interrupts*/
	release_interrupts(dev);
	return 0;
}

/**
 * @short of_device_id structure
 */
static const struct of_device_id dw_hdmi_tx[] = {
	{ .compatible =	"allwinner,sunxi-hdmi" },
	{ }
};
MODULE_DEVICE_TABLE(of, dw_hdmi_tx);

/**
 * @short Platform driver structure
 */
static struct platform_driver __refdata dwc_hdmi_tx_pdrv = {
	.remove = hdmi_tx_exit,
	.probe = hdmi_tx_init,
	.driver = {
		.name = "allwinner,sunxi-hdmi",
		.owner = THIS_MODULE,
		.of_match_table = dw_hdmi_tx,
	},
};


static int hdmi_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int hdmi_release(struct inode *inode, struct file *file)
{
	return 0;
}


static ssize_t hdmi_read(struct file *file, char __user *buf,
						size_t count,
						loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t hdmi_write(struct file *file, const char __user *buf,
						size_t count,
						loff_t *ppos)
{
	return -EINVAL;
}

static int hdmi_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static long hdmi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return 0;
}


static const struct file_operations hdmi_fops = {
	.owner		= THIS_MODULE,
	.open		= hdmi_open,
	.release	= hdmi_release,
	.write	  = hdmi_write,
	.read		= hdmi_read,
	.unlocked_ioctl	= hdmi_ioctl,
	.mmap	   = hdmi_mmap,
};




static int __parse_dump_str(const char *buf, size_t size,
				unsigned long *start, unsigned long *end)
{
	char *ptr = NULL;
	char *ptr2 = (char *)buf;
	int ret = 0, times = 0;

	/* Support single address mode, some time it haven't ',' */
next:

	/*Default dump only one register(*start =*end).
	If ptr is not NULL, we will cover the default value of end.*/
	if (times == 1)
		*start = *end;

	if (!ptr2 || (ptr2 - buf) >= size)
		goto out;

	ptr = ptr2;
	ptr2 = strnchr(ptr, size - (ptr - buf), ',');
	if (ptr2) {
		*ptr2 = '\0';
		ptr2++;
	}

	ptr = strim(ptr);
	if (!strlen(ptr))
		goto next;

	ret = kstrtoul(ptr, 16, end);
	if (!ret) {
		times++;
		goto next;
	} else
	pr_warn("String syntax errors: \"%s\"\n", ptr);

out:
	return ret;
}


static ssize_t hdmi_test_reg_read_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%s\n", "echo [0x(address offset), 0x(count)] > hdmi_test_reg_read");
}

ssize_t hdmi_test_reg_read_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long start_reg = 0;
	unsigned long read_count = 0;
	u32 i;
	struct hdmi_tx_core *core = get_platform();

	if (__parse_dump_str(buf, count, &start_reg, &read_count))
		pr_err("%s,%d err, invalid para!\n", __func__, __LINE__);

	pr_info("start_reg=0x%lx  read_count=%ld\n", start_reg, read_count);
	for (i = 0; i < read_count; i++) {
		pr_info("hdmi_addr_offset: 0x%lx = 0x%x\n", start_reg,
				dev_read(&core->hdmi_tx, (start_reg * 4)));

		start_reg++;
	}
	pr_info("\n");

	return count;
}

static DEVICE_ATTR(hdmi_test_reg_read, S_IRUGO|S_IWUSR|S_IWGRP,
					hdmi_test_reg_read_show,
					hdmi_test_reg_read_store);


static ssize_t hdmi_test_reg_write_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%s\n", "echo [0x(address offset), 0x(value)] > hdmi_test_write");
}

ssize_t hdmi_test_reg_write_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long reg_addr = 0;
	unsigned long value = 0;
	struct hdmi_tx_core *core = get_platform();

	if (__parse_dump_str(buf, count, &reg_addr, &value))
		pr_err("%s,%d err, invalid para!\n", __func__, __LINE__);

	pr_info("reg_addr=0x%lx  write_value=0x%lx\n", reg_addr, value);
	dev_write(&core->hdmi_tx, (reg_addr * 4), value);

	mdelay(1);
	pr_info("after write,red(%lx)=%x\n", reg_addr,
			dev_read(&core->hdmi_tx, (reg_addr * 4)));

	return count;
}

static DEVICE_ATTR(hdmi_test_reg_write, S_IRUGO|S_IWUSR|S_IWGRP,
					hdmi_test_reg_write_show,
					hdmi_test_reg_write_store);

static ssize_t phy_write_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	pr_info("OPMODE_PLLCFG-0x16\n");
	pr_info("CKSYMTXCTRL-0x09\n");
	pr_info("PLLCURRCTRL-0x10\n");
	pr_info("VLEVCTRL-0x0E\n");
	pr_info("PLLGMPCTRL-0x15\n");
	pr_info("TXTERM-0x19\n");

	return sprintf(buf, "%s\n", "echo [0x(address offset), 0x(value)] > phy_write");
}

static ssize_t phy_write_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 reg_addr = 0;
	u16 value = 0;
	struct hdmi_tx_core *core = NULL;

	core = hdmi_drv->hdmi_core;

	if (__parse_dump_str(buf, count, (unsigned long *)&reg_addr, (unsigned long *)&value))
		pr_err("%s,%d err, invalid para!\n", __func__, __LINE__);

	pr_info("reg_addr=0x%x  write_value=0x%x\n", (u32)reg_addr, (u32)value);
	core->api_func.phy_write(&core->hdmi_tx, reg_addr, value);
	return count;
}

static DEVICE_ATTR(phy_write, S_IRUGO|S_IWUSR|S_IWGRP,
					phy_write_show,
					phy_write_store);


/*static DEVICE_ATTR(hdmi_test_print_core_structure, S_IRUGO|S_IWUSR|S_IWGRP,
					hdmi_test_print_core_structure_show,
					NULL);*/

static ssize_t phy_read_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	pr_info("OPMODE_PLLCFG-0x16\n");
	pr_info("CKSYMTXCTRL-0x09\n");
	pr_info("PLLCURRCTRL-0x10\n");
	pr_info("VLEVCTRL-0x0E\n");
	pr_info("PLLGMPCTRL-0x15\n");
	pr_info("TXTERM-0x19\n");

	return sprintf(buf, "%s\n", "echo [0x(address offset), 0x(count)] > phy_read");
}

ssize_t phy_read_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 start_reg = 0;
	u16 value = 0;
	unsigned long read_count = 0;
	u32 i;
	struct hdmi_tx_core *core = get_platform();

	if (__parse_dump_str(buf, count, (unsigned long *)&start_reg, &read_count))
		pr_err("%s,%d err, invalid para!\n", __func__, __LINE__);

	pr_info("start_reg=0x%x  read_count=%ld\n", (u32)start_reg, read_count);
	for (i = 0; i < read_count; i++) {
		core->api_func.phy_read(&core->hdmi_tx, start_reg, &value);
		pr_info("hdmi_addr_offset: 0x%x = 0x%x\n", (u32)start_reg, value);
		start_reg++;
	}
	pr_info("\n");

	return count;
}

static DEVICE_ATTR(phy_read, S_IRUGO|S_IWUSR|S_IWGRP,
					phy_read_show,
					phy_read_store);


static ssize_t scdc_read_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%s\n", "echo [0x(address offset), 0x(count)] > scdc_read");
}

ssize_t scdc_read_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 start_reg = 0;
	u8 value = 0;
	unsigned long read_count = 0;
	u32 i;
	struct hdmi_tx_core *core = get_platform();

	if (__parse_dump_str(buf, count, (unsigned long *)&start_reg, &read_count))
		pr_err("%s,%d err, invalid para!\n", __func__, __LINE__);

	pr_info("start_reg=0x%x  read_count=%ld\n", (u32)start_reg, read_count);
	for (i = 0; i < read_count; i++) {
		core->api_func.scdc_read(&core->hdmi_tx, start_reg, 1, &value);
		pr_info("hdmi_addr_offset: 0x%x = 0x%x\n", (u32)start_reg, value);
		start_reg++;
	}
	pr_info("\n");

	return count;
}

static DEVICE_ATTR(scdc_read, S_IRUGO|S_IWUSR|S_IWGRP,
					scdc_read_show,
					scdc_read_store);

static ssize_t scdc_write_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%s\n", "echo [0x(address offset), 0x(value)] > scdc_write");
}

static ssize_t scdc_write_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 reg_addr = 0;
	u8 value = 0;
	struct hdmi_tx_core *core = NULL;

	core = hdmi_drv->hdmi_core;

	if (__parse_dump_str(buf, count, (unsigned long *)&reg_addr, (unsigned long *)&value))
		pr_err("%s,%d err, invalid para!\n", __func__, __LINE__);

	pr_info("reg_addr=0x%x  write_value=0x%x\n", reg_addr, value);
	core->api_func.scdc_write(&core->hdmi_tx, reg_addr, 1, &value);
	return count;
}

static DEVICE_ATTR(scdc_write, S_IRUGO|S_IWUSR|S_IWGRP,
					scdc_write_show,
					scdc_write_store);


static ssize_t hdmi_debug_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "debug=%d\n", hdmi_printf);

	return 0;
}

static ssize_t hdmi_debug_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	if (count < 1)
		return -EINVAL;

	if (strnicmp(buf, "1", 1) == 0)
		hdmi_printf = 1;
	else if (strnicmp(buf, "0", 1) == 0)
		hdmi_printf = 0;
	else
		HDMI_ERROR_MSG("Error Input!\n");

	pr_info("debug=%d\n", hdmi_printf);

	return count;
}
static DEVICE_ATTR(debug, S_IRUGO|S_IWUSR|S_IWGRP, hdmi_debug_show,
							hdmi_debug_store);

static ssize_t hdmi_rgb_only_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc444Support ||
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc422Support ||
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc420Support)
		return sprintf(buf, "rgb_only=%s\n", "off");
	else
		return sprintf(buf, "rgb_only=%s\n", "on");

	return 0;
}

static ssize_t hdmi_rgb_only_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int mYcc444Support =
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc444Support;
	int mYcc422Support =
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc422Support;
	int mYcc420Support =
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc420Support;

	if (count < 1)
		return -EINVAL;

	if (strnicmp(buf, "on", 2) == 0 || strnicmp(buf, "1", 1) == 0) {
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc444Support = 0;
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc422Support = 0;
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc420Support = 0;
	} else if (strnicmp(buf, "off", 3) == 0 || strnicmp(buf, "0", 1) == 0) {
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc444Support =
								mYcc444Support;
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc422Support =
								mYcc422Support;
		hdmi_drv->hdmi_core->mode.sink_cap->edid_mYcc420Support =
								mYcc420Support;
		} else {
			return -EINVAL;
		}

	return count;
}

static DEVICE_ATTR(rgb_only, S_IRUGO|S_IWUSR|S_IWGRP, hdmi_rgb_only_show,
							hdmi_rgb_only_store);


static ssize_t hdmi_hpd_mask_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "0x%x\n", hdmi_hpd_mask);
}

static ssize_t hdmi_hpd_mask_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int err;
	unsigned long val;

	if (count < 1)
		return -EINVAL;

	err = kstrtoul(buf, 16, &val);
	if (err) {
		pr_info("Invalid size\n");
		return err;
	}

	pr_info("val=0x%x\n", (u32)val);
	hdmi_hpd_mask = val;

	queue_work(hdmi_drv->hdmi_workqueue, &hdmi_drv->hpd_work);
	return count;
}

static DEVICE_ATTR(hpd_mask, S_IRUGO|S_IWUSR|S_IWGRP, hdmi_hpd_mask_show,
							hdmi_hpd_mask_store);


static ssize_t hdmi_edid_show(struct device *dev, struct device_attribute *attr,
							char *buf)
{
	u8 pedid[1024];

	if ((hdmi_drv->hdmi_core->mode.edid != NULL) &&
		(hdmi_drv->hdmi_core->mode.edid_ext != NULL)) {
		/*EDID_block0*/
		memcpy(pedid, hdmi_drv->hdmi_core->mode.edid, 0x80);
		/*EDID_block1*/
		memcpy(pedid+0x80, hdmi_drv->hdmi_core->mode.edid_ext, 0x380);

		memcpy(buf, pedid, 0x400);

		return 0x400;
	} else {
		return 0;
	}
}

static ssize_t hdmi_edid_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(edid, S_IRUGO|S_IWUSR|S_IWGRP,
			hdmi_edid_show,
			hdmi_edid_store);


static ssize_t hdmi_hdcp_enable_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n",
			hdmi_drv->hdmi_core->hdmi_tx.snps_hdmi_ctrl.hdcp_on);
}

static ssize_t hdmi_hdcp_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	if (count < 1)
		return -EINVAL;

	if (strnicmp(buf, "1", 1) == 0)
		hdcp_core_enable(1);
	else
		hdcp_core_enable(0);

	return count;
}

static DEVICE_ATTR(hdcp_enable, S_IRUGO|S_IWUSR|S_IWGRP,
				hdmi_hdcp_enable_show,
				hdmi_hdcp_enable_store);


static struct attribute *hdmi_attributes[] = {
	&dev_attr_hdmi_test_reg_read.attr,
	&dev_attr_hdmi_test_reg_write.attr,
	&dev_attr_phy_write.attr,
	&dev_attr_phy_read.attr,
	&dev_attr_scdc_read.attr,
	&dev_attr_scdc_write.attr,
	/*&dev_attr_hdmi_test_print_core_structure.attr,*/

	&dev_attr_debug.attr,
	&dev_attr_rgb_only.attr,
	&dev_attr_hpd_mask.attr,
	&dev_attr_edid.attr,
	&dev_attr_hdcp_enable.attr,
	NULL
};

static struct attribute_group hdmi_attribute_group = {
	.name = "attr",
	.attrs = hdmi_attributes
};

static int __init hdmi_module_init(void)
{
	int ret = 0, err;

	/*Create and add a character device*/
	alloc_chrdev_region(&devid, 0, 1, "hdmi");/*corely for device number*/
	hdmi_cdev = cdev_alloc();
	cdev_init(hdmi_cdev, &hdmi_fops);
	hdmi_cdev->owner = THIS_MODULE;
	err = cdev_add(hdmi_cdev, devid, 1);/*/proc/device/hdmi*/
	if (err) {
		HDMI_ERROR_MSG("cdev_add fail.\n");
		return -1;
	}

	/*Create a path: sys/class/hdmi*/
	hdmi_class = class_create(THIS_MODULE, "hdmi");
	if (IS_ERR(hdmi_class)) {
		HDMI_ERROR_MSG("class_create fail\n");
		return -1;
	}

	/*Create a path "sys/class/hdmi/hdmi"*/
	hdev = device_create(hdmi_class, NULL, devid, NULL, "hdmi");

	/*Create a path: sys/class/hdmi/hdmi/attr*/
	ret = sysfs_create_group(&hdev->kobj, &hdmi_attribute_group);
	if (ret)
		HDMI_INFO_MSG("failed!\n");

	ret = platform_driver_register(&dwc_hdmi_tx_pdrv);
	if (ret)
		HDMI_ERROR_MSG("hdmi driver register fail\n");

	pr_info("HDMI2.0 module init end\n");

	return ret;
}

static void __exit hdmi_module_exit(void)
{
	pr_info("hdmi_module_exit\n");

	hdmi_tx_exit(hdmi_drv->pdev);
	hdmi_core_exit(hdmi_drv->hdmi_core);

	kfree(hdmi_drv);

	platform_driver_unregister(&dwc_hdmi_tx_pdrv);

	sysfs_remove_group(&hdev->kobj, &hdmi_attribute_group);
	device_destroy(hdmi_class, devid);

	destroy_workqueue(hdmi_drv->hdmi_workqueue);

	class_destroy(hdmi_class);
	cdev_del(hdmi_cdev);
}

late_initcall(hdmi_module_init);
module_exit(hdmi_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("zhengwanyu");
MODULE_DESCRIPTION("HDMI_TX20 module driver");
MODULE_VERSION("1.0");
