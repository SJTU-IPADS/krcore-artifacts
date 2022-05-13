#ifndef _COMPAT_LINUX_UUID_H
#define _COMPAT_LINUX_UUID_H

#include "../../compat/config.h"
#include <linux/version.h>

#include_next <linux/uuid.h>

#ifndef HAVE_GUID_PARSE
typedef struct {
    __u8 b[16];
} guid_t;
#endif


#ifndef HAVE_UUID_GEN

#define uuid_t		uuid_be
#define uuid_gen	uuid_be_gen
#define uuid_parse	uuid_be_to_bin

#ifndef HAVE_GUID_PARSE
static inline bool guid_equal(const guid_t *u1, const guid_t *u2)
{
	return memcmp(u1, u2, sizeof(guid_t)) == 0;
}

static inline void guid_copy(guid_t *dst, const guid_t *src)
{
	memcpy(dst, src, sizeof(guid_t));
}

int guid_parse(const char *uuid, guid_t *u);
#endif

#ifndef UUID_STRING_LEN
#define        UUID_STRING_LEN         36
#endif

#ifndef HAVE_UUID_BE_TO_BIN
/*
* The length of a UUID string ("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
* not including trailing NUL.
*/
#define uuid_is_valid LINUX_BACKPORT(uuid_is_valid)
bool __must_check uuid_is_valid(const char *uuid);

#define uuid_le_index LINUX_BACKPORT(uuid_le_index)
extern const u8 uuid_le_index[16];
#define uuid_be_index LINUX_BACKPORT(uuid_be_index)
extern const u8 uuid_be_index[16];

#define uuid_le_to_bin LINUX_BACKPORT(uuid_le_to_bin)
int uuid_le_to_bin(const char *uuid, uuid_le *u);
#define uuid_be_to_bin LINUX_BACKPORT(uuid_be_to_bin)
int uuid_be_to_bin(const char *uuid, uuid_be *u);

#endif /* HAVE_UUID_BE_TO_BIN */

#endif /* HAVE_UUID_GEN */

#ifndef HAVE_UUID_EQUAL
static inline bool uuid_equal(const uuid_t *u1, const uuid_t *u2)
{
	return memcmp(u1, u2, sizeof(uuid_t)) == 0;
}

static inline void uuid_copy(uuid_t *dst, const uuid_t *src)
{
	memcpy(dst, src, sizeof(uuid_be));
}
#endif

#endif /* _COMPAT_LINUX_UUID_H */
