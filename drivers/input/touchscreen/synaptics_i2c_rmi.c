/* drivers/input/keyboard/synaptics_i2c_rmi.c
 *
 * Copyright (C) 2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/synaptics_i2c_rmi.h>

static struct workqueue_struct *synaptics_wq;

struct synaptics_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_irq;
	bool has_relative_report;
	struct hrtimer timer;
	struct work_struct  work;
	uint16_t max[2];
	int snap_state[2][2];
	int snap_down_on[2];
	int snap_down_off[2];
	int snap_up_on[2];
	int snap_up_off[2];
	int snap_down[2];
	int snap_up[2];
	uint32_t flags;
	int reported_finger_count;
	int8_t sensitivity_adjust;
	uint32_t dup_threshold;
	int (*power)(int on);
	struct early_suspend early_suspend;
	uint8_t first_pressed;
	uint8_t key_pressed;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void synaptics_ts_early_suspend(struct early_suspend *h);
static void synaptics_ts_late_resume(struct early_suspend *h);
#endif

static const char SYNAPTICSNAME[]	= "Synaptics-touch";
static uint32_t syn_panel_version;

static ssize_t touch_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "%s_%#x\n", SYNAPTICSNAME, syn_panel_version);
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(vendor, 0444, touch_vendor_show, NULL);

static struct kobject *android_touch_kobj;

static int touch_sysfs_init(void)
{
	int ret;

	android_touch_kobj = kobject_create_and_add("android_touch", NULL);
	if (android_touch_kobj == NULL) {
		printk(KERN_ERR
		       "touch_sysfs_init: subsystem_register failed\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = sysfs_create_file(android_touch_kobj, &dev_attr_vendor.attr);
	if (ret) {
		printk(KERN_ERR
		       "touch_sysfs_init: sysfs_create_group failed\n");
		goto err4;
	}

	return 0;
err4:
	kobject_del(android_touch_kobj);
err:
	return ret;
}

#define ENABLE_IME_IMPROVEMENT

#ifdef ENABLE_IME_IMPROVEMENT
#define MULTI_FINGER_NUMBER 2

static int ime_threshold;
static int report_x[MULTI_FINGER_NUMBER];
static int report_y[MULTI_FINGER_NUMBER];
static int pixel_x;
static int pixel_y;
static int touch_hit;
static int ime_work_area[4];
static int ime_work_area_x1;
static int ime_work_area_x2;
static int ime_work_area_y1;
static int ime_work_area_y2;
static int display_width;
static int display_height;
static int touch_screen_margin_left;
static int touch_screen_margin_right;
static int touch_screen_margin_top;
static int touch_screen_margin_bottom;

static ssize_t ime_threshold_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ime_threshold);
}

static ssize_t ime_threshold_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	char *ptr_data = (char *)buf;
	unsigned long val;

	val = simple_strtoul(ptr_data, NULL, 10);

	if (val >= 0 && val <= max(display_width, display_height))
		ime_threshold = val;
	else
		ime_threshold = 0;
	return count;
}

static ssize_t ime_work_area_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d,%d,%d,%d\n", ime_work_area[0],
			ime_work_area[1], ime_work_area[2], ime_work_area[3]);
}

static ssize_t ime_work_area_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	char *ptr_data = (char *)buf;
	char *p;
	int pt_count = 0;
	unsigned long val[4];

	while ((p = strsep(&ptr_data, ","))) {
		if (!*p)
			break;

		if (pt_count >= 4)
			break;

		// if(strict_strtoul(p, 10, val[pt_count])) return count;
		val[pt_count] = simple_strtoul(p, NULL, 10);

		pt_count++;
	}

	if (pt_count >= 4 && display_width && display_height) {
		ime_work_area[0] = val[0];
		ime_work_area[1] = val[1];
		ime_work_area[2] = val[2];
		ime_work_area[3] = val[3];

		if (val[0] <= 1)
			ime_work_area_x1 = touch_screen_margin_left;
		else
			ime_work_area_x1 = touch_screen_margin_left +
				(val[0] * (touch_screen_margin_right -
				touch_screen_margin_left) / display_width);

		if (val[1] >= display_width - 1)
			ime_work_area_x2 = touch_screen_margin_right;
		else
			ime_work_area_x2 = touch_screen_margin_left +
				(val[1] * (touch_screen_margin_right -
				touch_screen_margin_left) / display_width);

		if (val[2] <= 1)
			ime_work_area_y1 = touch_screen_margin_top;
		else
			ime_work_area_y1 = touch_screen_margin_top +
				(val[2] * (touch_screen_margin_bottom -
				touch_screen_margin_top) / display_height);

		if (val[3] >= display_height - 1)
			ime_work_area_y2 = touch_screen_margin_bottom;
		else
			ime_work_area_y2 = touch_screen_margin_top +
				(val[3] * (touch_screen_margin_bottom -
				touch_screen_margin_top) / display_height);
	}

	return count;
}

static void clear_queue(void)
{
	/* Clear report point coordinate */
	int i;

	for (i = 0; i < MULTI_FINGER_NUMBER; i++) {
		report_x[i] = 0;
		report_y[i] = 0;
	}

	touch_hit = 0;
}

