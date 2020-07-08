#include "hid-tminit.h"

static void tminit_callback(struct urb *urb){
    ctx.status = urb->status;
    hid_info(urb->dev, "urb status %d received\n", urb->status);
}

int tminit(struct hid_device *hdev){
    struct urb *urb;
    u8 *setup_packet, *transfer_buffer;
    struct device *dev = &hdev->dev;
    struct usb_interface *usbif = to_usb_interface(dev->parent);
    struct usb_device *usbdev = interface_to_usbdev(usbif);
    int ret;
    
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
           0,
           tminit_callback,
           hdev);

    /* we sort of have to go on faith that the message is sent, because the
     * wheel usually completely dies as soon as it receives the message.
     */
    ret = usb_start_wait_urb(urb, 5, NULL);

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
