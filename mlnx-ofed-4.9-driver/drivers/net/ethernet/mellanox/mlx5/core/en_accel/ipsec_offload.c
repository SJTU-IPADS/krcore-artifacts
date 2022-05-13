// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIBt
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#include "mlx5_core.h"
#include "ipsec_offload.h"
#include "lib/mlx5.h"
#include "ipsec_steering.h"

struct mlx5e_ipsec_esp_xfrm {
	/* reference counter of SA ctx */
	struct mlx5e_ipsec_sa_ctx	*sa_ctx;
	struct mutex	lock; /* protects mlx5e_ipsec_esp_xfrm */
	struct mlx5_accel_esp_xfrm	accel_xfrm;
};

u32 mlx5e_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	u32 ret = MLX5_ACCEL_IPSEC_CAP_DEVICE |
		  MLX5_ACCEL_IPSEC_CAP_IPV6 |
		  MLX5_ACCEL_IPSEC_CAP_LSO;

	if (!mlx5e_is_ipsec_device(mdev))
		return 0;

	if (!MLX5_CAP_FLOWTABLE_NIC_TX(mdev, ipsec_encrypt) ||
	    !MLX5_CAP_FLOWTABLE_NIC_RX(mdev, ipsec_decrypt))
		return 0;

	if (MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_encrypt) &&
	    MLX5_CAP_IPSEC(mdev, ipsec_crypto_esp_aes_gcm_128_decrypt))
		ret |= MLX5_ACCEL_IPSEC_CAP_ESP;

	if (MLX5_CAP_IPSEC(mdev, ipsec_esn)) {
		ret |= MLX5_ACCEL_IPSEC_CAP_ESN;
		ret |= MLX5_ACCEL_IPSEC_CAP_TX_IV_IS_ESN;
	}

	WARN_ON(MLX5_CAP_IPSEC(mdev, log_max_ipsec_offload) > 24);
	return ret;
}