/* sys/class/input/input1/ime_threshold */
static DEVICE_ATTR(ime_threshold, 0666, ime_threshold_show,
		ime_threshold_store);
static DEVICE_ATTR(ime_work_area, 0666, ime_work_area_show,
		ime_work_area_store);
#endif

static int synaptics_init_panel(struct synaptics_ts_data *ts)
{
	int ret;

	ret = i2c_smbus_write_byte_data(ts->client, 0xff, 0x10); /* page select = 0x10 */
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_write_byte_data failed for page select\n");
		goto err_page_select_failed;
	}
	ret = i2c_smbus_write_byte_data(ts->client, 0x41, 0x04); /* Set "No Clip Z" */
	if (ret < 0)
		printk(KERN_ERR "i2c_smbus_write_byte_data failed for No Clip Z\n");

	ret = i2c_smbus_write_byte_data(ts->client, 0x44,
					ts->sensitivity_adjust);
	if (ret < 0)
		pr_err("synaptics_ts: failed to set Sensitivity Adjust\n");

err_page_select_failed:
	ret = i2c_smbus_write_byte_data(ts->client, 0xff, 0x04); /* page select = 0x04 */
	if (ret < 0)
		printk(KERN_ERR "i2c_smbus_write_byte_data failed for page select\n");
	ret = i2c_smbus_write_byte_data(ts->client, 0xf0, 0x81); /* normal operation, 80 reports per second */
	if (ret < 0)
		printk(KERN_ERR "synaptics_ts_resume: i2c_smbus_write_byte_data failed\n");
	return ret;
}


