/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HID_TMFF2
#define _HID_TMFF2

#include <linux/fixp-arith.h>
#include <linux/ktime.h>
#include <linux/input.h>

#define USB_VENDOR_ID_THRUSTMASTER 0x044f

/* the wheel seems to only be capable of processing a certain number of
 * interrupts per second, and if this value is too low the kernel urb buffer(or
 * some buffer at least) fills up. Optimally I would figure out some way to
 * space out the interrupts so that they all leave at regular intervals, but
 * for now this is good enough, go slow enough that everything works.
 */
#define DEFAULT_TIMER_PERIOD 8

#define FF_EFFECT_QUEUE_UPLOAD 0
#define FF_EFFECT_QUEUE_START  1
#define FF_EFFECT_QUEUE_STOP   2
#define FF_EFFECT_QUEUE_UPDATE 3
#define FF_EFFECT_PLAYING      4

#undef fixp_sin16
#define fixp_sin16(v) (((v % 360) > 180) ?\
		-(fixp_sin32((v % 360) - 180) >> 16)\
		: fixp_sin32(v) >> 16)

#define JIFFIES2MS(jiffies) ((jiffies) * 1000 / HZ)

struct tmff2_effect_state {
	struct ff_effect effect;
	struct ff_effect old;

	unsigned long flags;
	unsigned long count;
	unsigned long start_time;
};

struct tmff2_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;

	/* pointer to array */
	struct tmff2_effect_state *states;

	struct delayed_work work;

	spinlock_t lock;

	/* fields relevant to each actual device (T300, T150...) */
	void *data;
	unsigned long max_effects;
	signed short supported_effects[FF_CNT];

	/* obligatory callbacks */
	int (*play_effect)(void *data, struct tmff2_effect_state *state);
	int (*upload_effect)(void *data, struct tmff2_effect_state *state);
	int (*update_effect)(void *data, struct tmff2_effect_state *state);
	int (*stop_effect)(void *data, struct tmff2_effect_state *state);

	int (*wheel_init)(struct tmff2_device_entry *tmff2);
	int (*wheel_destroy)(void *data);

	/* optional callbacks */
	int (*open)(void *data);
	int (*close)(void *data);
	int (*set_gain)(void *data, uint16_t gain);
	int (*set_range)(void *data, uint16_t range);
	int (*switch_mode)(void *data, uint16_t mode);
	int (*set_autocenter)(void *data, uint16_t autocenter);
	__u8 *(*wheel_fixup)(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize);

	/* void pointers are dangerous, I know, but in this case likely the best option... */
};

#endif /* _HID_TMFF2 */
