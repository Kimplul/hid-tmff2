#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>

#define USB_VENDOR_ID_THRUSTMASTER 0x044f

struct api_context {
    struct completion done;
    int status;
};

struct api_context ctx;

int usb_start_wait_urb(struct urb *urb, int timeout, int *actual_length)
{
	unsigned long expire;
	int retval;

	init_completion(&ctx.done);
	urb->context = &ctx;
	urb->actual_length = 0;
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(retval))
		goto out;

	expire = timeout ? msecs_to_jiffies(timeout) : MAX_SCHEDULE_TIMEOUT;
	if (!wait_for_completion_timeout(&ctx.done, expire)) {
		usb_kill_urb(urb);
		retval = (ctx.status == -ENOENT ? -ETIMEDOUT : ctx.status);

		dev_dbg(&urb->dev->dev,
				"%s timed out on ep%d%s len=%u/%u\n",
				current->comm,
				usb_endpoint_num(&urb->ep->desc),
				usb_urb_dir_in(urb) ? "in" : "out",
				urb->actual_length,
				urb->transfer_buffer_length);
	} else
		retval = ctx.status;
out:
	if (actual_length)
		*actual_length = urb->actual_length;

	usb_free_urb(urb);
	return retval;
}