static void synaptics_ts_work_func(struct work_struct *work)
{
	int i;
	int ret;
	int bad_data = 0;
	struct i2c_msg msg[2];
	uint8_t start_reg;
	uint8_t buf[15];
	struct synaptics_ts_data *ts = container_of(work, struct synaptics_ts_data, work);
	int buf_len = ts->has_relative_report ? 15 : 13;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &start_reg;
	start_reg = 0x00;
	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = buf_len;
	msg[1].buf = buf;

	/* printk("synaptics_ts_work_func\n"); */
	for (i = 0; i < ((ts->use_irq && !bad_data) ? 1 : 10); i++) {
		ret = i2c_transfer(ts->client->adapter, msg, 2);
		if (ret < 0) {
			printk(KERN_ERR "synaptics_ts_work_func: i2c_transfer failed\n");
			bad_data = 1;
		} else {
			/* printk("synaptics_ts_work_func: %x %x %x %x %x %x" */
			/*	" %x %x %x %x %x %x %x %x %x, ret %d\n", */
			/*	buf[0], buf[1], buf[2], buf[3], */
			/*	buf[4], buf[5], buf[6], buf[7], */
			/*	buf[8], buf[9], buf[10], buf[11], */
			/*	buf[12], buf[13], buf[14], ret); */
			if ((buf[buf_len - 1] & 0xc0) != 0x40) {
				printk(KERN_WARNING "synaptics_ts_work_func:"
				       " bad read %x %x %x %x %x %x %x %x %x"
				       " %x %x %x %x %x %x, ret %d\n",
				       buf[0], buf[1], buf[2], buf[3],
				       buf[4], buf[5], buf[6], buf[7],
				       buf[8], buf[9], buf[10], buf[11],
				       buf[12], buf[13], buf[14], ret);
				if (bad_data)
					synaptics_init_panel(ts);
				bad_data = 1;
				continue;
			}
			bad_data = 0;
			if ((buf[buf_len - 1] & 1) == 0) {
				/* printk("read %d coordinates\n", i); */
				break;
			} else {
				int pos[2][2];
				int f, a;
				int base;
				/* int x = buf[3] | (uint16_t)(buf[2] & 0x1f) << 8; */
				/* int y = buf[5] | (uint16_t)(buf[4] & 0x1f) << 8; */
				int z = buf[1];
				int w = buf[0] >> 4;
				int finger = buf[0] & 7;

				/* int x2 = buf[3+6] | (uint16_t)(buf[2+6] & 0x1f) << 8; */
				/* int y2 = buf[5+6] | (uint16_t)(buf[4+6] & 0x1f) << 8; */
				/* int z2 = buf[1+6]; */
				/* int w2 = buf[0+6] >> 4; */
				/* int finger2 = buf[0+6] & 7; */

				/* int dx = (int8_t)buf[12]; */
				/* int dy = (int8_t)buf[13]; */
				int finger2_pressed;

				/* printk("x %4d, y %4d, z %3d, w %2d, F %d, 2nd: x %4d, y %4d, z %3d, w %2d, F %d, dx %4d, dy %4d\n", */
				/*	x, y, z, w, finger, */
				/*	x2, y2, z2, w2, finger2, */
				/*	dx, dy); */

				base = 2;
				for (f = 0; f < 2; f++) {
					uint32_t flip_flag = SYNAPTICS_FLIP_X;
					for (a = 0; a < 2; a++) {
						int p = buf[base + 1];
						p |= (uint16_t)(buf[base] & 0x1f) << 8;
						if (ts->flags & flip_flag)
							p = ts->max[a] - p;
						if (ts->flags & SYNAPTICS_SNAP_TO_INACTIVE_EDGE) {
							if (ts->snap_state[f][a]) {
								if (p <= ts->snap_down_off[a])
									p = ts->snap_down[a];
								else if (p >= ts->snap_up_off[a])
									p = ts->snap_up[a];
								else
									ts->snap_state[f][a] = 0;
							} else {
								if (p <= ts->snap_down_on[a]) {
									p = ts->snap_down[a];
									ts->snap_state[f][a] = 1;
								} else if (p >= ts->snap_up_on[a]) {
									p = ts->snap_up[a];
									ts->snap_state[f][a] = 1;
								}
							}
						}
						pos[f][a] = p;
						base += 2;
						flip_flag <<= 1;
					}
					base += 2;
					if (ts->flags & SYNAPTICS_SWAP_XY)
						swap(pos[f][0], pos[f][1]);
				}
#ifdef CONFIG_TOUCHSCREEN_COMPATIBLE_REPORT
				if (z) {
					input_report_abs(ts->input_dev, ABS_X, pos[0][0]);
					input_report_abs(ts->input_dev, ABS_Y, pos[0][1]);
				}
				input_report_abs(ts->input_dev, ABS_PRESSURE, z);
				input_report_abs(ts->input_dev, ABS_TOOL_WIDTH, w);
				input_report_key(ts->input_dev, BTN_TOUCH, finger);
				finger2_pressed = finger > 1 && finger != 7;
				input_report_key(ts->input_dev, BTN_2, finger2_pressed);
				if (finger2_pressed) {
					input_report_abs(ts->input_dev, ABS_HAT0X, pos[1][0]);
					input_report_abs(ts->input_dev, ABS_HAT0Y, pos[1][1]);
				}
#else
				finger2_pressed = finger > 1 && finger != 7;
#endif
				if (!finger) {
					if (ts->key_pressed) {
						ts->key_pressed = 0;
						if (!ts->first_pressed) {
							ts->first_pressed = 1;
							printk(KERN_INFO "E@%d, %d\n",
								pos[0][0], pos[0][1]);
						}
					}
					z = 0;
				}

#ifdef ENABLE_IME_IMPROVEMENT
				if (ime_threshold > 0) {
					int point_moved = 0; /* for ENABLE_IME_IMPROVEMENT */
					int ime_multi_touch = 0;
					int dx = 0;
					int dy = 0;

					if (finger2_pressed) {
						ime_multi_touch = 1;
					} else if ((pos[0][0] >= ime_work_area_x1 && pos[0][0] <= ime_work_area_x2) &&
						(pos[0][1] >= ime_work_area_y1 && pos[0][1] <= ime_work_area_y2)) {
						dx = abs(pos[0][0] - report_x[0]);
						dy = abs(pos[0][1] - report_y[0]);

						if ((dx > pixel_x * ime_threshold) || (dy > pixel_y * ime_threshold)) {
							report_x[0] = pos[0][0];
							report_y[0] = pos[0][1];
							point_moved = 1;
						}
						if (z == 0) {
							clear_queue();
							point_moved = 1;
						}
					} else { /* Not in IME work area */
						point_moved = 1;
					}

					if (!point_moved || ime_multi_touch)
						break;
				}
#endif

#ifdef CONFIG_TOUCHSCREEN_CONCATENATE_REPORT
				/**
				 * We concatenate z, w, x, y info to reduce the number of reports in event hub
				 */
#ifdef CONFIG_TOUCHSCREEN_DUPLICATED_FILTER
				/**
				 * Small movement report would seem as duplicated report, discard it
				 */
				int drift_x[2];
				int drift_y[2];
				uint8_t discard[2] = {0, 0};
				static int ref_x[2], ref_y[2];

				drift_x[0] = abs(ref_x[0] - pos[0][0]);
				drift_y[0] = abs(ref_y[0] - pos[0][1]);
				if (finger2_pressed) {
					drift_x[1] = abs(ref_x[1] - pos[1][0]);
					drift_y[1] = abs(ref_y[1] - pos[1][1]);
				}
				/* printk("ref_x :%d, ref_y: %d, x: %d, y: %d\n", ref_x, ref_y, pos[0][0], pos[0][1]); */
				if (drift_x[0] < ts->dup_threshold && drift_y[0] < ts->dup_threshold && z != 0) {
					/* if drift position < threshold, it would seem as duplicated report event and discard */
					/* printk("ref_x :%d, ref_y: %d, x: %d, y: %d\n", ref_x[0], ref_y[0], pos[0][0], pos[0][1]); */
					discard[0] = 1;
				}
				if (!finger2_pressed || (drift_x[1] < ts->dup_threshold && drift_y[1] < ts->dup_threshold)) {
					discard[1] = 1;
				}
				if (discard[0] && discard[1]) {
					/* if finger 0 and finger 1's movement < threshold , discard it. */
					break;
				}
				ref_x[0] = pos[0][0];
				ref_y[0] = pos[0][1];
				if (finger2_pressed) {
					ref_x[1] = pos[1][0];
					ref_y[1] = pos[1][1];
				}
				if (z == 0) {
					ref_x[0] = ref_x[1] = 0;
					ref_y[0] = ref_y[1] = 0;
				}
#endif
				input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE, z << 16 | w);
				input_report_abs(ts->input_dev, ABS_MT_POSITION, (!finger2_pressed) << 31 | pos[0][0] << 16 | pos[0][1]);
				if (finger2_pressed) {
					input_report_abs(ts->input_dev, ABS_MT_AMPLITUDE, z << 16 | w);
					input_report_abs(ts->input_dev, ABS_MT_POSITION, 1 << 31 | pos[1][0] << 16 | pos[1][1]);
				}
#else
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, z);
				input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, pos[0][0]);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, pos[0][1]);
				input_mt_sync(ts->input_dev);
				if (finger2_pressed) {
					input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, z);
					input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, w);
					input_report_abs(ts->input_dev, ABS_MT_POSITION_X, pos[1][0]);
					input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, pos[1][1]);
					input_mt_sync(ts->input_dev);
				} else if (ts->reported_finger_count > 1) {
					input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
					input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
					input_mt_sync(ts->input_dev);
				}
				ts->reported_finger_count = finger;
				input_sync(ts->input_dev);
