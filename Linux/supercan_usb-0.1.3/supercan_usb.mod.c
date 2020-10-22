#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
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
__used
__attribute__((section("__versions"))) = {
	{ 0x7ba62dba, "module_layout" },
	{ 0xe80ae2b0, "can_change_mtu" },
	{ 0x40cb8422, "usb_deregister" },
	{ 0xea77e5bc, "usb_register_driver" },
	{ 0xb40778ae, "usb_alloc_coherent" },
	{ 0xf78adb5, "usb_alloc_urb" },
	{ 0x28318305, "snprintf" },
	{ 0x32a8a47c, "register_candev" },
	{ 0x1f2dffde, "alloc_candev_mqs" },
	{ 0x893e89c0, "_dev_warn" },
	{ 0x4ea5d10, "ksize" },
	{ 0xae909697, "_dev_info" },
	{ 0xd2b09ce5, "__kmalloc" },
	{ 0x1ce1ab25, "kmem_cache_alloc_trace" },
	{ 0x3a8870f8, "kmalloc_caches" },
	{ 0x199a9099, "netdev_crit" },
	{ 0xa8c05e67, "netdev_err" },
	{ 0x2e3561b8, "alloc_can_skb" },
	{ 0x69acdf38, "memcpy" },
	{ 0x85c48d63, "can_bus_off" },
	{ 0xb0e602eb, "memmove" },
	{ 0x5976fa56, "can_change_state" },
	{ 0xbbc9f7a5, "alloc_can_err_skb" },
	{ 0xd9a365fe, "can_get_echo_skb" },
	{ 0x167c5967, "print_hex_dump" },
	{ 0xf6ebc03b, "net_ratelimit" },
	{ 0xa36987f8, "netif_rx" },
	{ 0xd791e2d0, "alloc_canfd_skb" },
	{ 0xc2e15764, "netif_tx_wake_queue" },
	{ 0x7317a4ab, "netdev_info" },
	{ 0x4d5be6f1, "open_candev" },
	{ 0x40fabfb1, "usb_kill_urb" },
	{ 0x20c4f8c4, "close_candev" },
	{ 0xf5bcdbef, "__dynamic_netdev_dbg" },
	{ 0x9597d69a, "netif_device_detach" },
	{ 0x4d60ecdc, "kfree_skb" },
	{ 0x4e1569f0, "netdev_warn" },
	{ 0xce010cbc, "can_free_echo_skb" },
	{ 0xcb72dde5, "usb_submit_urb" },
	{ 0x16081ffb, "can_dlc2len" },
	{ 0x8762619a, "can_len2dlc" },
	{ 0x2ea2c95c, "__x86_indirect_thunk_rax" },
	{ 0x3812050a, "_raw_spin_unlock_irqrestore" },
	{ 0x242de20c, "can_put_echo_skb" },
	{ 0x51760917, "_raw_spin_lock_irqsave" },
	{ 0xdb7305a1, "__stack_chk_fail" },
	{ 0xefd3b086, "_dev_err" },
	{ 0x2deece78, "usb_bulk_msg" },
	{ 0x7553aacd, "__dynamic_dev_dbg" },
	{ 0x977aebd0, "unregister_candev" },
	{ 0xb89d5605, "free_candev" },
	{ 0x7c32d0f0, "printk" },
	{ 0x37a0cba, "kfree" },
	{ 0x71a8afa0, "usb_free_urb" },
	{ 0x733d66f, "usb_free_coherent" },
	{ 0xbdfb6dbb, "__fentry__" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=can-dev,usbcore";

MODULE_ALIAS("usb:v1D50p5035d*dc*dsc*dp*ic*isc*ip*in*");
