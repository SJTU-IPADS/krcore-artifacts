#ifndef _COMPAT_LINUX_KMOD_H
#define _COMPAT_LINUX_KMOD_H 1

#include_next <linux/kmod.h>

#ifdef CONFIG_MODULES

/* When building drivers on Containers, we cannot use request_module functions,
 * as the called modprobe command will run in the host (not the Container)
 * resulting in loading the installed inbox drivers on the host that will either
 * cause errors about symbols or even kernel panic. */
#ifdef CONFIG_MLNX_BLOCK_REQUEST_MODULE
#ifdef request_module
#undef request_module
#endif

#ifdef request_module_nowait
#undef request_module_nowait
#endif

static inline int compat_block_request_module(const char *name, ...) {
	return 0;
}
#define request_module(mod...) compat_block_request_module(mod)
#define request_module_nowait(mod...) compat_block_request_module(mod)

#endif /* CONFIG_MLNX_BLOCK_REQUEST_MODULE */

#endif /* CONFIG_MODULES */

#endif	/* _COMPAT_LINUX_KMOD_H */