#endif
				if (!ts->key_pressed && finger) {
					ts->key_pressed = 1;
					if (!ts->first_pressed)
						printk(KERN_INFO "S@%d, %d\n",
							pos[0][0], pos[0][1]);
				}
			}
		}
	}
	if (ts->use_irq)
		enable_irq(ts->client->irq);
}

static enum hrtimer_restart synaptics_ts_timer_func(struct hrtimer *timer)
{
	struct synaptics_ts_data *ts = container_of(timer, struct synaptics_ts_data, timer);
	/* printk("synaptics_ts_timer_func\n"); */

	queue_work(synaptics_wq, &ts->work);

	hrtimer_start(&ts->timer, ktime_set(0, 12500000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static irqreturn_t synaptics_ts_irq_handler(int irq, void *dev_id)
{
	struct synaptics_ts_data *ts = dev_id;

	/* printk("synaptics_ts_irq_handler\n"); */
	disable_irq_nosync(ts->client->irq);
	queue_work(synaptics_wq, &ts->work);
	return IRQ_HANDLED;
}

static int synaptics_ts_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	struct synaptics_ts_data *ts;
	uint8_t buf0[4];
	uint8_t buf1[8];
	struct i2c_msg msg[2];
	int ret = 0;
	uint16_t max_x, max_y;
	int fuzz_x, fuzz_y, fuzz_p, fuzz_w;
	struct synaptics_i2c_rmi_platform_data *pdata;
	unsigned long irqflags;
	int inactive_area_left;
	int inactive_area_right;
	int inactive_area_top;
	int inactive_area_bottom;
	int snap_left_on;
	int snap_left_off;
	int snap_right_on;
	int snap_right_off;
	int snap_top_on;
	int snap_top_off;
	int snap_bottom_on;
	int snap_bottom_off;
	uint32_t panel_version;
#ifdef ENABLE_IME_IMPROVEMENT
	int i;
#endif

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk(KERN_ERR "synaptics_ts_probe: need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}
	INIT_WORK(&ts->work, synaptics_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;
	if (pdata)
		ts->power = pdata->power;
	if (ts->power) {
		ret = ts->power(1);
		if (ret < 0) {
			printk(KERN_ERR "synaptics_ts_probe power on failed\n");
			goto err_power_failed;
		}
	}

	ret = i2c_smbus_write_byte_data(ts->client, 0xf4, 0x01); /* device command = reset */
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_write_byte_data failed\n");
		/* fail? */
	}
	{
		int retry = 10;
		while (retry-- > 0) {
			ret = i2c_smbus_read_byte_data(ts->client, 0xe4);
			if (ret >= 0)
				break;
			msleep(100);
		}
	}
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_byte_data failed\n");
		goto err_detect_failed;
	}
	printk(KERN_INFO "synaptics_ts_probe: Product Major Version %x\n", ret);
	panel_version = ret << 8;
	ret = i2c_smbus_read_byte_data(ts->client, 0xe5);
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_byte_data failed\n");
		goto err_detect_failed;
	}
	printk(KERN_INFO "synaptics_ts_probe: Product Minor Version %x\n", ret);
	panel_version |= ret;

	ret = i2c_smbus_read_byte_data(ts->client, 0xe3);
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_byte_data failed\n");
		goto err_detect_failed;
	}
	printk(KERN_INFO "synaptics_ts_probe: product property %x\n", ret);

