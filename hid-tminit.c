#include "hid-tminit.h"

static void tminit_callback(struct urb *urb){
    hid_info(urb->dev, "urb status %d received\n", urb->status);
}

/* for some godawful reason these interrupts are absolutely necessary, otherwise
 * the whole kernel crashes. I have no idea why.
 * */
static void tminit_interrupts(struct hid_device *hdev){
    int ret, trans, i, b_ep;
    u8 *send_buf = kmalloc(256, GFP_KERNEL);

    struct usb_host_endpoint *ep;
    struct device *dev = &hdev->dev;
    struct usb_interface *usbif = to_usb_interface(dev->parent);
    struct usb_device *usbdev = interface_to_usbdev(usbif);

    ep = &usbif->cur_altsetting->endpoint[1];
    b_ep = ep->desc.bEndpointAddress;

    for(i = 0; i < ARRAY_SIZE(setup_arr); ++i){
        memcpy(send_buf, setup_arr[i], setup_arr_sizes[i]);

        ret = usb_interrupt_msg(usbdev,
                usb_sndintpipe(usbdev, b_ep),
                send_buf,
                setup_arr_sizes[i],
                &trans,
                USB_CTRL_SET_TIMEOUT);

        if(ret){
            hid_err(hdev, "setup data couldn't be sent\n");
            return;
        }
        
    }

    kzfree(send_buf);
}

/*
void tminit_controls(struct hid_device *hdev){
    int i = 0, ret;
    struct usb_host_endpoint *ep;
    struct usb_host_endpoint *ip;
    struct device *dev = &hdev->dev;
    struct usb_interface *usbif = to_usb_interface(dev->parent);
    struct usb_device *usbdev = interface_to_usbdev(usbif);

    u8 *transfer = kzalloc(64, GFP_ATOMIC);

    ret = usb_control_msg(usbdev,
            usb_rcvctrlpipe(usbdev, 0),
            86,
            0xc1,
            0,
            0,
            transfer,
            8,
            USB_CTRL_SET_TIMEOUT);

    if(ret < 0){
        hid_err(hdev, "failed with the ctrl: %i", ret);
    }

    ret = usb_control_msg(usbdev,
            usb_rcvctrlpipe(usbdev, 0),
            73,
            0xc1,
            0,
            0,
            transfer,
            16,
            USB_CTRL_SET_TIMEOUT);

    if(ret < 0){
        hid_err(hdev, "failed with the ctrl: %i", ret);
    }
    ret = usb_control_msg(usbdev,
            usb_rcvctrlpipe(usbdev, 0),
            66,
            0xc1,
            0,
            0,
            transfer,
            8,
            USB_CTRL_SET_TIMEOUT);

    if(ret < 0){
        hid_err(hdev, "failed with the ctrl: %i", ret);
    }
    ret = usb_control_msg(usbdev,
            usb_rcvctrlpipe(usbdev, 0),
            78,
            0xc1,
            0,
            0,
            transfer,
            8,
            USB_CTRL_SET_TIMEOUT);

    if(ret < 0){
        hid_err(hdev, "failed with the ctrl: %i", ret);
    }

    ret = usb_control_msg(usbdev,
            usb_rcvctrlpipe(usbdev, 0),
            86,
            0xc1,
            0,
            0,
            transfer,
            8,
            USB_CTRL_SET_TIMEOUT);

    if(ret < 0){
        hid_err(hdev, "failed with the ctrl: %i", ret);
    }
}
*/

int tminit(struct hid_device *hdev){
    struct urb *urb;
    u8 *setup_packet, *transfer_buffer;
    struct device *dev = &hdev->dev;
    struct usb_interface *usbif = to_usb_interface(dev->parent);
    struct usb_device *usbdev = interface_to_usbdev(usbif);
    int ret;

    //tminit_controls(hdev);

    tminit_interrupts(hdev);

    setup_packet = kmalloc(8, GFP_ATOMIC);
    transfer_buffer = kmalloc(8, GFP_ATOMIC);

    memcpy(setup_packet, hw_rq_out, 8);
    memcpy(transfer_buffer, hw_rq_in, 8);

    urb = usb_alloc_urb(0, GFP_ATOMIC);

    usb_fill_control_urb(urb,
            usbdev,
            usb_sndctrlpipe(usbdev, 0),
            setup_packet,
            transfer_buffer,
            8,
            tminit_callback,
            hdev);

    /* we sort of have to go on faith that the message is sent, because the
     * wheel usually completely dies as soon as it receives the message.
     * No need to even check the return value.
     */
    ret = usb_submit_urb(urb, GFP_ATOMIC);
    kfree(setup_packet);
    kfree(transfer_buffer);
    return ret;
}

static void tminit_remove(struct hid_device *hdev){
    /* we are dead, hopefully without any serious side effects */
    hid_hw_stop(hdev);
}

static int tminit_probe(struct hid_device *hdev, const struct hid_device_id *id){
    int ret;

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

    ret = tminit(hdev);
    if(ret){
        hid_err(hdev, "tminit exited, error might be intended behaviour\n");
        goto err;
    }

err:
    return ret; 
}

static const struct hid_device_id tminit_devices[] = {
    { HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb65d)},
    {}
};

MODULE_DEVICE_TABLE(hid, tminit_devices);

static struct hid_driver tminit_driver = {
    .name = "thrustmaster init",
    .id_table = tminit_devices,
    .probe = tminit_probe,
    .remove = tminit_remove,
};
module_hid_driver(tminit_driver);

MODULE_LICENSE("GPL");
