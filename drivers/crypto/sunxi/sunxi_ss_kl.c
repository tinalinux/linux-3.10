/*
 * The driver of sunxi key ladder.
 *
 * Copyright (C) 2017-2020 Allwinner.
 *
 * zhouhuacai <zhouhuacai@allwinnertech.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>

#include "sunxi_ss.h"
#include "sunxi_ss_reg.h"
#include "sunxi_ss_proc.h"
#include "sunxi_ss_kl.h"
#include <linux/sunxi_ss_kl_ioctl.h>


static struct ce_task_desc *kl_task_list;
static struct ss_kl_s *ss_kl;
static u32 key_zero[4] = {0};

static ssize_t ss_kl_info_show(struct class *class,
				struct class_attribute *attr, char *buf);

static DEFINE_MUTEX(kl_lock);

void ss_kl_lock(void)
{
	mutex_lock(&kl_lock);
}

void ss_kl_unlock(void)
{
	mutex_unlock(&kl_lock);
}

void sunxi_ss_kl_dump(char *_data, int _len, void *_addr)
{
	u32 i, phy_addr;
	for (i = 0; i < _len / 8; i++) {
		phy_addr = virt_to_phys(i*8 + _addr);
		pr_info("0x%x: %02X %02X %02X %02X %02X %02X %02X %02X\n",
			phy_addr, _data[i*8+0], _data[i*8+1],
			_data[i*8+2], _data[i*8+3], _data[i*8+4],
			_data[i*8+5], _data[i*8+6], _data[i*8+7]);
	}
}

static void ss_kl_task_desc_dump(struct ce_task_desc *kl_task)
{
#ifdef SUNXI_KL_DEBUG
	static int cnt;

	pr_info("========task %d: addr:0x%x========\n", cnt++,
			(u32)virt_to_phys(kl_task));

	pr_info("kl_task->chan_id       :0x%x\n", kl_task->chan_id);
	pr_info("kl_task->comm_ctl      :0x%x\n", kl_task->comm_ctl);
	pr_info("kl_task->sym_ctl       :0x%x\n", kl_task->sym_ctl);
	pr_info("kl_task->asym_ctl      :0x%x\n", kl_task->asym_ctl);
	pr_info("kl_task->key_addr      :0x%x\n", kl_task->key_addr);
	pr_info("kl_task->iv_addr       :0x%x\n", kl_task->iv_addr);
	pr_info("kl_task->data_len      :0x%x\n", kl_task->data_len);
	pr_info("kl_task->ctr_addr      :0x%x\n", kl_task->ctr_addr);
	pr_info("kl_task->src[0].addr   :0x%x\n", kl_task->src[0].addr);
	pr_info("kl_task->src[0].len    :0x%x\n", kl_task->src[0].len);
	pr_info("kl_task->dst[0].addr   :0x%x\n", kl_task->dst[0].addr);
	pr_info("kl_task->dst[0].len    :0x%x\n", kl_task->dst[0].len);
	pr_info("kl_task->next          :0x%x\n", (u32)kl_task->next);
	pr_info("\n");
#endif
}

#ifdef SUNXI_KL_DEBUG
static void ss_kl_task_addr_dump(void)
{
	int i;
	pr_info("===============ss_kl_task_addr_dump===============\n");
	for (i = 0; i < KL_TASK_NUM; i++)
		pr_info("kl_task[%d]:0x%x\n", i,
			(u32)virt_to_phys(&kl_task_list[i]));
}
#endif

static u32 ss_kl_check_no_modk(void)
{
	int i;
	for (i = 0; i < sizeof(ss_kl->mid); i++) {
		if (*(ss_kl->mid + i))
			return 0;
	}

	return 1;
}
static void ss_kl_dma_map(u32 addr, u32 len, int dir)
{
	if (!addr)
		return;

	dma_map_single(&ss_dev->pdev->dev, phys_to_virt(addr), len, dir);
}
static void ss_kl_dma_unmap(u32 addr, u32 len, int dir)
{
	if (!addr)
		return;

	dma_unmap_single(&ss_dev->pdev->dev, addr, len, dir);
}

static void ss_kl_dma_map_res(void)
{
	int i;
	struct ce_task_desc *t;

	for (i = 0; i < KL_TASK_NUM; i++) {
		t = &kl_task_list[i];
		ss_kl_dma_map(t->src[0].addr, t->src[0].len * 4,
				DMA_MEM_TO_DEV);
		ss_kl_dma_map(t->key_addr, sizeof(key_zero),
				DMA_MEM_TO_DEV);
		ss_kl_dma_map(t->dst[0].addr, t->dst[0].len * 4,
				DMA_DEV_TO_MEM);

	}
}
static void ss_kl_dma_unmap_res(void)
{
	int i;
	struct ce_task_desc *t;

	for (i = 0; i < KL_TASK_NUM; i++) {
		t = &kl_task_list[i];
		ss_kl_dma_unmap(t->src[0].addr, t->src[0].len * 4,
				DMA_MEM_TO_DEV);
		ss_kl_dma_unmap(t->key_addr, sizeof(key_zero),
				DMA_MEM_TO_DEV);
		ss_kl_dma_unmap(t->dst[0].addr, t->dst[0].len * 4,
				DMA_DEV_TO_MEM);
	}
}

static void ss_kl_set_src(u32 addr, u32 len, ce_task_desc_t *task)
{
	task->src[0].addr = addr;
	task->src[0].len = len / 4;
}

static void ss_kl_set_dest(u32 addr, u32 len, ce_task_desc_t *task)
{
	task->dst[0].addr = addr;
	task->dst[0].len = len / 4;
}

static int ss_kl_set_info(const void *param)
{
	if (copy_from_user((void *)ss_kl, (void __user *)param,
			sizeof(struct ss_kl_s)))
		return -EFAULT;

#ifdef SUNXI_KL_DEBUG
	ss_kl_info_show(NULL, NULL, NULL);
	ss_kl_task_addr_dump();
#endif

	return 0;
}

static int ss_kl_set_ekey_info(const void *param)
{
	if (copy_from_user((void *)ss_kl->ek, (void __user *)param,
			sizeof(ss_kl->ek)))
		return -EFAULT;

	return 0;
}

static int ss_kl_get_info(void *param)
{
	if (copy_to_user((void __user *)param, ss_kl, sizeof(struct ss_kl_s)))
		return -EFAULT;

	return 0;
}

static u32 ss_kl_get_cw_len_by_alg(void)
{
	u32 ret;
	switch (ss_kl->status.kl_alg_type) {
	case SS_METHOD_AES:
		ret = DATA_LEN_16BYTE;
		break;
	case SS_METHOD_3DES:
		ret = DATA_LEN_8BYTE;
		break;
	default:
		break;
	}
	return ret;

}
static u32 ss_kl_get_dkf_task_num(void)
{
	u32 ret;
	switch (ss_kl->status.kl_level) {
	case SS_KL_LEVEL3:
		ret = 3;
		break;
	case SS_KL_LEVEL5:
		ret = 5;
		break;
	default:
		break;
	}
	return ret;
}
static struct ce_task_desc *ss_kl_get_dkf_by_level(void)
{
	struct ce_task_desc *t;

	switch (ss_kl->status.kl_level) {
	case SS_KL_LEVEL3:
		t = &kl_task_list[KL_DK3F_TASK];
		break;
	case SS_KL_LEVEL5:
		t = &kl_task_list[KL_DK5F_TASK];
		break;
	default:
		KL_ERR("Unsupport key ladder level:%d\n",
		ss_kl->status.kl_level);
		break;
	}
	return t;
}

static inline void
ss_kl_set_next_task(struct ce_task_desc *c, struct ce_task_desc *n)
{
	if (n != NULL)
		c->next = (struct ce_task_desc *)virt_to_phys(n);
	else
		c->next = NULL;
}
static inline void ss_kl_set_key(u32 *key, struct ce_task_desc *t)
{
	if (!key)
		return;

	t->key_addr = virt_to_phys(key);
}

/*Task0 :Preliminary SCK Manipulation Function*/
static void __create_psmf_task(u32 flow)
{
	struct ce_task_desc *cur_task = &kl_task_list[KL_PSMF_TASK];
	struct ce_task_desc *next_task = &kl_task_list[KL_VSF_TASK];

	ss_task_desc_init(cur_task, flow);
	ss_method_set(SS_DIR_DECRYPT, ss_kl->status.kl_alg_type, cur_task);
	ss_keyselect_set(CE_KEY_SELECT_SCK0, cur_task);
	ss_aes_mode_set(SS_AES_MODE_ECB, cur_task);
	ss_kl_level_set(ss_kl->status.kl_level, cur_task);
	ss_keysize_set(CE_AES_KEY_SIZE_128, cur_task);
	ss_data_len_set(DATA_LEN_16BYTE, cur_task);
	ss_kl_set_src(virt_to_phys(ss_kl->vid), DATA_LEN_16BYTE, cur_task);
	ss_kl_set_dest(0, DATA_LEN_16BYTE, cur_task);
	ss_kl_set_next_task(cur_task, next_task);
	ss_kl_set_key(key_zero, cur_task);
	ss_kl_task_desc_dump(cur_task);
}

