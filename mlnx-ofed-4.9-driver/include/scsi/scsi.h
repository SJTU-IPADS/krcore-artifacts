#ifndef COMPAT_SCSI_SCSI_H
#define COMPAT_SCSI_SCSI_H

#include_next <scsi/scsi.h>

#ifndef SCSI_MAX_SG_CHAIN_SEGMENTS
#define SCSI_MAX_SG_CHAIN_SEGMENTS SG_MAX_SEGMENTS
#endif

#endif	/* COMPAT_SCSI_SCSI_H */