	if (pdata) {
		while (pdata->version > panel_version)
			pdata++;
		ts->flags = pdata->flags;
		ts->sensitivity_adjust = pdata->sensitivity_adjust;
		irqflags = pdata->irqflags;
		inactive_area_left = pdata->inactive_left;
		inactive_area_right = pdata->inactive_right;
		inactive_area_top = pdata->inactive_top;
		inactive_area_bottom = pdata->inactive_bottom;
		snap_left_on = pdata->snap_left_on;
		snap_left_off = pdata->snap_left_off;
		snap_right_on = pdata->snap_right_on;
		snap_right_off = pdata->snap_right_off;
		snap_top_on = pdata->snap_top_on;
		snap_top_off = pdata->snap_top_off;
		snap_bottom_on = pdata->snap_bottom_on;
		snap_bottom_off = pdata->snap_bottom_off;
		fuzz_x = pdata->fuzz_x;
		fuzz_y = pdata->fuzz_y;
		fuzz_p = pdata->fuzz_p;
		fuzz_w = pdata->fuzz_w;
		ts->dup_threshold = pdata->dup_threshold;
#ifdef ENABLE_IME_IMPROVEMENT
		display_width = pdata->display_width;
		display_height = pdata->display_height;
#endif
	} else {
		irqflags = 0;
		inactive_area_left = 0;
		inactive_area_right = 0;
		inactive_area_top = 0;
		inactive_area_bottom = 0;
		snap_left_on = 0;
		snap_left_off = 0;
		snap_right_on = 0;
		snap_right_off = 0;
		snap_top_on = 0;
		snap_top_off = 0;
		snap_bottom_on = 0;
		snap_bottom_off = 0;
		fuzz_x = 0;
		fuzz_y = 0;
		fuzz_p = 0;
		fuzz_w = 0;
#ifdef ENABLE_IME_IMPROVEMENT
		display_width = 240;
		display_height = 320;
#endif
	}

