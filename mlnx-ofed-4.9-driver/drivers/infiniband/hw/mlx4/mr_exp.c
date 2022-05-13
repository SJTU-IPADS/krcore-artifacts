#include <linux/slab.h>
#include <linux/proc_fs.h>
#include "mlx4_ib.h"

struct ib_mr *mlx4_ib_phys_addr(struct ib_pd *pd, u64 length, u64 virt_addr,
				int access_flags)
{
#ifdef CONFIG_INFINIBAND_PA_MR
	if (virt_addr || length)
		return ERR_PTR(-EINVAL);

	return pd->device->ops.get_dma_mr(pd, access_flags);
#else
	pr_debug("Physical Address MR support wasn't compiled in"
		 "the RDMA subsystem. Recompile with Physical"
		 "Address MR\n");
	return ERR_PTR(-EOPNOTSUPP);
#endif /* CONFIG_INFINIBAND_PA_MR */
}
