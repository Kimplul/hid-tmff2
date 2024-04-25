#include <linux/module.h>
#include <linux/usb.h>

// Define the devices and their corresponding control writes
static const struct usb_device_id usb_table[] = {
    { USB_DEVICE(0x044f, 0xb691) },
    // { USB_DEVICE(0x044f, 0xb692) },
    { USB_DEVICE(0x044f, 0xb664) },
    { USB_DEVICE(0x044f, 0xb65d) },
    // { USB_DEVICE(0x044f, 0xb669) },
    { } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, usb_table);

static int usb_control_write_probe(struct usb_interface *interface,
                                   const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(interface);
    int ret = 0;

    // Check if the device is one of the ones we're interested in
    for (int i = 0; i < sizeof(usb_table) / sizeof(usb_table[0]); i++) {
        if (usb_table[i].idVendor == udev->descriptor.idVendor &&
            usb_table[i].idProduct == udev->descriptor.idProduct) {

            // Send control writes based on the device
            switch (udev->descriptor.idProduct) {
            case 0xb691:
                // Send control writes for Thrustmaster TS XW (initial mode)
                printk(KERN_INFO "usb_tminit: Initializing Thrustmaster TS XW...");                
                ret = usb_control_msg_send(udev, 0,
                                     83, 0x41, 0x000a, 0x0000,
                                     NULL, 0, 10000, GFP_ATOMIC);


                break;
            case 0xb664:
                // Send control writes for Thrustmaster TX (initial mode)
                printk(KERN_INFO "usb_tminit: Initializing Thrustmaster TX (step 1/2)...");                
                ret = usb_control_msg_send(udev, 0,
                                     83, 0x41, 0x0001, 0x0000,
                                     NULL, 0, 10000, GFP_ATOMIC);

                break;
            case 0xb65d:
                // Send control writes for Thrustmaster TX (intermediate mode)
                printk(KERN_INFO "usb_tminit: Initializing Thrustmaster TX (step 2/2)...");                
                
                ret = usb_control_msg_send(udev, 0,
                                     83, 0x41, 0x0004, 0x0000,
                                     NULL, 0, 10000, GFP_ATOMIC);

                break;
            default:
                printk(KERN_ERR "usb_tminit: Unknown device product ID 0x%x\n", udev->descriptor.idProduct);
                break;
            }

            break;
        }
    }

    return 0;
}

static void usb_control_write_disconnect(struct usb_interface *interface)
{
    // Nothing to do here
}

static struct usb_driver usb_control_write_driver = {
   .name = "usb_tminit",
   .probe = usb_control_write_probe,
   .disconnect = usb_control_write_disconnect,
   .id_table = usb_table,
};

static int __init usb_control_write_init(void)
{
    return usb_register(&usb_control_write_driver);
}

static void __exit usb_control_write_exit(void)
{
    usb_deregister(&usb_control_write_driver);
}

module_init(usb_control_write_init);
module_exit(usb_control_write_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yassine Imounachen");
MODULE_DESCRIPTION("Kernel module to detect and initialize Thrustmaster TX and TS-XW. Based on tmdrv by her0 (https://gitlab.com/her0/tmdrv).");
