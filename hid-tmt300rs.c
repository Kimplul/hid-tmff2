#include "hid-tmt300rs.h"

static struct t300rs_device_entry *t300rs_get_device(struct hid_device *hdev){
    struct t300rs_data *drv_data;
    struct t300rs_device_entry *t300rs;

    spin_lock_irqsave(&lock, lock_flags);
    drv_data = hid_get_drvdata(hdev);
    if(!drv_data){
        hid_err(hdev, "private data not found\n");
        return NULL;
    }

    t300rs = drv_data->device_props;
    if(!t300rs){
        hid_err(hdev, "device properties not found\n");
        return NULL;
    }
    spin_unlock_irqrestore(&lock, lock_flags);

    return t300rs;
}

static int t300rs_send_int(struct input_dev *dev, u8 *send_buffer, int *trans){
    struct hid_device *hdev = input_get_drvdata(dev);
    struct t300rs_device_entry *t300rs;
    struct usb_device *usbdev;
    struct usb_interface *usbif;
    struct usb_host_endpoint *ep;
    int b_ep;

    t300rs = t300rs_get_device(hdev);

    usbdev = t300rs->usbdev;
    usbif = t300rs->usbif;
    ep = &usbif->cur_altsetting->endpoint[1];
    b_ep = ep->desc.bEndpointAddress;

    return usb_interrupt_msg(usbdev,
            usb_sndintpipe(usbdev, b_ep),
            send_buffer,
            T300RS_BUFFER_LENGTH,
            trans,
            USB_CTRL_SET_TIMEOUT);
}

static int t300rs_upload(struct input_dev *dev, struct ff_effect *effect, struct ff_effect *old){
    /* temp */
    return 0;
}

static int t300rs_play(struct input_dev *dev, int effect_id, int value){
    /* temp */
    return 0;
}

static ssize_t t300rs_range_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count){
    struct hid_device *hdev = to_hid_device(dev);
    struct t300rs_device_entry *t300rs;
    u8 *send_buffer = kzalloc(T300RS_BUFFER_LENGTH, GFP_ATOMIC);
    u16 range = simple_strtoul(buf, NULL, 10);
    int ret, trans;

    t300rs = t300rs_get_device(hdev);

    if(range < 0x097b){
        range = 0x097b;
    }
    
    send_buffer[0] = 0x60;
    send_buffer[1] = 0x08;
    send_buffer[2] = 0x11;
    send_buffer[3] = range & 0xff;
    send_buffer[4] = range >> 8;

    ret = t300rs_send_int(t300rs->input_dev, send_buffer, &trans);
    if(ret){
        hid_err(hdev, "failed sending interrupts\n");
        return -1;
    }
    
    t300rs->range = range;
    kfree(send_buffer);
    return count;
}

static ssize_t t300rs_range_show(struct device *dev, struct device_attribute *attr,
        char *buf){
    struct hid_device *hdev = to_hid_device(dev);
    struct t300rs_device_entry *t300rs;
    
    t300rs = t300rs_get_device(hdev);

    return t300rs->range;
}

static DEVICE_ATTR(range, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH, t300rs_range_show, t300rs_range_store);

static void t300rs_set_gain(struct input_dev *dev, u16 gain){
    struct hid_device *hdev = input_get_drvdata(dev);
    u8 *send_buffer = kzalloc(T300RS_BUFFER_LENGTH, GFP_ATOMIC);
    int ret, trans;

    send_buffer[0] = 0x60;
    send_buffer[1] = 0x02;
    send_buffer[2] = SCALE_VALUE_U16(gain, sizeof(char));

    ret = t300rs_send_int(dev, send_buffer, &trans);
    if(ret){
        hid_err(hdev, "failed sending interrupts\n");
    }
}

static void t300rs_destroy(struct ff_device *ff){
    /* maybe not temp? */
    return;
}


