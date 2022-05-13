/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __MLX5_CORE_H__
#define __MLX5_CORE_H__

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/if_link.h>
#include <linux/firmware.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/driver.h>

#include "fs_core.h"

#define DRIVER_NAME "mlx5_core"
#define DRIVER_VERSION	"4.9-3.1.5"

#define MLX5_DEFAULT_COMP_IRQ_NAME "mlx5_comp%d"

/* Number of EQs reserved for non-completion purposes */
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
#define MLX5_MAX_ASYNC_EQS 4
#else
#define MLX5_MAX_ASYNC_EQS 3
#endif


extern uint mlx5_core_debug_mask;

#define mlx5_core_dbg(__dev, format, ...)				\
	dev_dbg((__dev)->device, "%s:%d:(pid %d): " format,		\
		 __func__, __LINE__, current->pid,			\
		 ##__VA_ARGS__)

#define mlx5_core_dbg_once(__dev, format, ...)		\
	dev_dbg_once((__dev)->device,		\
		     "%s:%d:(pid %d): " format,		\
		     __func__, __LINE__, current->pid,	\
		     ##__VA_ARGS__)

#define mlx5_core_dbg_mask(__dev, mask, format, ...)		\
do {								\
	if ((mask) & mlx5_core_debug_mask)			\
		mlx5_core_dbg(__dev, format, ##__VA_ARGS__);	\
} while (0)

#define mlx5_core_err(__dev, format, ...)			\
	dev_err((__dev)->device, "%s:%d:(pid %d): " format,	\
		__func__, __LINE__, current->pid,		\
	       ##__VA_ARGS__)

#define mlx5_core_err_rl(__dev, format, ...)			\
	dev_err_ratelimited((__dev)->device,			\
			    "%s:%d:(pid %d): " format,		\
			    __func__, __LINE__, current->pid,	\
			    ##__VA_ARGS__)

#define mlx5_core_warn(__dev, format, ...)			\
	dev_warn((__dev)->device, "%s:%d:(pid %d): " format,	\
		 __func__, __LINE__, current->pid,		\
		 ##__VA_ARGS__)

#define mlx5_core_warn_once(__dev, format, ...)				\
	dev_warn_once((__dev)->device, "%s:%d:(pid %d): " format,	\
		      __func__, __LINE__, current->pid,			\
		      ##__VA_ARGS__)

#define mlx5_core_warn_rl(__dev, format, ...)			\
	dev_warn_ratelimited((__dev)->device,			\
			     "%s:%d:(pid %d): " format,		\
			     __func__, __LINE__, current->pid,	\
			     ##__VA_ARGS__)

#define mlx5_core_info(__dev, format, ...)		\
	dev_info((__dev)->device, format, ##__VA_ARGS__)

#define mlx5_core_info_rl(__dev, format, ...)			\
	dev_info_ratelimited((__dev)->device,			\
			     "%s:%d:(pid %d): " format,		\
			     __func__, __LINE__, current->pid,	\
			     ##__VA_ARGS__)

enum {
	MLX5_CMD_DATA, /* print command payload only */
	MLX5_CMD_TIME, /* print command execution time */
};

enum {
	MLX5_DRIVER_STATUS_ABORTED = 0xfe,
	MLX5_DRIVER_SYND = 0xbadd00de,
};

enum mlx5_semaphore_space_address {
	MLX5_SEMAPHORE_SPACE_DOMAIN     = 0xA,
	MLX5_SEMAPHORE_SW_RESET         = 0x20,
};

enum mlx5_pddr_page_select {
	MLX5_PDDR_OPERATIONAL_INFO_PAGE            = 0x0,
	MLX5_PDDR_TROUBLESHOOTING_INFO_PAGE        = 0x1,
	MLX5_PDDR_MODULE_INFO_PAGE                 = 0x3,
};

enum mlx5_pddr_monitor_opcodes {
	MLX5_LINK_NO_ISSUE_OBSERVED                = 0x0,
	MLX5_LINK_PORT_CLOSED                      = 0x1,
	MLX5_LINK_AN_FAILURE                       = 0x2,
	MLX5_LINK_TRAINING_FAILURE                 = 0x5,
	MLX5_LINK_LOGICAL_MISMATCH                 = 0x9,
	MLX5_LINK_REMOTE_FAULT_INDICATION          = 0xe,
	MLX5_LINK_BAD_SIGNAL_INTEGRITY             = 0xf,
	MLX5_LINK_CABLE_COMPLIANCE_CODE_MISMATCH   = 0x10,
	MLX5_LINK_INTERNAL_ERR                     = 0x17,
	MLX5_LINK_INFO_NOT_AVAIL                   = 0x3ff,
	MLX5_LINK_CABLE_UNPLUGGED                  = 0x400,
	MLX5_LINK_LONG_RANGE_FOR_NON_MLX_CABLE     = 0x401,
	MLX5_LINK_BUS_STUCK                        = 0x402,
	MLX5_LINK_UNSUPP_EEPROM                    = 0x403,
	MLX5_LINK_PART_NUM_LIST                    = 0x404,
	MLX5_LINK_UNSUPP_CABLE                     = 0x405,
	MLX5_LINK_MODULE_TEMP_SHUTDOWN             = 0x406,
	MLX5_LINK_SHORTED_CABLE                    = 0x407,
	MLX5_LINK_POWER_BUDGET_EXCEEDED            = 0x408,
	MLX5_LINK_MNG_FORCED_DOWN                  = 0x409,
};

enum mlx5_icmd_conf_address {
	MLX5_ICMD_CTRL		= 0x0,      /* RW */
	MLX5_ICMD_MAILBOX_SZ	= 0x1000,   /* RO */
	MLX5_ICMD_SYNDROME	= 0x1008,   /* RO */
	MLX5_ICMD_MAILBOX	= 0x100000, /* RW */
};

enum mlx5_icmd_ctrl_opcode {
	MLX5_ICMD_ACCESS_REG	= 0x9001,
};

enum mlx5_icmd_access_reg_id {
	MLX5_ICMD_MCION		= 0x9052,
};

enum mlx5_icmd_access_reg_method {
	MLX5_ICMD_QUERY		= 0x1,
	MLX5_ICMD_WRITE		= 0x2,
};

enum {
	MLX5_ICMD_ACCESS_REG_DATA_DW_SZ = 0x2,
};

struct mlx5_icmd_ctrl_bits {
	u16 opcode;
	u8  status;
	u8  busy;
} __packed;

struct mlx5_icmd_access_reg_input_bits {
	u16 constant_1_2;
	u8  reserved_0[0x2];
	u16 register_id;
	u8  method;
	u8  constant_3;
	u8  reserved_1[0x8];
	u16 len;
	u8  reserved_2[0x2];
	u32 reg_data[MLX5_ICMD_ACCESS_REG_DATA_DW_SZ];
} __packed;

struct mlx5_icmd_access_reg_output_bits {
	u8  reserved_0[0x2];
	u8  status;
	u8  reserved_1[0x1];
	u16 register_id;
	u8  reserved_2[0xA];
	u16 len;
	u8  reserved_3[0x2];
	u32 reg_data[MLX5_ICMD_ACCESS_REG_DATA_DW_SZ];
} __packed;

struct mlx5_mcion_reg {
	u8  reserved_0[0x1];
	u8  module;
	u8  reserved_1[0x5];
	u8  module_status;
} __packed;

#define MLX5_DEFAULT_PROF	2

int mlx5_query_hca_caps(struct mlx5_core_dev *dev);
int mlx5_query_board_id(struct mlx5_core_dev *dev);
int mlx5_cmd_init_hca(struct mlx5_core_dev *dev, uint32_t *sw_owner_id);
int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev);
int mlx5_cmd_force_teardown_hca(struct mlx5_core_dev *dev);
int mlx5_cmd_fast_teardown_hca(struct mlx5_core_dev *dev);
void mlx5_enter_error_state(struct mlx5_core_dev *dev, bool force);
bool mlx5_sensor_pci_not_working(struct mlx5_core_dev *dev);
void mlx5_error_sw_reset(struct mlx5_core_dev *dev);
void mlx5_disable_device(struct mlx5_core_dev *dev);
void mlx5_recover_device(struct mlx5_core_dev *dev);
void mlx5_rename_comp_eq(struct mlx5_core_dev *dev, unsigned int eq_ix,
			 char *name);
int mlx5_sriov_init(struct mlx5_core_dev *dev);
void mlx5_sriov_cleanup(struct mlx5_core_dev *dev);
int mlx5_sriov_attach(struct mlx5_core_dev *dev);
void mlx5_sriov_detach(struct mlx5_core_dev *dev);
int mlx5_core_sriov_configure(struct pci_dev *dev, int num_vfs);
int mlx5_core_enable_hca(struct mlx5_core_dev *dev, u16 func_id);
int mlx5_sriov_sysfs_init(struct mlx5_core_dev *dev);
void mlx5_sriov_sysfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_create_vfs_sysfs(struct mlx5_core_dev *dev, int num_vfs);
void mlx5_destroy_vfs_sysfs(struct mlx5_core_dev *dev, int num_vfs);
int mlx5_create_vf_group_sysfs(struct mlx5_core_dev *dev,
			       u32 group_id, struct kobject *group_kobj);
void mlx5_destroy_vf_group_sysfs(struct mlx5_core_dev *dev,
				 struct kobject *group_kobj);
int mlx5_core_disable_hca(struct mlx5_core_dev *dev, u16 func_id);
int mlx5_core_enable_sf_hca(struct mlx5_core_dev *dev, u16 sf_func_id);
int mlx5_core_disable_sf_hca(struct mlx5_core_dev *dev, u16 sf_func_id);
int mlx5_create_scheduling_element_cmd(struct mlx5_core_dev *dev, u8 hierarchy,
				       void *context, u32 *element_id);
int mlx5_modify_scheduling_element_cmd(struct mlx5_core_dev *dev, u8 hierarchy,
				       void *context, u32 element_id,
				       u32 modify_bitmask);
int mlx5_destroy_scheduling_element_cmd(struct mlx5_core_dev *dev, u8 hierarchy,
					u32 element_id);
int mlx5_wait_for_pages(struct mlx5_core_dev *dev, int *pages);
u64 mlx5_read_internal_timer(struct mlx5_core_dev *dev,
			     struct ptp_system_timestamp *sts);

void mlx5_cmd_trigger_completions(struct mlx5_core_dev *dev);
void mlx5_cmd_flush(struct mlx5_core_dev *dev);
int mlx5_cq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cq_debugfs_cleanup(struct mlx5_core_dev *dev);

int mlx5_query_pcam_reg(struct mlx5_core_dev *dev, u32 *pcam, u8 feature_group,
			u8 access_reg_group);
int mlx5_query_mcam_reg(struct mlx5_core_dev *dev, u32 *mcap, u8 feature_group,
			u8 access_reg_group);
int mlx5_query_qcam_reg(struct mlx5_core_dev *mdev, u32 *qcam,
			u8 feature_group, u8 access_reg_group);

int mlx5_query_pddr_troubleshooting_info(struct mlx5_core_dev *mdev,
					 u16 *monitor_opcode,
					 u8 *status_message);

void mlx5_lag_remove_mdev(struct mlx5_core_dev *dev);
void mlx5_lag_add_mdev(struct mlx5_core_dev *dev);
void mlx5_lag_add(struct mlx5_core_dev *dev,
		  struct net_device *netdev,
		  bool intf_mutex_held);
void mlx5_lag_remove(struct mlx5_core_dev *dev,
		     bool intf_mutex_held);

int mlx5_irq_table_init(struct mlx5_core_dev *dev);
void mlx5_irq_table_cleanup(struct mlx5_core_dev *dev);
int mlx5_irq_table_create(struct mlx5_core_dev *dev);
void mlx5_irq_table_destroy(struct mlx5_core_dev *dev);
int mlx5_irq_attach_nb(struct mlx5_irq_table *irq_table, int vecidx,
		       struct notifier_block *nb);
int mlx5_irq_detach_nb(struct mlx5_irq_table *irq_table, int vecidx,
		       struct notifier_block *nb);
void mlx5_irq_rename(struct mlx5_core_dev *dev, int vecidx,
		     const char *name);
struct cpumask *
mlx5_irq_get_affinity_mask(struct mlx5_irq_table *irq_table, int vecidx);
struct cpu_rmap *mlx5_irq_get_rmap(struct mlx5_irq_table *table);
int mlx5_irq_get_num_comp(struct mlx5_irq_table *table);

int mlx5_events_init(struct mlx5_core_dev *dev);
void mlx5_events_cleanup(struct mlx5_core_dev *dev);
void mlx5_events_start(struct mlx5_core_dev *dev);
void mlx5_events_stop(struct mlx5_core_dev *dev);

void mlx5_lag_enable(struct mlx5_core_dev *dev, struct mlx5_lag *ldev);
struct mlx5_lag *mlx5_lag_disable(struct mlx5_core_dev *dev);

#ifdef CONFIG_MLX5_ESWITCH
int esw_offloads_load_all_reps(struct mlx5_eswitch *esw);
int esw_offloads_reload_reps(struct mlx5_eswitch *esw);
int esw_offloads_config_single_fdb(struct mlx5_eswitch *master_esw,
				   struct mlx5_eswitch *slave_esw);
void esw_offloads_destroy_single_fdb(struct mlx5_eswitch *master_esw,
				     struct mlx5_eswitch *slave_esw);
#else
static inline int esw_offloads_load_all_reps(struct mlx5_eswitch *esw) { return 0; }
static inline int esw_offloads_reload_reps(struct mlx5_eswitch *esw) { return 0; }
static inline int esw_offloads_config_single_fdb(struct mlx5_eswitch *master_esw,
                                  struct mlx5_eswitch *slave_esw) { return 0; }
static inline void esw_offloads_destroy_single_fdb(struct mlx5_eswitch *master_esw,
                                    struct mlx5_eswitch *slave_esw) { return;  }
#endif
struct mlx5_vport *mlx5_eswitch_get_vport(struct mlx5_eswitch *esw,
					  u16 vport_num);
struct mlx5_flow_root_namespace *find_root(struct fs_node *node);

void mlx5_add_device(struct mlx5_interface *intf, struct mlx5_priv *priv);
void mlx5_remove_device(struct mlx5_interface *intf, struct mlx5_priv *priv);
void mlx5_attach_device(struct mlx5_core_dev *dev);
void mlx5_attach_device_by_protocol(struct mlx5_core_dev *dev, int protocol);
void mlx5_detach_device(struct mlx5_core_dev *dev);
bool mlx5_device_registered(struct mlx5_core_dev *dev);
int mlx5_register_device(struct mlx5_core_dev *dev);
void mlx5_unregister_device(struct mlx5_core_dev *dev);
void mlx5_add_dev_by_protocol(struct mlx5_core_dev *dev, int protocol);
void mlx5_remove_dev_by_protocol(struct mlx5_core_dev *dev, int protocol);
struct mlx5_core_dev *mlx5_get_next_phys_dev(struct mlx5_core_dev *dev);
void mlx5_dev_list_lock(void);
void mlx5_dev_list_unlock(void);
int mlx5_dev_list_trylock(void);

bool mlx5_lag_intf_add(struct mlx5_interface *intf, struct mlx5_priv *priv);

int mlx5_query_mtpps(struct mlx5_core_dev *dev, u32 *mtpps, u32 mtpps_size);
int mlx5_set_mtpps(struct mlx5_core_dev *mdev, u32 *mtpps, u32 mtpps_size);
int mlx5_query_mtppse(struct mlx5_core_dev *mdev, u8 pin, u8 *arm, u8 *mode);
int mlx5_set_mtppse(struct mlx5_core_dev *mdev, u8 pin, u8 arm, u8 mode);

struct mlx5_dm *mlx5_dm_create(struct mlx5_core_dev *dev);
void mlx5_dm_cleanup(struct mlx5_core_dev *dev);

#define MLX5_PPS_CAP(mdev) (MLX5_CAP_GEN((mdev), pps) &&		\
			    MLX5_CAP_GEN((mdev), pps_modify) &&		\
			    MLX5_CAP_MCAM_FEATURE((mdev), mtpps_fs) &&	\
			    MLX5_CAP_MCAM_FEATURE((mdev), mtpps_enh_out_per_adj))

ssize_t mlx5_show_counters_ct(char *buf);

int mlx5_firmware_flash(struct mlx5_core_dev *dev, const struct firmware *fw,
			struct netlink_ext_ack *extack);
int mlx5_fw_version_query(struct mlx5_core_dev *dev,
			  u32 *running_ver, u32 *stored_ver);

enum {
	UNLOCK,
	LOCK,
	CAP_ID = 0x9,
};

int mlx5_pciconf_cap9_sem(struct mlx5_core_dev *dev, int state);
int mlx5_pciconf_set_addr_space(struct mlx5_core_dev *dev, u16 space);
int mlx5_pciconf_set_protected_addr_space(struct mlx5_core_dev *dev,
					  u32 *ret_space_size);
int mlx5_block_op_pciconf(struct mlx5_core_dev *dev,
			  unsigned int offset, u32 *data,
			  int length);
int mlx5_block_op_pciconf_fast(struct mlx5_core_dev *dev,
			       u32 *data,
			       int length);
int mlx5_mst_dump_init(struct mlx5_core_dev *dev);
int mlx5_mst_capture(struct mlx5_core_dev *dev);
u32 mlx5_mst_dump(struct mlx5_core_dev *dev, void *buff, u32 buff_sz);
void mlx5_mst_free_capture(struct mlx5_core_dev *dev);
void mlx5_mst_dump_cleanup(struct mlx5_core_dev *dev);

int mlx5_icmd_access_register(struct mlx5_core_dev *dev,
			      int reg_id,
			      int method,
			      void *io_buff,
			      u32 io_buff_dw_sz);

void mlx5e_init(void);
void mlx5e_cleanup(void);

int mlx5_modify_other_hca_cap_roce(struct mlx5_core_dev *mdev,
				   u16 function_id, bool value);
int mlx5_get_other_hca_cap_roce(struct mlx5_core_dev *mdev,
				u16 function_id, bool *value);

static inline bool mlx5_sriov_is_enabled(struct mlx5_core_dev *dev)
{
	return pci_num_vf(dev->pdev) ? true : false;
}

/* crdump */
struct mlx5_fw_crdump {
	u32	crspace_size;
	/* sync reading/freeing the data */
	struct mutex crspace_mutex;
	u32	vsec_addr;
	u8	*crspace;
	u16	space;
};

int mlx5_cr_protected_capture(struct mlx5_core_dev *dev);

#define MLX5_CORE_PROC "driver/mlx5_core"
#define MLX5_CORE_PROC_CRDUMP "crdump"
extern struct proc_dir_entry *mlx5_crdump_dir;
#define MLX5_CORE_PROC_SMFS_DUMP "smfs_dump"
extern struct proc_dir_entry *mlx5_smfs_dump_dir;
#define MLX5_CORE_PROC_SMFS_FDB_DUMP "fdb"
extern struct proc_dir_entry *mlx5_smfs_fdb_dump_dir;

int mlx5_crdump_init(struct mlx5_core_dev *dev);
void mlx5_crdump_cleanup(struct mlx5_core_dev *dev);
int mlx5_fill_cr_dump(struct mlx5_core_dev *dev);

static inline int mlx5_lag_is_lacp_owner(struct mlx5_core_dev *dev)
{
	/* LACP owner conditions:
	 * 1) Function is physical.
	 * 2) LAG is supported by FW.
	 * 3) LAG is managed by driver (currently the only option).
	 */
	return  MLX5_CAP_GEN(dev, vport_group_manager) &&
		   (MLX5_CAP_GEN(dev, num_lag_ports) > 1) &&
		    MLX5_CAP_GEN(dev, lag_master);
}

void mlx5_reload_interface(struct mlx5_core_dev *mdev, int protocol);
void mlx5_reload_interfaces(struct mlx5_core_dev *mdev,
			    int protocol1, int protocol2,
			    bool valid1, bool valid2);
void mlx5_lag_update(struct mlx5_core_dev *dev);

enum {
	MLX5_NIC_IFC_FULL		= 0,
	MLX5_NIC_IFC_DISABLED		= 1,
	MLX5_NIC_IFC_NO_DRAM_NIC	= 2,
	MLX5_NIC_IFC_SW_RESET		= 7
};

u8 mlx5_get_nic_state(struct mlx5_core_dev *dev);
void mlx5_set_nic_state(struct mlx5_core_dev *dev, u8 state);

int mlx5_mdev_init(struct mlx5_core_dev *dev, int profile_idx);
void mlx5_mdev_uninit(struct mlx5_core_dev *dev);
int mlx5_load_one(struct mlx5_core_dev *dev, bool boot);
int mlx5_unload_one(struct mlx5_core_dev *dev, bool cleanup);

#ifdef CONFIG_MLX5_MDEV
void mlx5_meddev_init(struct mlx5_core_dev *dev);
void mlx5_meddev_cleanup(struct mlx5_core_dev *dev);
bool mlx5_medev_can_and_mark_cleanup(struct mlx5_core_dev *dev);

int mlx5_meddev_register_driver(void);
void mlx5_meddev_unregister_driver(void);
#else
static inline void mlx5_meddev_init(struct mlx5_core_dev *dev)
{
}

static inline void mlx5_meddev_cleanup(struct mlx5_core_dev *dev)
{
}

static inline bool mlx5_medev_can_and_mark_cleanup(struct mlx5_core_dev *dev)
{
	return true;
}

static inline int mlx5_meddev_register_driver(void)
{
	return 0;
}

static inline void mlx5_meddev_unregister_driver(void)
{
}
#endif

struct mlx5_core_dev *mlx5_get_core_dev(const struct device *dev);

void mlx5_pcie_print_link_status(struct mlx5_core_dev *dev);

int set_tunneled_operation(struct mlx5_core_dev *mdev,
			   u16 asn_match_mask, u16 asn_match_value,
			   u32 *log_response_bar_size,
			   u64 *response_bar_address);

#ifdef CONFIG_MLX5_MDEV
struct mlx5_sf;

int mlx5_sf_get_mac(struct mlx5_sf *sf, u8 *mac);
int mlx5_sf_set_mac(struct mlx5_sf *sf, u8 *mac);
struct net_device *mlx5_sf_get_netdev(struct mlx5_sf *sf);
#endif
#endif /* __MLX5_CORE_H__ */
