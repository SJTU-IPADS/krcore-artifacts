#ifndef _COMPAT_LINUX_KCONFIG_H
#define _COMPAT_LINUX_KCONFIG_H 1

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
#include_next <linux/kconfig.h>
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0))

/*
 * Getting something that works in C and CPP for an arg that may or may
 * not be defined is tricky.  Here, if we have "#define CONFIG_BOOGER 1"
 * we match on the placeholder define, insert the "0," for arg1 and generate
 * the triplet (0, 1, 0).  Then the last step cherry picks the 2nd arg (a one).
 * When CONFIG_BOOGER is not defined, we generate a (... 1, 0) pair, and when
 * the last step cherry picks the 2nd arg, we get a zero.
 */
#ifdef __ARG_PLACEHOLDER_1
#undef __ARG_PLACEHOLDER_1
#endif
#ifdef ___config_enabled
#undef ___config_enabled
#endif
#ifdef __config_enabled
#undef __config_enabled
#endif
#ifdef config_enabled
#undef config_enabled
#endif
#ifdef _config_enabled
#undef _config_enabled
#endif
#ifdef IS_ENABLED
#undef IS_ENABLED
#endif
#ifdef IS_BUILTIN
#undef IS_BUILTIN
#endif
#ifdef IS_MODULE
#undef IS_MODULE
#endif

#define __ARG_PLACEHOLDER_1 0,
#define config_enabled(cfg) _config_enabled(cfg)
#define _config_enabled(value) __config_enabled(__ARG_PLACEHOLDER_##value)
#define __config_enabled(arg1_or_junk) ___config_enabled(arg1_or_junk 1, 0)
#define ___config_enabled(__ignored, val, ...) val

/*
 * IS_ENABLED(CONFIG_FOO) evaluates to 1 if CONFIG_FOO is set to 'y' or 'm',
 * 0 otherwise.
 *
 */
#define IS_ENABLED(option) \
	(config_enabled(option) || config_enabled(option##_MODULE))

/*
 * IS_BUILTIN(CONFIG_FOO) evaluates to 1 if CONFIG_FOO is set to 'y', 0
 * otherwise. For boolean options, this is equivalent to
 * IS_ENABLED(CONFIG_FOO).
 */
#define IS_BUILTIN(option) config_enabled(option)

/*
 * IS_MODULE(CONFIG_FOO) evaluates to 1 if CONFIG_FOO is set to 'm', 0
 * otherwise.
 */
#define IS_MODULE(option) config_enabled(option##_MODULE)


#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)) */

#ifndef IS_REACHABLE
#define IS_REACHABLE(option) (config_enabled(option) || \
	      (config_enabled(option##_MODULE) && config_enabled(MODULE)))
#endif

#endif /* _COMPAT_LINUX_KCONFIG_H */