/*Task1: Vendor Separation Function*/
static void __create_vsf_task(u32 flow)
{
	struct ce_task_desc *cur_task = &kl_task_list[KL_VSF_TASK];
	struct ce_task_desc *next_task = &kl_task_list[KL_FRDF_TASK];

	ss_task_desc_init(cur_task, flow);
	ss_method_set(SS_DIR_DECRYPT, ss_kl->status.kl_alg_type, cur_task);
	ss_keyselect_set(CE_KEY_SELECT_INPUT, cur_task);
	ss_aes_mode_set(SS_AES_MODE_ECB, cur_task);
	ss_kl_level_set(ss_kl->status.kl_level, cur_task);
	ss_keysize_set(CE_AES_KEY_SIZE_128, cur_task);
	ss_data_len_set(DATA_LEN_16BYTE, cur_task);
	ss_kl_set_src(virt_to_phys(ss_kl->vid), DATA_LEN_16BYTE, cur_task);
	ss_kl_set_dest(0, DATA_LEN_16BYTE, cur_task);
	ss_kl_set_next_task(cur_task, next_task);
	ss_kl_set_key(key_zero, cur_task);
	ss_kl_task_desc_dump(cur_task);
}

/*Task2: Final Root key Derivation Function*/
static void __create_frdf_task(u32 flow)
{
	struct ce_task_desc *cur_task = &kl_task_list[KL_FRDF_TASK];
	struct ce_task_desc *next_task = &kl_task_list[KL_MKDF_TASK];
	u32 no_modk;

	no_modk = ss_kl_check_no_modk();

	ss_task_desc_init(cur_task, flow);
	ss_method_set(SS_DIR_DECRYPT, ss_kl->status.kl_alg_type, cur_task);
	ss_kl_no_modk_set(no_modk, cur_task);
	ss_keyselect_set(CE_KEY_SELECT_INPUT, cur_task);
	ss_aes_mode_set(SS_AES_MODE_ECB, cur_task);
	ss_kl_level_set(ss_kl->status.kl_level, cur_task);
	ss_keysize_set(CE_AES_KEY_SIZE_128, cur_task);
	ss_data_len_set(DATA_LEN_16BYTE, cur_task);
	ss_kl_set_src(virt_to_phys(ss_kl->vid), DATA_LEN_16BYTE, cur_task);
	ss_kl_set_dest(0, DATA_LEN_16BYTE, cur_task);
	if (no_modk)
		next_task = ss_kl_get_dkf_by_level();
	ss_kl_set_next_task(cur_task, next_task);
	ss_kl_set_key(key_zero, cur_task);
	ss_kl_task_desc_dump(cur_task);
}

