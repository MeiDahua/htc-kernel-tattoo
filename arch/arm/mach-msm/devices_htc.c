/* linux/arch/arm/mach-msm/devices.c
 *
 * Copyright (C) 2008 Google, Inc.
 * Copyright (C) 2007-2009 HTC Corporation.
 * Author: Thomas Tsai <thomas_tsai@htc.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/dma-mapping.h>
#include <mach/msm_iomap.h>
#include <mach/dma.h>
#include "gpio_chip.h"
#include "devices.h"
#include <mach/board.h>
#include <mach/board_htc.h>
#include <mach/msm_hsusb.h>

#ifdef CONFIG_USB_FUNCTION
#include <linux/usb/mass_storage_function.h>
#endif
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android.h>
#endif

#include <asm/mach/flash.h>
#include <asm/setup.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <linux/android_pmem.h>
#include <mach/msm_rpcrouter.h>
#include <mach/msm_iomap.h>
#include <asm/mach/mmc.h>

static char *df_serialno = "000000000000";

#if 0
struct platform_device *devices[] __initdata = {
	&msm_device_nand,
	&msm_device_smd,
	&msm_device_i2c,
};

void __init msm_add_devices(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}
#endif

#define HSUSB_API_INIT_PHY_PROC	2
#define HSUSB_API_PROG		0x30000064
#define HSUSB_API_VERS MSM_RPC_VERS(1,1)

static void internal_phy_reset(void)
{
	struct msm_rpc_endpoint *usb_ep;
	int rc;
	struct hsusb_phy_start_req {
		struct rpc_request_hdr hdr;
	} req;

	printk(KERN_INFO "msm_hsusb_phy_reset\n");

	usb_ep = msm_rpc_connect(HSUSB_API_PROG, HSUSB_API_VERS, 0);
	if (IS_ERR(usb_ep)) {
		printk(KERN_ERR "%s: init rpc failed! error: %ld\n",
				__func__, PTR_ERR(usb_ep));
		goto close;
	}
	rc = msm_rpc_call(usb_ep, HSUSB_API_INIT_PHY_PROC,
			&req, sizeof(req), 5 * HZ);
	if (rc < 0)
		printk(KERN_ERR "%s: rpc call failed! (%d)\n", __func__, rc);

close:
	msm_rpc_close(usb_ep);
}

/* adjust eye diagram, disable vbusvalid interrupts */
static int hsusb_phy_init_seq[] = { 0x40, 0x31, 0x1D, 0x0D, 0x1D, 0x10, -1 };

#ifdef CONFIG_USB_FUNCTION
static char *usb_functions[] = {
#if defined(CONFIG_USB_FUNCTION_MASS_STORAGE) || defined(CONFIG_USB_FUNCTION_UMS)
	"usb_mass_storage",
#endif
#if defined(CONFIG_USB_FUNCTION_ADB)
	"adb",
#endif
#if defined(CONFIG_USB_FUNCTION_ETHER)
	"ether",
#endif
#if defined(CONFIG_USB_FUNCTION_DIAG)
	"diag",
#endif
#if defined(CONFIG_USB_FUNCTION_SERIAL)
	"modem",
#endif
#if defined(CONFIG_USB_FUNCTION_PROJECTOR)
	"projector",
#endif
#if defined(CONFIG_USB_FUNCTION_FSYNC)
	"fsync",
#endif
#if defined(CONFIG_USB_FUNCTION_MTP_TUNNEL)
	"mtp_tunnel",
#endif
#if defined(CONFIG_USB_FUNCTION_SERIAL)
	"nmea",
#endif

};

/* about .functions variable, please refer: drivers/usb/function/usb_function.h  */
static struct msm_hsusb_product usb_products[] = {
	{
		.product_id	= 0x0c01,
		.functions	= 0x00000001, /* usb_mass_storage */
	},
	{
		.product_id	= 0x0c02,
		.functions	= 0x00000003, /* usb_mass_storage + adb */
	},
	{
		.product_id	= 0x0c03,
		.functions	= 0x00000011, /* modem + mass_storage */
	},
	{
		.product_id	= 0x0c04,
		.functions	= 0x00000013, /* modem + adb + mass_storage */
	},
	{
		.product_id	= 0x0c05,
		.functions	= 0x00000021, /* Projector + mass_storage */
	},
	{
		.product_id	= 0x0c06,
		.functions	= 0x00000023, /* Projector + adb + mass_storage */
	},
	{
		.product_id	= 0x0c07,
		.functions	= 0x0000000B, /* diag + adb + mass_storage */
	},
	{
		.product_id = 0x0c08,
		.functions	= 0x00000009, /* diag + mass_storage */
	},
	{
		.product_id	= 0x0c09,
		.functions	= 0x0000001B, /* adb + mass_storage + diag + modem */
	},
	{
		.product_id	= 0x0c88,
		.functions	= 0x0000011B, /* adb + mass_storage + diag + modem + nmea */
	},
	{
		.product_id	= 0x0c89,
		.functions	= 0x00000113, /* adb + mass_storage + modem +nmea */
	},
	{
		.product_id	= 0x0FFE,
		.functions	= 0x00000004, /* internet sharing */
	},

};
#endif

