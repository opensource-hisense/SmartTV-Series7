#ifndef _MT_SYNC_WRITE_H
#define _MT_SYNC_WRITE_H

#if defined(__KERNEL__)

#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/system.h>

/*
 * Define macros.
 */

#define reg_sync_writel(v, a) \
        do {    \
            writel((v), IOMEM((a)));   \
            dsb();  \
        } while (0)

#define reg_sync_writew(v, a) \
        do {    \
            writew((v), IOMEM((a)));   \
            dsb();  \
        } while (0)

#define reg_sync_writeb(v, a) \
        do {    \
            writeb((v), IOMEM((a)));   \
            dsb();  \
        } while (0)

#endif  /* __KERNEL__ */

#endif  /* !_MT_SYNC_WRITE_H */