/*Task3: Module Key Derivation Function*/
static void __create_mkdf_task(u32 flow)
{
	struct ce_task_desc *cur_task = &kl_task_list[KL_MKDF_TASK];
	struct ce_task_desc *next_task;
	u32 no_modk;

	no_modk = ss_kl_check_no_modk();

	ss_task_desc_init(cur_task, flow);
	ss_method_set(SS_DIR_DECRYPT, ss_kl->status.kl_alg_type, cur_task);
	ss_kl_no_modk_set(no_modk, cur_task);
	ss_keyselect_set(CE_KEY_SELECT_INPUT, cur_task);
	ss_aes_mode_set(SS_AES_MODE_ECB, cur_task);
	ss_kl_level_set(ss_kl->status.kl_level, cur_task);
	ss_keysize_set(CE_AES_KEY_SIZE_128, cur_task);
	ss_data_len_set(DATA_LEN_16BYTE, cur_task);
	ss_kl_set_src(virt_to_phys(ss_kl->mid), DATA_LEN_16BYTE, cur_task);
	ss_kl_set_dest(0, DATA_LEN_16BYTE, cur_task);
	next_task = ss_kl_get_dkf_by_level();
	ss_kl_set_next_task(cur_task, next_task);
	ss_kl_set_key(key_zero, cur_task);
	ss_kl_task_desc_dump(cur_task);
}