struct msm_hsusb_platform_data msm_hsusb_pdata = {
	.phy_reset = internal_phy_reset,
	.phy_init_seq = hsusb_phy_init_seq,
#ifdef CONFIG_USB_FUNCTION
	.vendor_id = 0x0bb4,
	.product_id = 0x0c02,
	.version = 0x0100,
	.product_name = "Android Phone",
	.manufacturer_name = "HTC",

	.functions = usb_functions,
	.num_functions = ARRAY_SIZE(usb_functions),
	.products = usb_products,
	.num_products = ARRAY_SIZE(usb_products),
#endif
};

static struct resource resources_hsusb[] = {
	{
		.start	= MSM_HSUSB_PHYS,
		.end	= MSM_HSUSB_PHYS + MSM_HSUSB_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HS,
		.end	= INT_USB_HS,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_hsusb = {
	.name		= "msm_hsusb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_hsusb),
	.resource	= resources_hsusb,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
		.platform_data = &msm_hsusb_pdata,
	},
};

#ifdef CONFIG_USB_ANDROID
static struct android_usb_platform_data android_usb_pdata = {
	.vendor_id	= 0x0bb4,
	.product_id	= 0x0c01,
	.adb_product_id	= 0x0c02,
	.version	= 0x0100,
	.product_name	= "Android Phone",
	.manufacturer_name = "HTC",
	.nluns = 1,
};

static struct platform_device android_usb_device = {
	.name	= "android_usb",
	.id		= -1,
	.dev		= {
		.platform_data = &android_usb_pdata,
	},
};
#endif

#ifdef CONFIG_USB_FUNCTION_MASS_STORAGE
static struct usb_mass_storage_platform_data mass_storage_pdata = {
	.nluns = 1,
	.buf_size = 16384,
	.vendor = "HTC     ",
	.product = "Android Phone   ",
	.release = 0x0100,
};

static struct platform_device msm_device_usb_mass_storage = {
	.name = "usb_mass_storage",
	.id = -1,
	.dev = {
		.platform_data = &mass_storage_pdata,
		},
};
#endif
void __init msm_add_usb_devices(void (*phy_reset) (void), void (*phy_shutdown) (void))
{
	if (phy_reset)
		msm_hsusb_pdata.phy_reset = phy_reset;

	if (phy_shutdown)
		msm_hsusb_pdata.phy_shutdown = phy_shutdown;

	platform_device_register(&msm_device_hsusb);
#ifdef CONFIG_USB_FUNCTION_MASS_STORAGE
	platform_device_register(&msm_device_usb_mass_storage);
#endif
}

static struct android_pmem_platform_data pmem_pdata = {
	.name = "pmem",
	.no_allocator = 1,
	.cached = 1,
};

static struct android_pmem_platform_data pmem_adsp_pdata = {
	.name = "pmem_adsp",
	.no_allocator = 0,
	.cached = 0,
};

static struct android_pmem_platform_data pmem_camera_pdata = {
	.name = "pmem_camera",
	.no_allocator = 1,
	.cached = 0,
};

static struct android_pmem_platform_data pmem_gpu0_pdata = {
	.name = "pmem_gpu0",
	.no_allocator = 1,
	.cached = 0,
	.buffered = 1,
};

static struct android_pmem_platform_data pmem_gpu1_pdata = {
	.name = "pmem_gpu1",
	.no_allocator = 1,
	.cached = 0,
	.buffered = 1,
};

static struct platform_device pmem_device = {
	.name = "android_pmem",
	.id = 0,
	.dev = { .platform_data = &pmem_pdata },
};

static struct platform_device pmem_adsp_device = {
	.name = "android_pmem",
	.id = 1,
	.dev = { .platform_data = &pmem_adsp_pdata },
};

static struct platform_device pmem_gpu0_device = {
	.name = "android_pmem",
	.id = 2,
	.dev = { .platform_data = &pmem_gpu0_pdata },
};

static struct platform_device pmem_gpu1_device = {
	.name = "android_pmem",
	.id = 3,
	.dev = { .platform_data = &pmem_gpu1_pdata },
};

static struct platform_device pmem_camera_device = {
	.name = "android_pmem",
	.id = 4,
	.dev = { .platform_data = &pmem_camera_pdata },
};

static struct resource ram_console_resource[] = {
	{
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device ram_console_device = {
	.name = "ram_console",
	.id = -1,
	.num_resources  = ARRAY_SIZE(ram_console_resource),
	.resource       = ram_console_resource,
};

void __init msm_add_mem_devices(struct msm_pmem_setting *setting)
{
	if (setting->pmem_size) {
		pmem_pdata.start = setting->pmem_start;
		pmem_pdata.size = setting->pmem_size;
		platform_device_register(&pmem_device);
	}

	if (setting->pmem_adsp_size) {
		pmem_adsp_pdata.start = setting->pmem_adsp_start;
		pmem_adsp_pdata.size = setting->pmem_adsp_size;
		platform_device_register(&pmem_adsp_device);
	}

	if (setting->pmem_gpu0_size) {
		pmem_gpu0_pdata.start = setting->pmem_gpu0_start;
		pmem_gpu0_pdata.size = setting->pmem_gpu0_size;
		platform_device_register(&pmem_gpu0_device);
	}

	if (setting->pmem_gpu1_size) {
		pmem_gpu1_pdata.start = setting->pmem_gpu1_start;
		pmem_gpu1_pdata.size = setting->pmem_gpu1_size;
		platform_device_register(&pmem_gpu1_device);
	}

	if (setting->pmem_camera_size) {
		pmem_camera_pdata.start = setting->pmem_camera_start;
		pmem_camera_pdata.size = setting->pmem_camera_size;
		platform_device_register(&pmem_camera_device);
	}

	if (setting->ram_console_size) {
		ram_console_resource[0].start = setting->ram_console_start;
		ram_console_resource[0].end = setting->ram_console_start
			+ setting->ram_console_size - 1;
		platform_device_register(&ram_console_device);
	}
}

#if 0
static struct platform_device *msm_serial_devices[] __initdata = {
	&msm_device_uart1,
	&msm_device_uart2,
	&msm_device_uart3,
	#ifdef CONFIG_SERIAL_MSM_HS
	&msm_device_uart_dm1,
	&msm_device_uart_dm2,
	#endif
};

int __init msm_add_serial_devices(unsigned num)
{
	if (num > MSM_SERIAL_NUM)
		return -EINVAL;

	return platform_device_register(msm_serial_devices[num]);
}
#endif

#define ATAG_SMI 0x4d534D71
/* setup calls mach->fixup, then parse_tags, parse_cmdline
 * We need to setup meminfo in mach->fixup, so this function
 * will need to traverse each tag to find smi tag.
 */
int __init parse_tag_smi(const struct tag *tags)
{
	int smi_sz = 0, find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_SMI) {
			printk(KERN_DEBUG "find the smi tag\n");
			find = 1;
			break;
		}
	}
	if (!find)
		return -1;