static int t300rs_open(struct input_dev *dev){
    struct hid_device *hdev = input_get_drvdata(dev);
    u8 *send_buffer = kzalloc(T300RS_BUFFER_LENGTH, GFP_ATOMIC);
    int ret, trans;

    send_buffer[0] = 0x60;
    send_buffer[1] = 0x01;
    send_buffer[2] = 0x04;

    ret = t300rs_send_int(dev, send_buffer, &trans); 
    if(ret){
        hid_err(hdev, "failed sending interrupts\n");
        goto err;
    }
    memset(send_buffer, 0, T300RS_BUFFER_LENGTH);

    send_buffer[0] = 0x60;
    send_buffer[1] = 0x12;
    send_buffer[2] = 0xbf;
    send_buffer[3] = 0x04;
    send_buffer[6] = 0x03;
    send_buffer[7] = 0xb7;
    send_buffer[8] = 0x1e;

    ret = t300rs_send_int(dev, send_buffer, &trans); 
    if(ret){
        hid_err(hdev, "failed sending interrupts\n");
        goto err;
    }
    memset(send_buffer, 0, T300RS_BUFFER_LENGTH);

    send_buffer[0] = 0x60;
    send_buffer[1] = 0x01;
    send_buffer[2] = 0x05;

    ret = t300rs_send_int(dev, send_buffer, &trans); 
    if(ret){
        hid_err(hdev, "failed sending interrupts\n");
        goto err;
    }

err:
    kfree(send_buffer);
    return ret;
}

static void t300rs_close(struct input_dev *dev){
    int ret, trans;
    struct hid_device *hdev = input_get_drvdata(dev);
    u8 *send_buffer = kzalloc(T300RS_BUFFER_LENGTH, GFP_ATOMIC);

    send_buffer[0] = 0x60;
    send_buffer[1] = 0x01;

    ret = t300rs_send_int(dev, send_buffer, &trans);
    if(ret){
        hid_err(hdev, "failed sending interrupts\n");
        goto err;
    }
err:
    kfree(send_buffer);
    return;
}

int t300rs_init(struct hid_device *hdev, const signed short *ff_bits){
    struct t300rs_device_entry *t300rs;
    struct t300rs_data *drv_data;
    struct list_head *report_list;
    struct hid_input *hidinput = list_entry(hdev->inputs.next,
            struct hid_input, list);
    struct input_dev *input_dev = hidinput->input;
    struct device *dev = &hdev->dev;
    struct usb_interface *usbif = to_usb_interface(dev->parent);
    struct usb_device *usbdev = interface_to_usbdev(usbif);
    struct hid_report *report;
    struct ff_device *ff;
    int i, ret;

    drv_data = hid_get_drvdata(hdev);
    if(!drv_data){
        hid_err(hdev, "private driver data not allocated\n");
        ret = -ENOMEM;
        goto err;
    }

    t300rs = kzalloc(sizeof(struct t300rs_device_entry), GFP_ATOMIC);
    if(!t300rs){
        return -ENOMEM;
    }
    
    t300rs->input_dev = input_dev;
    t300rs->hdev = hdev;
    t300rs->usbdev = usbdev;
    t300rs->usbif = usbif;
    spin_lock_init(&t300rs->lock);

    drv_data->device_props = t300rs;


    report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;
    list_for_each_entry(report, report_list, list){
        int fieldnum;

        for(fieldnum = 0; fieldnum < report->maxfield; ++fieldnum){
            struct hid_field *field = report->field[fieldnum];

            if(field->maxusage <= 0){
                continue;
            }

            switch(field->usage[0].hid){
                case 0xff00000a:
                    if(field->report_count < 2){
                        hid_warn(hdev, "ignoring FF field with report_count < 2\n");
                        continue;
                    }

                    if(field->logical_maximum == field->logical_minimum){
                        hid_warn(hdev, "ignoring FF field with l_max == l_min");
                        continue;
                    }

                    if(t300rs->report && t300rs->report != report){
                        hid_warn(hdev, "ignoring FF field in other report\n");
                        continue;
                    }

                    if(t300rs->ff_field && t300rs->ff_field != field){
                        hid_warn(hdev, "ignoring duplicate FF field\n");
                        continue;
                    }

                    t300rs->report = report;
                    t300rs->ff_field = field;

                    for(i = 0; ff_bits[i] >= 0; ++i){
                        set_bit(ff_bits[i], input_dev->ffbit);
                    }

                    break;

                default:
                    hid_warn(hdev, "ignoring unknown output usage\n");
                    continue;
            }
        }
    }

    if(!t300rs->report){
        hid_err(hdev, "can't find FF field in output reports\n");
        ret = -ENODEV;
        goto err;
    }

    ret = input_ff_create(input_dev, T300RS_MAX_EFFECTS);
    if(ret){
        hid_err(hdev, "could not create input_ff\n");
        goto err;
    }

    ff = input_dev->ff;
    ff->upload = t300rs_upload;
    ff->playback = t300rs_play;
    ff->set_gain = t300rs_set_gain;
    ff->destroy = t300rs_destroy;

    input_dev->open = t300rs_open;
    input_dev->close = t300rs_close;

    ret = device_create_file(&hdev->dev, &dev_attr_range);
    if(ret){
        hid_warn(hdev, "unable to create sysfs interface for range\n");
    }

    hid_info(hdev, "force feedback for T300RS\n");
    return 0;
err:
    kfree(t300rs);
    hid_err(hdev, "failed creating force feedback device\n");
    return ret;

}