/*
 * Task4~Task6 (keyladder3)
 * Task4~Task8 (keyladder5)
 * Task: Decrypt Key Function
 */
static void __create_dkf_task(u32 flow)
{
	struct ce_task_desc *cur_task = ss_kl_get_dkf_by_level();
	struct ce_task_desc *next_task;
	int i, task_num, len = DATA_LEN_16BYTE;

	task_num = ss_kl_get_dkf_task_num();
	for (i = 0; i < task_num; i++) {
		ss_task_desc_init(cur_task, flow);
		ss_method_set(SS_DIR_DECRYPT, ss_kl->status.kl_alg_type,
				cur_task);
		ss_keyselect_set(CE_KEY_SELECT_INPUT, cur_task);
		ss_aes_mode_set(SS_AES_MODE_ECB, cur_task);
		ss_kl_level_set(ss_kl->status.kl_level, cur_task);
		ss_keysize_set(CE_AES_KEY_SIZE_128, cur_task);
		ss_data_len_set(DATA_LEN_16BYTE, cur_task);
		next_task = cur_task+1;
		if (i == task_num-1) {
			len = ss_kl_get_cw_len_by_alg();
			if (ss_kl->status.kl_alg_type == SS_METHOD_3DES)
					len = DATA_LEN_8BYTE;
		}
		ss_data_len_set(len, cur_task);
		ss_kl_set_src(virt_to_phys(ss_kl->ek[task_num-i-1]),
				len, cur_task);
		ss_kl_set_dest(0, len, cur_task);
		ss_kl_set_next_task(cur_task, next_task);
		ss_kl_set_key(key_zero, cur_task);
		ss_kl_task_desc_dump(cur_task);
		cur_task = next_task;
	}
}

/*Task7(Task9): Challenge Response(DK2)*/
static void __create_daf_task(u32 flow)
{
	struct ce_task_desc *cur_task = &kl_task_list[KL_DA_DK2_TASK];
	struct ce_task_desc *next_task = &kl_task_list[KL_DA_NONCE_TASK];
	cur_task->chan_id = flow;

	ss_task_desc_init(cur_task, flow);
	ss_method_set(SS_DIR_DECRYPT, ss_kl->status.kl_alg_type, cur_task);
	ss_keyselect_set(CE_KEY_SELECT_INPUT, cur_task);
	ss_aes_mode_set(SS_AES_MODE_ECB, cur_task);
	ss_kl_level_set(ss_kl->status.kl_level, cur_task);
	ss_keysize_set(CE_AES_KEY_SIZE_128, cur_task);
	ss_data_len_set(DATA_LEN_16BYTE, cur_task);
	ss_kl_set_src(0, DATA_LEN_16BYTE, cur_task);
	ss_kl_set_dest(0, DATA_LEN_16BYTE, cur_task);
	ss_kl_set_next_task(cur_task, next_task);
	ss_kl_set_key(key_zero, cur_task);
	ss_kl_task_desc_dump(cur_task);
}

