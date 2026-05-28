content = r"""// SPDX-License-Identifier: GPL-2.0
#include "../hid-tmff2.h"
#include "../t300rs/hid-t300rs.h"
#include "../tmt248/hid-tmt248.h"

static int t598_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
        struct t300rs_device_entry *t598 = kzalloc(sizeof(struct t300rs_device_entry),
                                                   GFP_KERNEL);
        struct list_head *report_list;
        int ret;

        if (!t598) {
                ret = -ENOMEM;
                goto t248_err;
        }

        t598->hdev = tmff2->hdev;
        t598->input_dev = tmff2->input_dev;
        t598->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);
        t598->buffer_length = T248_BUFFER_LENGTH;

        t598->send_buffer = kzalloc(t598->buffer_length, GFP_KERNEL);
        if (!t598->send_buffer) {
                ret = -ENOMEM;
                goto send_err;
        }

        report_list = &t598->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
        t598->report = list_entry(report_list->next, struct hid_report, list);
        t598->ff_field = t598->report->field;

        t598->open = t598->input_dev->open;
        t598->close = t598->input_dev->close;

        /* T598 does NOT need the USB interrupt init sequence that T248 uses */

        tmff2->data = t598;
        tmff2->params = t248_params;
        tmff2->max_effects = T248_MAX_EFFECTS;
        memcpy(tmff2->supported_effects, t248_effects, sizeof(t248_effects));

        if (!open_mode)
                t248_send_open(t598);

        hid_info(t598->hdev, "force feedback for T598\n");
        return 0;

send_err:
        kfree(t598);
t248_err:
        hid_err(tmff2->hdev, "failed initializing T598\n");
        return ret;
}

static __u8 *t598_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
                unsigned int *rsize)
{
        rdesc = t248_pc_rdesc_fixed;
        *rsize = sizeof(t248_pc_rdesc_fixed);
        return rdesc;
}

int t598_populate_api(struct tmff2_device_entry *tmff2)
{
        tmff2->play_effect = t300rs_play_effect;
        tmff2->upload_effect = t300rs_upload_effect;
        tmff2->update_effect = t300rs_update_effect;
        tmff2->stop_effect = t300rs_stop_effect;

        tmff2->set_gain = t300rs_set_gain;
        tmff2->set_autocenter = t300rs_set_autocenter;
        tmff2->set_range = t248_set_range;
        tmff2->wheel_fixup = t598_wheel_fixup;

        tmff2->open = t248_open;
        tmff2->close = t248_close;

        tmff2->wheel_init = t598_wheel_init;
        tmff2->wheel_destroy = t248_wheel_destroy;

        return 0;
}
EXPORT_SYMBOL_GPL(t598_populate_api);
"""

with open("/home/asrock/hid-tmff2/src/tmt598/hid-tmt598.c", "w") as f:
    f.write(content)

print("Written successfully.")