static int t300rs_probe(struct hid_device *hdev, const struct hid_device_id *id){
    int ret;
    struct t300rs_data *drv_data;

    spin_lock_init(&lock);
    spin_lock_irqsave(&lock, lock_flags);

    drv_data = kzalloc(sizeof(struct t300rs_data), GFP_ATOMIC);
    if(!drv_data){
        hid_err(hdev, "out of memory\n");
        ret = -ENOMEM;
        goto err;
    }

    drv_data->quirks = id->driver_data;
    hid_set_drvdata(hdev, (void*)drv_data);

    ret = hid_parse(hdev);
    if(ret){
        hid_err(hdev, "parse failed\n");
        goto err;
    }

    ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
    if(ret){
        hid_err(hdev, "hw start failed\n");
        goto err;
    }

    ret = t300rs_init(hdev, (void*)id->driver_data);
    if(ret){
        hid_err(hdev, "t300rs_init failed\n");
        goto err;
    }

    spin_unlock_irqrestore(&lock, lock_flags);
    return 0;
err:
    kfree(drv_data);
    spin_unlock_irqrestore(&lock, lock_flags);
    return ret;
}

static void t300rs_remove(struct hid_device *hdev){
    struct t300rs_device_entry *t300rs;
    struct t300rs_data *drv_data;

    device_remove_file(&hdev->dev, &dev_attr_range);

    drv_data = hid_get_drvdata(hdev);
    t300rs = t300rs_get_device(hdev);

    kfree(drv_data);
    kfree(t300rs);
    hid_hw_stop(hdev);
    return;
}

static __u8 *t300rs_report_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize){
    rdesc = t300rs_rdesc_fixed;
    *rsize = sizeof(t300rs_rdesc_fixed);
    return rdesc;
}

static const struct hid_device_id t300rs_devices[] = {
    {HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb66e),
        .driver_data = (unsigned long)t300rs_ff_effects},
    {}
};
MODULE_DEVICE_TABLE(hid, t300rs_devices);

static struct hid_driver t300rs_driver = {
    .name = "t300rs",
    .id_table = t300rs_devices,
    .probe = t300rs_probe,
    .remove = t300rs_remove,
    .report_fixup = t300rs_report_fixup,
};
module_hid_driver(t300rs_driver);

MODULE_LICENSE("GPL");