	ret = i2c_smbus_read_byte_data(ts->client, 0xf0);
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_byte_data failed\n");
		goto err_detect_failed;
	}
	printk(KERN_INFO "synaptics_ts_probe: device control %x\n", ret);

	ret = i2c_smbus_read_byte_data(ts->client, 0xf1);
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_byte_data failed\n");
		goto err_detect_failed;
	}
	printk(KERN_INFO "synaptics_ts_probe: interrupt enable %x\n", ret);

	ret = i2c_smbus_write_byte_data(ts->client, 0xf1, 0); /* disable interrupt */
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_write_byte_data failed\n");
		goto err_detect_failed;
	}

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf0;
	buf0[0] = 0xe0;
	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 8;
	msg[1].buf = buf1;
	ret = i2c_transfer(ts->client->adapter, msg, 2);
	if (ret < 0) {
		printk(KERN_ERR "i2c_transfer failed\n");
		goto err_detect_failed;
	}
	printk(KERN_INFO "synaptics_ts_probe: 0xe0: %x %x %x %x %x %x %x %x\n",
	       buf1[0], buf1[1], buf1[2], buf1[3],
	       buf1[4], buf1[5], buf1[6], buf1[7]);

	ret = i2c_smbus_write_byte_data(ts->client, 0xff, 0x10); /* page select = 0x10 */
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_write_byte_data failed for page select\n");
		goto err_detect_failed;
	}
	ret = i2c_smbus_read_word_data(ts->client, 0x02);
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_word_data failed\n");
		goto err_detect_failed;
	}
	ts->has_relative_report = !(ret & 0x100);
	printk(KERN_INFO "synaptics_ts_probe: Sensor properties %x\n", ret);
	ret = i2c_smbus_read_word_data(ts->client, 0x04);
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_word_data failed\n");
		goto err_detect_failed;
	}
	ts->max[0] = max_x = (ret >> 8 & 0xff) | ((ret & 0x1f) << 8);
	ret = i2c_smbus_read_word_data(ts->client, 0x06);
	if (ret < 0) {
		printk(KERN_ERR "i2c_smbus_read_word_data failed\n");
		goto err_detect_failed;
	}
	ts->max[1] = max_y = (ret >> 8 & 0xff) | ((ret & 0x1f) << 8);
	if (ts->flags & SYNAPTICS_SWAP_XY)
		swap(max_x, max_y);

	ret = synaptics_init_panel(ts); /* will also switch back to page 0x04 */
	if (ret < 0) {
		printk(KERN_ERR "synaptics_init_panel failed\n");
		goto err_detect_failed;
	}

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		printk(KERN_ERR "synaptics_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}
	ts->input_dev->name = "synaptics-rmi-touchscreen";
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(BTN_2, ts->input_dev->keybit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	inactive_area_left = inactive_area_left * max_x / 0x10000;
	inactive_area_right = inactive_area_right * max_x / 0x10000;
	inactive_area_top = inactive_area_top * max_y / 0x10000;
	inactive_area_bottom = inactive_area_bottom * max_y / 0x10000;
	snap_left_on = snap_left_on * max_x / 0x10000;
	snap_left_off = snap_left_off * max_x / 0x10000;
	snap_right_on = snap_right_on * max_x / 0x10000;
	snap_right_off = snap_right_off * max_x / 0x10000;
	snap_top_on = snap_top_on * max_y / 0x10000;
	snap_top_off = snap_top_off * max_y / 0x10000;
	snap_bottom_on = snap_bottom_on * max_y / 0x10000;
	snap_bottom_off = snap_bottom_off * max_y / 0x10000;
	fuzz_x = fuzz_x * max_x / 0x10000;
	fuzz_y = fuzz_y * max_y / 0x10000;
	ts->snap_down[!!(ts->flags & SYNAPTICS_SWAP_XY)] = -inactive_area_left;
	ts->snap_up[!!(ts->flags & SYNAPTICS_SWAP_XY)] = max_x + inactive_area_right;
	ts->snap_down[!(ts->flags & SYNAPTICS_SWAP_XY)] = -inactive_area_top;
	ts->snap_up[!(ts->flags & SYNAPTICS_SWAP_XY)] = max_y + inactive_area_bottom;
	ts->snap_down_on[!!(ts->flags & SYNAPTICS_SWAP_XY)] = snap_left_on;
	ts->snap_down_off[!!(ts->flags & SYNAPTICS_SWAP_XY)] = snap_left_off;
	ts->snap_up_on[!!(ts->flags & SYNAPTICS_SWAP_XY)] = max_x - snap_right_on;
	ts->snap_up_off[!!(ts->flags & SYNAPTICS_SWAP_XY)] = max_x - snap_right_off;
	ts->snap_down_on[!(ts->flags & SYNAPTICS_SWAP_XY)] = snap_top_on;
	ts->snap_down_off[!(ts->flags & SYNAPTICS_SWAP_XY)] = snap_top_off;
	ts->snap_up_on[!(ts->flags & SYNAPTICS_SWAP_XY)] = max_y - snap_bottom_on;
	ts->snap_up_off[!(ts->flags & SYNAPTICS_SWAP_XY)] = max_y - snap_bottom_off;
	printk(KERN_INFO "synaptics_ts_probe: max_x %d, max_y %d\n", max_x, max_y);
	printk(KERN_INFO "synaptics_ts_probe: inactive_x %d %d, inactive_y %d %d\n",
	       inactive_area_left, inactive_area_right,
	       inactive_area_top, inactive_area_bottom);
	printk(KERN_INFO "synaptics_ts_probe: snap_x %d-%d %d-%d, snap_y %d-%d %d-%d\n",
	       snap_left_on, snap_left_off, snap_right_on, snap_right_off,
	       snap_top_on, snap_top_off, snap_bottom_on, snap_bottom_off);
#ifdef CONFIG_TOUCHSCREEN_COMPATIBLE_REPORT
	input_set_abs_params(ts->input_dev, ABS_X, -inactive_area_left, max_x + inactive_area_right, fuzz_x, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, -inactive_area_top, max_y + inactive_area_bottom, fuzz_y, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, fuzz_p, 0);
	input_set_abs_params(ts->input_dev, ABS_TOOL_WIDTH, 0, 15, fuzz_w, 0);
	input_set_abs_params(ts->input_dev, ABS_HAT0X, -inactive_area_left, max_x + inactive_area_right, fuzz_x, 0);
	input_set_abs_params(ts->input_dev, ABS_HAT0Y, -inactive_area_top, max_y + inactive_area_bottom, fuzz_y, 0);
#endif
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, -inactive_area_left, max_x + inactive_area_right, fuzz_x, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, -inactive_area_top, max_y + inactive_area_bottom, fuzz_y, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, fuzz_p, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 15, fuzz_w, 0);
#ifdef CONFIG_TOUCHSCREEN_CONCATENATE_REPORT
	input_set_abs_params(ts->input_dev, ABS_MT_AMPLITUDE, 0, ((255 << 16) | 255), 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION, 0, ((1 << 31) | (max_x << 16) | max_y), 0, 0);
#endif
	/* ts->input_dev->name = ts->keypad_info->name; */
	ret = input_register_device(ts->input_dev);
	if (ret) {
		printk(KERN_ERR "synaptics_ts_probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}

#ifdef ENABLE_IME_IMPROVEMENT
	touch_screen_margin_left = -inactive_area_left;
	touch_screen_margin_top = -inactive_area_top;
	touch_screen_margin_right = max_x + inactive_area_right;
	touch_screen_margin_bottom = max_y + inactive_area_bottom;

	ret = device_create_file(&ts->input_dev->dev, &dev_attr_ime_threshold);
	if (ret) {
		printk(KERN_ERR "ENABLE_IME_IMPROVEMENT: "
				"Error to create ime_threshold\n");
		goto err_input_register_device_failed;
	}
	ret = device_create_file(&ts->input_dev->dev, &dev_attr_ime_work_area);
	if (ret) {
		printk(KERN_ERR "ENABLE_IME_IMPROVEMENT: "
				"Error to create ime_work_area\n");
		device_remove_file(&ts->input_dev->dev,
				   &dev_attr_ime_threshold);
		goto err_input_register_device_failed;
	}

	if (display_width)
		pixel_x = (touch_screen_margin_right - touch_screen_margin_left)
			  / display_width;
	if (display_height)
		pixel_y = (touch_screen_margin_bottom - touch_screen_margin_top)
			  / display_height;

	ime_threshold = 0;
	for (i = 0; i < MULTI_FINGER_NUMBER; i++) {
		report_x[i] = 0;
		report_y[i] = 0;
	}
	touch_hit = 0;
	printk(KERN_INFO "ENABLE_IME_IMPROVEMENT: x pixel size %d, "
			"y pixel size %d, margin %d, %d, %d, %d.\n",
			pixel_x, pixel_y,
			touch_screen_margin_left, touch_screen_margin_right,
			touch_screen_margin_top, touch_screen_margin_bottom);
#endif

	if (client->irq) {
		ret = request_irq(client->irq, synaptics_ts_irq_handler, irqflags, client->name, ts);
		if (ret == 0) {
			ret = i2c_smbus_write_byte_data(ts->client, 0xf1, 0x01); /* enable abs int */
			if (ret)
				free_irq(client->irq, ts);
		}
		if (ret == 0)
			ts->use_irq = 1;
		else
			dev_err(&client->dev, "request_irq failed\n");
	}
	if (!ts->use_irq) {
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = synaptics_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING + 1;
	ts->early_suspend.suspend = synaptics_ts_early_suspend;
	ts->early_suspend.resume = synaptics_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	printk(KERN_INFO "synaptics_ts_probe: Start touchscreen %s in %s mode\n", ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");

	touch_sysfs_init();

	return 0;

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
err_detect_failed:
err_power_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}

static int synaptics_ts_remove(struct i2c_client *client)
{
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);
	unregister_early_suspend(&ts->early_suspend);
	if (ts->use_irq)
		free_irq(client->irq, ts);
	else
		hrtimer_cancel(&ts->timer);
	input_unregister_device(ts->input_dev);

#ifdef ENABLE_IME_IMPROVEMENT
	device_remove_file(&ts->input_dev->dev, &dev_attr_ime_threshold);
	device_remove_file(&ts->input_dev->dev, &dev_attr_ime_work_area);
#endif

	kfree(ts);
	return 0;
}

static int synaptics_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);
	ret = cancel_work_sync(&ts->work);
	if (ret && ts->use_irq) /* if work was pending disable-count is now 2 */
		enable_irq(client->irq);
	ret = i2c_smbus_write_byte_data(ts->client, 0xf1, 0); /* disable interrupt */
	if (ret < 0)
		printk(KERN_ERR "synaptics_ts_suspend: i2c_smbus_write_byte_data failed\n");

	ret = i2c_smbus_write_byte_data(client, 0xf0, 0x86); /* deep sleep */
	if (ret < 0)
		printk(KERN_ERR "synaptics_ts_suspend: i2c_smbus_write_byte_data failed\n");
	if (ts->power) {
		ret = ts->power(0);
		if (ret < 0)
			printk(KERN_ERR "synaptics_ts_resume power off failed\n");
	}
	return 0;
}

static int synaptics_ts_resume(struct i2c_client *client)
{
	int ret;
	struct synaptics_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(1);
		if (ret < 0)
			printk(KERN_ERR "synaptics_ts_resume power on failed\n");
	}

	ts->first_pressed = 0;

	synaptics_init_panel(ts);

	if (ts->use_irq)
		enable_irq(client->irq);

	if (!ts->use_irq)
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	else
		i2c_smbus_write_byte_data(ts->client, 0xf1, 0x01); /* enable abs int */

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void synaptics_ts_early_suspend(struct early_suspend *h)
{
	struct synaptics_ts_data *ts;
	ts = container_of(h, struct synaptics_ts_data, early_suspend);
	synaptics_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void synaptics_ts_late_resume(struct early_suspend *h)
{
	struct synaptics_ts_data *ts;
	ts = container_of(h, struct synaptics_ts_data, early_suspend);
	synaptics_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id synaptics_ts_id[] = {
	{ SYNAPTICS_I2C_RMI_NAME, 0 },
	{ }
};

static struct i2c_driver synaptics_ts_driver = {
	.probe		= synaptics_ts_probe,
	.remove		= synaptics_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= synaptics_ts_suspend,
	.resume		= synaptics_ts_resume,
#endif
	.id_table	= synaptics_ts_id,
	.driver = {
		.name	= SYNAPTICS_I2C_RMI_NAME,
	},
};

static int __devinit synaptics_ts_init(void)
{
	synaptics_wq = create_singlethread_workqueue("synaptics_wq");
	if (!synaptics_wq)
		return -ENOMEM;
	return i2c_add_driver(&synaptics_ts_driver);
}

static void __exit synaptics_ts_exit(void)
{
	i2c_del_driver(&synaptics_ts_driver);
	if (synaptics_wq)
		destroy_workqueue(synaptics_wq);
}

module_init(synaptics_ts_init);
module_exit(synaptics_ts_exit);

MODULE_DESCRIPTION("Synaptics Touchscreen Driver");
MODULE_LICENSE("GPL");