/*Task8(Task10): Challenge Response(DANONCE)*/
static void __create_danoncef_task(u32 flow)
{
	struct ce_task_desc *cur_task = &kl_task_list[KL_DA_NONCE_TASK];

	ss_task_desc_init(cur_task, flow);
	ss_method_set(SS_DIR_DECRYPT, ss_kl->status.kl_alg_type, cur_task);
	ss_keyselect_set(CE_KEY_SELECT_INPUT, cur_task);
	ss_aes_mode_set(SS_AES_MODE_ECB, cur_task);
	ss_kl_level_set(ss_kl->status.kl_level, cur_task);
	ss_keysize_set(CE_AES_KEY_SIZE_128, cur_task);
	ss_data_len_set(DATA_LEN_16BYTE, cur_task);
	ss_kl_set_src(virt_to_phys(ss_kl->nonce), DATA_LEN_16BYTE, cur_task);
	ss_kl_set_dest(virt_to_phys(ss_kl->da_nonce),
			DATA_LEN_16BYTE, cur_task);
	ss_kl_set_next_task(cur_task, NULL);
	ss_kl_set_key(NULL, cur_task);
	ss_kl_task_desc_dump(cur_task);
}

static void ss_kl_task_desc_init(u32 flow)
{
	__create_psmf_task(flow);
	__create_vsf_task(flow);
	__create_frdf_task(flow);
	__create_mkdf_task(flow);
	__create_dkf_task(flow);
	__create_daf_task(flow);
	__create_danoncef_task(flow);
}

static int ss_kl_gen_all(void)
{
	ss_comm_ctx_t comm;
	u32 flow, ret;

	ss_flow_request(&comm);
	flow = comm.flow;

	ss_pending_clear(flow);
	ss_irq_enable(flow);

	/*config CE task descriptor*/
	ss_kl_task_desc_init(flow);

	/*dma map resource */
	init_completion(&ss_dev->flows[flow].done);
	ss_kl_dma_map_res();
	dma_map_single(&ss_dev->pdev->dev, &kl_task_list[0],
			sizeof(ce_task_desc_t)*KL_TASK_NUM, DMA_MEM_TO_DEV);
	/* start CE controller */
	ss_ctrl_start(&kl_task_list[0]);

	/*wait task end*/
	ret = wait_for_completion_timeout(&ss_dev->flows[flow].done,
					msecs_to_jiffies(SS_WAIT_TIME));
	if (ret == 0) {
		SS_ERR("Timed out\n");
		ss_reset();
		ret = -ETIMEDOUT;
	}

	ss_irq_disable(flow);
	/*dma unmap resource */
	dma_unmap_single(&ss_dev->pdev->dev, virt_to_phys(&kl_task_list[0]),
			sizeof(ce_task_desc_t)*KL_TASK_NUM, DMA_MEM_TO_DEV);
	ss_kl_dma_unmap_res();

	SS_DBG("After CE, TSR: 0x%08x, ERR: 0x%08x\n",
	       ss_reg_rd(CE_REG_TSR), ss_reg_rd(CE_REG_ERR));

	ret = ss_flow_err(flow);
	if (ret) {
		SS_ERR("CE return error: %d\n", ss_flow_err(flow));
		ret = -EINVAL;
	}
	ss_flow_release(&comm);

#ifdef SUNXI_KL_DEBUG
	sunxi_ss_kl_dump((s8 *)ss_kl->da_nonce,
			sizeof(ss_kl->da_nonce), ss_kl->da_nonce);
#endif

	return ret;

}