static int
mlx5e_ipsec_esp_validate_xfrm_attrs(struct mlx5_core_dev *mdev,
				    const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	if (attrs->replay_type != MLX5_ACCEL_ESP_REPLAY_NONE) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with anti replay\n");
		return -EOPNOTSUPP;
	}

	if (attrs->keymat_type != MLX5_ACCEL_ESP_KEYMAT_AES_GCM) {
		mlx5_core_err(mdev, "Only aes gcm keymat is supported\n");
		return -EOPNOTSUPP;
	}

	if (attrs->keymat.aes_gcm.iv_algo !=
	    MLX5_ACCEL_ESP_AES_GCM_IV_ALGO_SEQ) {
		mlx5_core_err(mdev, "Only iv sequence algo is supported\n");
		return -EOPNOTSUPP;
	}

	if (attrs->keymat.aes_gcm.key_len != 128 &&
	    attrs->keymat.aes_gcm.key_len != 256) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with AEAD key length other than 128/256 bit\n");
		return -EOPNOTSUPP;
	}

	if ((attrs->flags & MLX5_ACCEL_ESP_FLAGS_ESN_TRIGGERED) &&
	    !MLX5_CAP_IPSEC(mdev, ipsec_esn)) {
		mlx5_core_err(mdev, "Cannot offload xfrm states with ESN triggered\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

struct mlx5_accel_esp_xfrm *
mlx5e_ipsec_esp_create_xfrm(struct mlx5_core_dev *mdev,
			    const struct mlx5_accel_esp_xfrm_attrs *attrs,
			    u32 flags)
{
	struct mlx5e_ipsec_esp_xfrm *mxfrm;

	if (mlx5e_ipsec_esp_validate_xfrm_attrs(mdev, attrs)) {
		mlx5_core_warn(mdev, "Tried to create an esp with unsupported attrs\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	mxfrm = kzalloc(sizeof(*mxfrm), GFP_KERNEL);
	if (!mxfrm)
		return ERR_PTR(-ENOMEM);

	mutex_init(&mxfrm->lock);
	memcpy(&mxfrm->accel_xfrm.attrs, attrs,
	       sizeof(mxfrm->accel_xfrm.attrs));

	return &mxfrm->accel_xfrm;
}

void *mlx5e_ipsec_create_sa_ctx(struct mlx5_core_dev *mdev,
				struct mlx5_accel_esp_xfrm *accel_xfrm,
				u32 *hw_handle)
{
	struct mlx5e_ipsec_esp_xfrm *mxfrm =
		container_of(accel_xfrm,
			     struct mlx5e_ipsec_esp_xfrm,
			     accel_xfrm);
	struct mlx5_accel_esp_xfrm_attrs *xfrm_attrs = &accel_xfrm->attrs;
	struct aes_gcm_keymat *aes_gcm = &xfrm_attrs->keymat.aes_gcm;
	struct mlx5_ipsec_obj_attrs ipsec_attrs = {0};
	struct mlx5e_ipsec_sa_ctx *sa_ctx;
	int err;

	/* alloc SA context */
	sa_ctx = kzalloc(sizeof(*sa_ctx), GFP_KERNEL);
	if (!sa_ctx)
		return ERR_PTR(-ENOMEM);

	sa_ctx->dev = mdev;

	mutex_lock(&mxfrm->lock);
	sa_ctx->mxfrm = mxfrm;

	/* key */
	err = mlx5_create_encryption_key(mdev, aes_gcm->aes_key,
					 aes_gcm->key_len / BITS_PER_BYTE,
					 MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_TYPE_IPSEC,
					 &sa_ctx->enc_key_id);
	if (err) {
		mlx5_core_dbg(mdev, "mlx5e: Failed to create encryption key\n");
		goto err_sa_ctx;
	}

	ipsec_attrs.aes_gcm = aes_gcm;
	ipsec_attrs.accel_flags = accel_xfrm->attrs.flags;
	ipsec_attrs.esn_msb = htonl(accel_xfrm->attrs.esn);
	ipsec_attrs.enc_key_id = sa_ctx->enc_key_id;
	err = mlx5_create_ipsec_obj(mdev, &ipsec_attrs,
				    &sa_ctx->ipsec_obj_id);
	if (err) {
		mlx5_core_dbg(mdev, "mlx5e: Failed to create IPsec object\n");
		goto err_enc_key;
	}

	err = mlx5e_xfrm_add_rule(mdev, xfrm_attrs, sa_ctx);
	if (err)
		goto err_ipsec_obj;

	*hw_handle = sa_ctx->ipsec_obj_id;
	mxfrm->sa_ctx = sa_ctx;
	mutex_unlock(&mxfrm->lock);

	return sa_ctx;

err_ipsec_obj:
	mlx5_destroy_ipsec_obj(mdev, sa_ctx->ipsec_obj_id);
err_enc_key:
	mlx5_destroy_encryption_key(mdev, sa_ctx->enc_key_id);
err_sa_ctx:
	mutex_unlock(&mxfrm->lock);
	kfree(sa_ctx);
	return ERR_PTR(err);
}

static void mlx5e_ipsec_release_sa_ctx(struct mlx5e_ipsec_sa_ctx *sa_ctx)
{
	mlx5e_xfrm_del_rule(sa_ctx);
	mlx5_destroy_ipsec_obj(sa_ctx->dev, sa_ctx->ipsec_obj_id);
	mlx5_destroy_encryption_key(sa_ctx->dev, sa_ctx->enc_key_id);
	kfree(sa_ctx);
}

void mlx5e_ipsec_delete_sa_ctx(void *context)
{
	struct mlx5e_ipsec_sa_ctx *sa_ctx =
			(struct mlx5e_ipsec_sa_ctx *)context;
	struct mlx5e_ipsec_esp_xfrm *mxfrm = sa_ctx->mxfrm;

	mutex_lock(&mxfrm->lock);
	mlx5e_ipsec_release_sa_ctx(mxfrm->sa_ctx);
	mxfrm->sa_ctx = NULL;
	mutex_unlock(&mxfrm->lock);
}

void mlx5e_ipsec_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm)
{
	struct mlx5e_ipsec_esp_xfrm *mxfrm =
		container_of(xfrm, struct mlx5e_ipsec_esp_xfrm,
			     accel_xfrm);
	/* assuming no sa_ctx are connected to this xfrm_ctx */
	kfree(mxfrm);
}

int mlx5e_ipsec_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
				const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	struct mlx5_ipsec_obj_attrs ipsec_attrs = {0};
	struct mlx5_core_dev *mdev = xfrm->mdev;
	struct mlx5e_ipsec_esp_xfrm *mxfrm;

	int err = 0;

	if (!memcmp(&xfrm->attrs, attrs, sizeof(xfrm->attrs)))
		return 0;

	if (mlx5e_ipsec_esp_validate_xfrm_attrs(mdev, attrs)) {
		mlx5_core_warn(mdev, "Tried to create an esp with unsupported attrs\n");
		return -EOPNOTSUPP;
	}

	mxfrm = container_of(xfrm, struct mlx5e_ipsec_esp_xfrm, accel_xfrm);

	mutex_lock(&mxfrm->lock);

	if (!mxfrm->sa_ctx)
		/* Unbounded xfrm, change only sw attrs */
		goto change_sw_xfrm_attrs;

	/* need to add find and replace in ipsec_rhash_sa the sa_ctx */
	/* modify device with new hw_sa */
	ipsec_attrs.accel_flags = attrs->flags;
	ipsec_attrs.esn_msb = htonl(attrs->esn);
	err = mlx5_modify_ipsec_obj(mdev,
				    &ipsec_attrs,
				    mxfrm->sa_ctx->ipsec_obj_id);

change_sw_xfrm_attrs:
	if (!err)
		memcpy(&xfrm->attrs, attrs, sizeof(xfrm->attrs));

	mutex_unlock(&mxfrm->lock);
	return err;
}
