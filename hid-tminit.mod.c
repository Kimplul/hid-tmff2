#include <linux/module.h>
#include <linux/build-salt.h>
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
	{ 0x7ca88ad8, "module_layout" },
	{ 0x817489f6, "hid_unregister_driver" },
	{ 0xb7aa9422, "__hid_register_driver" },
	{ 0x414fc2a1, "hid_hw_start" },
	{ 0xcd9e657d, "hid_open_report" },
	{ 0x316d1bda, "_dev_err" },
	{ 0xdecd0b29, "__stack_chk_fail" },
	{ 0x37a0cba, "kfree" },
	{ 0x82fea2ff, "usb_alloc_urb" },
	{ 0xc30561e7, "usb_interrupt_msg" },
	{ 0x69acdf38, "memcpy" },
	{ 0xc27f683d, "kmem_cache_alloc_trace" },
	{ 0x46a9479b, "kmalloc_caches" },
	{ 0x70bce9a, "__dynamic_dev_dbg" },
	{ 0xfeba7354, "current_task" },
	{ 0x648538a5, "usb_kill_urb" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0xadd72fe9, "usb_free_urb" },
	{ 0x4a3ad70e, "wait_for_completion_timeout" },
	{ 0xedb8a9, "usb_submit_urb" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0x4a0c79b0, "hid_hw_stop" },
	{ 0x59e59766, "_dev_info" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "hid,usbcore");

MODULE_ALIAS("hid:b0003g*v0000044Fp0000B65D");
