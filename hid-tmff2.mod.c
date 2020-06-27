#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(.gnu.linkonce.this_module) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section(__versions) = {
	{ 0xb90cb2c6, "module_layout" },
	{ 0xb314bd85, "hid_unregister_driver" },
	{ 0x399e83c3, "__hid_register_driver" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x95ba88c, "input_ff_create_memless" },
	{ 0x52af4d4b, "_dev_info" },
	{ 0x60da7bc0, "hid_hw_start" },
	{ 0x484d36ee, "usb_interrupt_msg" },
	{ 0xdf872899, "_dev_err" },
	{ 0x69acdf38, "memcpy" },
	{ 0x443d25f8, "hid_open_report" },
	{ 0xe00d2f98, "__dynamic_dev_dbg" },
	{ 0x2d9e7e22, "current_task" },
	{ 0x162615, "usb_kill_urb" },
	{ 0x4d1ff60a, "wait_for_completion_timeout" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xc5850110, "printk" },
	{ 0x37a0cba, "kfree" },
	{ 0xf51ed08d, "usb_submit_urb" },
	{ 0xec4e6a94, "kmem_cache_alloc_trace" },
	{ 0xc4a14306, "kmalloc_caches" },
	{ 0xe0d57b51, "usb_alloc_urb" },
	{ 0xb43f9365, "ktime_get" },
	{ 0xd0d6e733, "usb_free_urb" },
	{ 0xf64c8101, "_dev_warn" },
	{ 0x3d6a80fd, "hid_hw_stop" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "hid,ff-memless,usbcore");

MODULE_ALIAS("hid:b0003g*v0000044Fp0000B65D");
MODULE_ALIAS("hid:b0003g*v0000044Fp0000B66E");