	printk(KERN_DEBUG "parse_tag_smi: smi size = %d\n", t->u.mem.size);
	smi_sz = t->u.mem.size;
	return smi_sz;
}
__tagtable(ATAG_SMI, parse_tag_smi);


#define ATAG_HWID 0x4d534D72
int __init parse_tag_hwid(const struct tag *tags)
{
	int hwid = 0, find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_HWID) {
			printk(KERN_DEBUG "find the hwid tag\n");
			find = 1;
			break;
		}
	}

	if (find)
		hwid = t->u.revision.rev;
	printk(KERN_DEBUG "parse_tag_hwid: hwid = 0x%x\n", hwid);
	return hwid;
}
__tagtable(ATAG_HWID, parse_tag_hwid);

#define ATAG_SKUID 0x4d534D73
int __init parse_tag_skuid(const struct tag *tags)
{
	int skuid = 0, find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_SKUID) {
			printk(KERN_DEBUG "find the skuid tag\n");
			find = 1;
			break;
		}
	}

	if (find)
		skuid = t->u.revision.rev;
	printk(KERN_DEBUG "parse_tag_skuid: hwid = 0x%x\n", skuid);
	return skuid;
}
__tagtable(ATAG_SKUID, parse_tag_skuid);

#define ATAG_ENGINEERID 0x4d534D75
int __init parse_tag_engineerid(const struct tag *tags)
{
	int engineerid = 0, find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_ENGINEERID) {
			printk(KERN_DEBUG "find the engineer tag\n");
			find = 1;
			break;
		}
	}

	if (find)
		engineerid = t->u.revision.rev;
	printk(KERN_DEBUG "parse_tag_engineerid: hwid = 0x%x\n", engineerid);
	return engineerid;
}
__tagtable(ATAG_ENGINEERID, parse_tag_engineerid);

