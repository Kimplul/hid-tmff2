#include "hid-tm.h"

u8 hw_rq_in[] = { 0xc1, 0x49, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00 };
u8 hw_rq_out[] = { 0x41, 0x53, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };

static void tminit_callback(struct urb *urb){ return; }

int tminit(struct hid_device *hdev){
    struct urb *urb;
    struct device *dev = &hdev->dev;
    struct usb_interface *usbif = to_usb_interface(dev->parent);
    struct usb_device *usbdev = interface_to_usbdev(usbif);

    urb = usb_alloc_urb(0, GFP_ATOMIC);

    usb_fill_control_urb(urb,
           usbdev,
           usb_sndctrlpipe(usbdev, 0),
           &hw_rq_in[0],
           &hw_rq_out[0],
           0,
           tminit_callback,
           hdev);

    /* we sort of have to go on faith that the message is sent, because the
     * wheel usually completely dies as soon as it gets the message.
     */
    return usb_start_wait_urb(urb, USB_CTRL_SET_TIMEOUT, NULL);
}

static void tminit_remove(struct hid_device *hdev){
    /* we are dead */
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
        hid_err(hdev, "tminit failed, possibly as it should\n");
        goto err;
    }
err:
    return ret; 
}

static const struct hid_device_id tminit_devices[] = {
    { HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb56d)},
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