static int sunxi_ss_kl_open(struct inode *inode, struct file *file)
{
	ss_kl = kzalloc(sizeof(struct ss_kl_s), GFP_KERNEL);
	if (NULL == ss_kl)
		return -ENOMEM;

	kl_task_list = kzalloc((sizeof(ce_task_desc_t)*KL_TASK_NUM),
				GFP_KERNEL);
	if (NULL == kl_task_list) {
		kfree(ss_kl);
		return -ENOMEM;
	}

	return 0;
}

static int sunxi_ss_kl_release(struct inode *inode, struct file *file)
{
	kfree(ss_kl);
	kfree(kl_task_list);

	return 0;
}

static long sunxi_ss_kl_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;

	ss_kl_lock();

	switch (cmd) {
	case KL_SET_INFO:
		ret = ss_kl_set_info((void *)arg);
		break;
	case KL_SET_KEY_INFO:
		ret = ss_kl_set_ekey_info((void *)arg);
		break;
	case KL_GET_INFO:
		ret = ss_kl_get_info((void *)arg);
		break;
	case KL_GEN_ALL:
		ret = ss_kl_gen_all();
		break;
	default:
		KL_ERR("Unsupported cmd:%d\n", cmd);
		ret = -EINVAL;
		break;
	}
	ss_kl_unlock();

	return ret;
}

static ssize_t ss_kl_info_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	int i, level = 3;
	if (!ss_kl)
		return -EINVAL;

	pr_info("ss_kl_info:\n");
	pr_info("kl_level:	%x\n", ss_kl->status.kl_level);
	pr_info("kl_alg_type:	%x\n", ss_kl->status.kl_alg_type);
	pr_info("kl_status:	%x\n", ss_kl->status.kl_stu);
	pr_info("--------------------vendor id--------------------\n");
	sunxi_ss_kl_dump((s8 *)ss_kl->vid, sizeof(ss_kl->vid), ss_kl->vid);

	pr_info("--------------------module id--------------------\n");
	sunxi_ss_kl_dump((s8 *)ss_kl->mid, sizeof(ss_kl->mid), ss_kl->mid);

	if (ss_kl->status.kl_level == SS_KL_LEVEL5)
		level = 5;

	for (i = 0; i < level; i++) {
		pr_info("--------------------ekey%x--------------------\n", i);
		sunxi_ss_kl_dump((s8 *)ss_kl->ek[i], sizeof(ss_kl->ek[i]),
				ss_kl->ek[i]);
	}
	pr_info("--------------------nonce--------------------\n");
	sunxi_ss_kl_dump((s8 *)ss_kl->nonce, sizeof(ss_kl->nonce),
			ss_kl->nonce);
	pr_info("--------------------da nonce--------------------\n");
	sunxi_ss_kl_dump((s8 *)ss_kl->da_nonce, sizeof(ss_kl->nonce),
			ss_kl->da_nonce);

	return 0;
}

static struct class_attribute info_class_attrs[] = {
	__ATTR(ss_kl_info, 0644, ss_kl_info_show, NULL),
	__ATTR_NULL,
};

static const struct file_operations sunxi_ss_kl_ops = {
	.owner   = THIS_MODULE,
	.open    = sunxi_ss_kl_open,
	.release = sunxi_ss_kl_release,
	.unlocked_ioctl = sunxi_ss_kl_ioctl,
};

static struct class kl_class = {
		.name           = "ss_key_ladder",
		.owner          = THIS_MODULE,
		.class_attrs    = info_class_attrs,
	};

struct miscdevice ss_kl_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "sunxi_key_ladder",
	.fops  = &sunxi_ss_kl_ops,
};

int __init sunxi_ss_kl_init(void)
{
	s32 ret = 0;

	ret = class_register(&kl_class);
	if (ret != 0)
		return ret;

	ret = misc_register(&ss_kl_device);
	if (ret != 0) {
		KL_ERR("misc_register() failed!(%d)\n", ret);
		class_unregister(&kl_class);
		return ret;
	}
	return ret;
}

void __exit sunxi_ss_kl_exit(void)
{
	if (misc_deregister(&ss_kl_device))
		KL_ERR("misc_deregister() failed!\n");
	class_unregister(&kl_class);
}

