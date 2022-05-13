// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2019 Mellanox Technologies.

#include "mlx5_core.h"
#include "lib/mlx5.h"
#include <linux/mlx5/accel.h>

int mlx5_create_encryption_key(struct mlx5_core_dev *mdev,
			       void *key, u32 sz_bytes,
			       u32 key_type, u32 *p_key_id)
{
	u32 in[MLX5_ST_SZ_DW(create_encryption_key_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u32 sz_bits = sz_bytes * BITS_PER_BYTE;
	u8  general_obj_key_size;
	u64 general_obj_types;
	void *obj, *key_p;
	int err;

	obj = MLX5_ADDR_OF(create_encryption_key_in, in, encryption_key_object);
	key_p = MLX5_ADDR_OF(encryption_key_obj, obj, key);

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types &
	      MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY))
		return -EINVAL;

	switch (sz_bits) {
	case 128:
		general_obj_key_size =
			MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_128;
		key_p += sz_bytes;
		break;
	case 256:
		general_obj_key_size =
			MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_256;
		break;
	default:
		return -EINVAL;
	}

	memcpy(key_p, key, sz_bytes);

	MLX5_SET(encryption_key_obj, obj, key_size, general_obj_key_size);
	MLX5_SET(encryption_key_obj, obj, key_type,
		 key_type);
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY);
	MLX5_SET(encryption_key_obj, obj, pd, mdev->mlx5e_res.pdn);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*p_key_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	/* avoid leaking key on the stack */
	memzero_explicit(in, sizeof(in));

	return err;
}

void mlx5_destroy_encryption_key(struct mlx5_core_dev *mdev, u32 key_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, key_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

#ifdef CONFIG_MLX5_IPSEC

int mlx5_create_ipsec_obj(struct mlx5_core_dev *mdev,
			  struct mlx5_ipsec_obj_attrs *attrs,
			  u32 *ipsec_id)
{
	const struct aes_gcm_keymat *aes_gcm = attrs->aes_gcm;
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	u32 in[MLX5_ST_SZ_DW(create_ipsec_obj_in)] = {};
	void *obj, *salt_p, *salt_iv_p;
	u64 general_obj_types;
	int err;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types & MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return -EINVAL;

	obj = MLX5_ADDR_OF(create_ipsec_obj_in, in, ipsec_object);

	/* salt and seq_iv */
	salt_p = MLX5_ADDR_OF(ipsec_obj, obj, salt);
	memcpy(salt_p, &aes_gcm->salt, sizeof(aes_gcm->salt));

	switch (aes_gcm->icv_len) {
	case 64:
		MLX5_SET(ipsec_obj, obj, icv_length,
			 MLX5_IPSEC_OBJECT_ICV_LEN_8B);
		break;
	case 96:
		MLX5_SET(ipsec_obj, obj, icv_length,
			 MLX5_IPSEC_OBJECT_ICV_LEN_12B);
		break;
	case 128:
		MLX5_SET(ipsec_obj, obj, icv_length,
			 MLX5_IPSEC_OBJECT_ICV_LEN_16B);
		break;
	default:
		return -EINVAL;
	}
	salt_iv_p = MLX5_ADDR_OF(ipsec_obj, obj, implicit_iv);
	memcpy(salt_iv_p, &aes_gcm->seq_iv, sizeof(aes_gcm->seq_iv));
	/* esn */
	if (attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED) {
		MLX5_SET(ipsec_obj, obj, esn_en, 1);
		MLX5_SET(ipsec_obj, obj, esn_msb, htonl(attrs->esn_msb));
		if (attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP)
			MLX5_SET(ipsec_obj, obj, esn_overlap, 1);
	}

	MLX5_SET(ipsec_obj, obj, dekn, attrs->enc_key_id);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*ipsec_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	return err;
}

void mlx5_destroy_ipsec_obj(struct mlx5_core_dev *mdev, u32 ipsec_id)
{
	u32 in[MLX5_ST_SZ_DW(general_obj_in_cmd_hdr)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];

	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id, ipsec_id);

	mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

int mlx5_modify_ipsec_obj(struct mlx5_core_dev *mdev,
			  struct mlx5_ipsec_obj_attrs *attrs,
			  u32 ipsec_id)
{
	u32 in[MLX5_ST_SZ_DW(modify_ipsec_obj_in)] = {};
	u32 out[MLX5_ST_SZ_DW(query_ipsec_obj_out)];
	u64 modify_field_select = 0;
	u64 general_obj_types;
	void *obj;
	int err;

	if (!(attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED))
		return 0;

	general_obj_types = MLX5_CAP_GEN_64(mdev, general_obj_types);
	if (!(general_obj_types & MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC))
		return -EINVAL;

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_IPSEC);
	MLX5_SET(general_obj_in_cmd_hdr, in, obj_id,
		 ipsec_id);
	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
	if (err) {
		mlx5_core_err(mdev, "Query IPsec object failed (Object id %d), err = %d\n",
			      ipsec_id, err);
		goto out_err;
	}

	obj = MLX5_ADDR_OF(query_ipsec_obj_out, out, ipsec_object);
	modify_field_select = MLX5_GET64(ipsec_obj, obj, modify_field_select);

	/* esn */
	if (!(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_OVERLAP) ||
	    !(modify_field_select & MLX5_MODIFY_IPSEC_BITMASK_ESN_MSB))
		return -EOPNOTSUPP;

	obj = MLX5_ADDR_OF(modify_ipsec_obj_in, in, ipsec_object);
	MLX5_SET(ipsec_obj, obj, esn_msb, htonl(attrs->esn_msb));
	if (attrs->accel_flags & MLX5_ACCEL_ESP_FLAGS_ESN_STATE_OVERLAP)
		MLX5_SET(ipsec_obj, obj, esn_overlap, 1);

	/* general object fields set */
	MLX5_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);

	err = mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));

out_err:
	return err;
}
#endif
