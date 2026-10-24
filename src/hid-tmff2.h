/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HID_TMFF2_H
#define __HID_TMFF2_H

#include <linux/fixp-arith.h>
#include <linux/math.h>
#include <linux/ktime.h>
#include <linux/input.h>

#define USB_VENDOR_ID_THRUSTMASTER 0x044f

/* the wheel seems to only be capable of processing a certain number of
 * interrupts per second, and if this value is too low the kernel urb buffer (or
 * some buffer at least) fills up. Optimally I would figure out some way to
 * space out the interrupts so that they all leave at regular intervals, but
 * for now this is good enough, go slow enough that everything works.
 */
#define DEFAULT_TIMER_PERIOD	8

#define FF_EFFECT_QUEUE_UPLOAD	0
#define FF_EFFECT_QUEUE_START	1
#define FF_EFFECT_QUEUE_STOP	2
#define FF_EFFECT_QUEUE_UPDATE	3
#define FF_EFFECT_PLAYING	4

#define PARAM_SPRING_LEVEL	(1 << 0)
#define PARAM_DAMPER_LEVEL	(1 << 1)
#define PARAM_FRICTION_LEVEL	(1 << 2)
#define PARAM_RANGE		(1 << 3)
#define PARAM_ALT_MODE		(1 << 4)
#define PARAM_GAIN		(1 << 5)

struct tmff2_effect_state {
	struct ff_effect effect;
	struct ff_effect old;

	unsigned long flags;
	unsigned long count;
	/* in jiffies */
	unsigned long start_time;
};

struct tm_wheel_desc;

struct tmff2_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;

	/* pointer to array */
	struct tmff2_effect_state *states;

	struct delayed_work work;

	spinlock_t lock;

	int allow_scheduling;
	/* the wheel is initialized, protected by input_dev->mutex */
	int ready;

	/* open/close of the input device as set up by hid-input */
	int (*hid_open)(struct input_dev *dev);
	void (*hid_close)(struct input_dev *dev);

	/* per-device settings, changeable through sysfs */
	int spring_level;
	int damper_level;
	int friction_level;
	u16 range;
	u16 gain;
	/* gain set by the application through the ff core, scaled by gain */
	u16 ff_gain;

	/* fields relevant to each actual device (T300, T248...) */
	void *data;
	/* only set for wheels using the shared tmcommon backend */
	const struct tm_wheel_desc *wheel_desc;
	unsigned long params;
	unsigned long max_effects;
	signed short supported_effects[FF_CNT];

	/* obligatory callbacks */
	int (*play_effect)(void *data, const struct tmff2_effect_state *state);
	int (*upload_effect)(void *data, const struct tmff2_effect_state *state);
	int (*update_effect)(void *data, const struct tmff2_effect_state *state);
	int (*stop_effect)(void *data, const struct tmff2_effect_state *state);

	int (*wheel_init)(struct tmff2_device_entry *tmff2, int open_mode);
	int (*wheel_destroy)(void *data);

	/* optional callbacks */
	int (*open)(void *data, int);
	int (*close)(void *data, int);
	int (*set_gain)(void *data, uint16_t gain);
	int (*set_range)(void *data, uint16_t range);
	/* switch_mode is required to not do anything if we're alredy in the
	 * specified mode */
	int (*switch_mode)(void *data, uint16_t mode);
	ssize_t (*alt_mode_show)(void *data, char *buf);
	ssize_t (*alt_mode_store)(void *data, const char *buf, size_t count);
	int (*set_autocenter)(void *data, uint16_t autocenter);
	const __u8 *(*wheel_fixup)(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize);

	/* void pointers are dangerous, I know, but in this case likely the
	 * best option... */
};

/* external */
int t300rs_populate_api(struct tmff2_device_entry *tmff2);
int t248_populate_api(struct tmff2_device_entry *tmff2);
int tx_populate_api(struct tmff2_device_entry *tmff2);
int tsxw_populate_api(struct tmff2_device_entry *tmff2);
int tspc_populate_api(struct tmff2_device_entry *tmff2);

#define TMT300RS_PS3_NORM_ID	0xb66e
#define TMT300RS_PS3_ADV_ID	0xb66f
#define TMT300RS_PS4_NORM_ID	0xb66d

#define TMT248_PC_ID		0xb696

#define TX_ACTIVE               0xb669

#define TSXW_ACTIVE		0xb692

#define TMTS_PC_RACER_ID	0xb689

/* APIs to different wheel families */
/* T248 and TX at least uses the T300RS api, not sure if there are other wheels
 * but that's why these functions are given global linkage */

struct t300rs_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;
	struct hid_report *report;
	struct hid_field *ff_field;
	struct usb_device *usbdev;
	struct tmff2_device_entry *tmff2;

	/* only set for wheels using the shared tmcommon backend */
	const struct tm_wheel_desc *desc;

	int mode;
	int attachment;
	u8 buffer_length;

	/* only used for effect packets, which are exclusively built from the
	 * tmff2 work handler. Other commands use their own buffers. */
	u8 *send_buffer;

	/* serializes access to ff_field and the output report */
	spinlock_t send_lock;
};

int t300rs_play_effect(void *, const struct tmff2_effect_state *);
int t300rs_upload_effect(void *, const struct tmff2_effect_state *);
int t300rs_update_effect(void *, const struct tmff2_effect_state *);
int t300rs_stop_effect(void *, const struct tmff2_effect_state *);

int t300rs_open(void *, int);
int t300rs_close(void *, int);
int t300rs_set_gain(void *, uint16_t);
int t300rs_set_range(void *, uint16_t);
int t300rs_set_autocenter(void *, uint16_t);

int t300rs_send_buf(struct t300rs_device_entry *t300rs, const u8 *send_buffer, size_t len);
int t300rs_send_int(struct t300rs_device_entry *t300rs);

/* shared initialization of a t300rs_device_entry, validates the output report
 * layout before anything is written into it */
int t300rs_init_common(struct tmff2_device_entry *tmff2,
		struct t300rs_device_entry *t300rs, u8 buffer_length);

/* description of a wheel handled by the shared tmcommon backend */
struct tm_wheel_desc {
	const char *name;
	const __u8 *rdesc;
	unsigned int rsize;
	u16 range_min;
	u16 range_max;
	unsigned long params;
	/* optional */
	ssize_t (*alt_mode_store)(void *data, const char *buf, size_t count);
};

int tm_common_populate_api(struct tmff2_device_entry *tmff2,
		const struct tm_wheel_desc *desc);

#endif