#define ATAG_MONODIE 0x4d534D76
static int mono_die;;
int __init parse_tag_monodie(const struct tag *tags)
{
	int find = 0;
	struct tag *t = (struct tag *)tags;

	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_MONODIE) {
			printk(KERN_DEBUG "find the flash id tag\n");
			find = 1;
			break;
		}
	}

	if (find)
		mono_die = t->u.revision.rev;
	printk(KERN_DEBUG "parse_tag_monodie: mono-die = 0x%x\n", mono_die);
	return mono_die;
}
__tagtable(ATAG_MONODIE, parse_tag_monodie);

int __init board_mcp_monodie(void)
{
	return mono_die;
}

static int mfg_mode;
int __init board_mfg_mode_init(char *s)
{
	if (!strcmp(s, "normal"))
		mfg_mode = 0;
	else if (!strcmp(s, "factory2"))
		mfg_mode = 1;
	else if (!strcmp(s, "recovery"))
		mfg_mode = 2;
	else if (!strcmp(s, "charge"))
		mfg_mode = 3;

	return 1;
}
__setup("androidboot.mode=", board_mfg_mode_init);


int board_mfg_mode(void)
{
	return mfg_mode;
}

static int __init board_serialno_setup(char *serialno)
{
	char *str;

	/* use default serial number when mode is factory2 */
	if (mfg_mode == 1 || !strlen(serialno))
		str = df_serialno;
	else
		str = serialno;
#ifdef CONFIG_USB_FUNCTION
	msm_hsusb_pdata.serial_number = str;
#endif
#ifdef CONFIG_USB_ANDROID
	android_usb_pdata.serial_number = str;
#endif
	return 1;
}

__setup("androidboot.serialno=", board_serialno_setup);

EXPORT_SYMBOL(board_mfg_mode);
static char *keycap_tag = NULL;
static int __init board_keycaps_tag(char *get_keypads)
{
	if(strlen(get_keypads))
		keycap_tag = get_keypads;
	else
		keycap_tag = NULL;
	return 1;
}
__setup("androidboot.keycaps=", board_keycaps_tag);
void board_get_keycaps_tag(char **ret_data)
{
	*ret_data = keycap_tag;
}
EXPORT_SYMBOL(board_get_keycaps_tag);

static char *cid_tag = NULL;
static int __init board_set_cid_tag(char *get_hboot_cid)
{
	if(strlen(get_hboot_cid))
		cid_tag = get_hboot_cid;
	else
		cid_tag = NULL;
	return 1;
}
__setup("androidboot.cid=", board_set_cid_tag);
void board_get_cid_tag(char **ret_data)
{
	*ret_data = cid_tag;
}
EXPORT_SYMBOL(board_get_cid_tag);

static char *carrier_tag = NULL;
static int __init board_set_carrier_tag(char *get_hboot_carrier)
{
	if(strlen(get_hboot_carrier))
		carrier_tag = get_hboot_carrier;
	else
		carrier_tag = NULL;
	return 1;
}
__setup("androidboot.carrier=", board_set_carrier_tag);
void board_get_carrier_tag(char **ret_data)
{
	*ret_data = carrier_tag;
}
EXPORT_SYMBOL(board_get_carrier_tag);

static char *mid_tag = NULL;
static int __init board_set_mid_tag(char *get_hboot_mid)
{
	if(strlen(get_hboot_mid))
		mid_tag = get_hboot_mid;
	else
		mid_tag = NULL;
	return 1;
}
__setup("androidboot.mid=", board_set_mid_tag);
void board_get_mid_tag(char **ret_data)
{
	*ret_data = mid_tag;
}
EXPORT_SYMBOL(board_get_mid_tag);
