#ifndef __COMPAT_ETHTOOL_H
#define __COMPAT_ETHTOOL_H

#include_next <linux/ethtool.h>

/* EEPROM Standards for plug in modules */
#ifndef ETH_MODULE_SFF_8079
#define ETH_MODULE_SFF_8079		0x1
#define ETH_MODULE_SFF_8079_LEN		256
#endif
#ifndef ETH_MODULE_SFF_8436_MAX_LEN
#define ETH_MODULE_SFF_8636_MAX_LEN     640
#define ETH_MODULE_SFF_8436_MAX_LEN     640 
#endif

#ifndef ETHTOOL_FEC_NONE
enum ethtool_fec_config_bits {
	ETHTOOL_FEC_NONE_BIT,
	ETHTOOL_FEC_AUTO_BIT,
	ETHTOOL_FEC_OFF_BIT,
	ETHTOOL_FEC_RS_BIT,
	ETHTOOL_FEC_BASER_BIT,
};

struct ethtool_fecparam {
	__u32   cmd;
	/* bitmask of FEC modes */
	__u32   active_fec;
	__u32   fec;
	__u32   reserved;
};

#define ETHTOOL_FEC_NONE                (1 << ETHTOOL_FEC_NONE_BIT)
#define ETHTOOL_FEC_AUTO                (1 << ETHTOOL_FEC_AUTO_BIT)
#define ETHTOOL_FEC_OFF                 (1 << ETHTOOL_FEC_OFF_BIT)
#define ETHTOOL_FEC_RS                  (1 << ETHTOOL_FEC_RS_BIT)
#define ETHTOOL_FEC_BASER               (1 << ETHTOOL_FEC_BASER_BIT)

#endif
#ifndef ETH_MODULE_SFF_8472
#define ETH_MODULE_SFF_8472		0x2
#define ETH_MODULE_SFF_8472_LEN		512
#endif

#ifndef ETH_MODULE_SFF_8636
#define ETH_MODULE_SFF_8636		0x3
#define ETH_MODULE_SFF_8636_LEN		256
#endif

#ifndef ETH_MODULE_SFF_8436
#define ETH_MODULE_SFF_8436		0x4
#define ETH_MODULE_SFF_8436_LEN		256
#endif

#ifndef SPEED_20000
#define SPEED_20000 20000
#define SUPPORTED_20000baseMLD2_Full    (1 << 21)
#define SUPPORTED_20000baseKR2_Full     (1 << 22)
#define ADVERTISED_20000baseMLD2_Full   (1 << 21)
#define ADVERTISED_20000baseKR2_Full    (1 << 22)
#endif

#ifndef SPEED_40000
#define SPEED_40000 40000
#endif

#ifndef SPEED_56000
#define SPEED_56000 56000
#define SUPPORTED_56000baseKR4_Full	(1 << 27)
#define SUPPORTED_56000baseCR4_Full	(1 << 28)
#define SUPPORTED_56000baseSR4_Full	(1 << 29)
#define SUPPORTED_56000baseLR4_Full	(1 << 30)
#define ADVERTISED_56000baseKR4_Full	(1 << 27)
#define ADVERTISED_56000baseCR4_Full	(1 << 28)
#define ADVERTISED_56000baseSR4_Full	(1 << 29)
#define ADVERTISED_56000baseLR4_Full	(1 << 30)
#endif

#define SPEED_25000 25000
#define SPEED_50000 50000
#define SPEED_100000 100000
#ifndef ETHTOOL_LINK_MODE_25000baseCR_Full_BIT
#define ETHTOOL_LINK_MODE_25000baseCR_Full_BIT 31
#define ETHTOOL_LINK_MODE_25000baseKR_Full_BIT 32
#define ETHTOOL_LINK_MODE_25000baseSR_Full_BIT 33
#define ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT 34
#define ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT 35
#define ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT 36
#define ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT 37
#define ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT 38
#define ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT 39
#endif
#ifndef ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT
#define ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT 40
#endif
#ifndef ETHTOOL_LINK_MODE_1000baseX_Full_BIT
#define ETHTOOL_LINK_MODE_1000baseX_Full_BIT 41
#define ETHTOOL_LINK_MODE_10000baseCR_Full_BIT 42
#define ETHTOOL_LINK_MODE_10000baseSR_Full_BIT 43
#define ETHTOOL_LINK_MODE_10000baseLR_Full_BIT 44
#define ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT 45
#define ETHTOOL_LINK_MODE_10000baseER_Full_BIT 46
#endif
#ifndef ETHTOOL_LINK_MODE_2500baseT_Full_BIT
#define ETHTOOL_LINK_MODE_2500baseT_Full_BIT 47
#define ETHTOOL_LINK_MODE_5000baseT_Full_BIT 48
#endif
#ifndef ETHTOOL_LINK_MODE_FEC_NONE_BIT
#define ETHTOOL_LINK_MODE_FEC_NONE_BIT 49
#define ETHTOOL_LINK_MODE_FEC_RS_BIT 50
#define ETHTOOL_LINK_MODE_FEC_BASER_BIT 51
#endif

#ifndef SPEED_200000
#define SPEED_200000            200000
#define ETHTOOL_LINK_MODE_50000baseKR_Full_BIT 52
#define ETHTOOL_LINK_MODE_50000baseSR_Full_BIT 53
#define ETHTOOL_LINK_MODE_50000baseCR_Full_BIT 54
#define ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT 55
#define ETHTOOL_LINK_MODE_50000baseDR_Full_BIT 56
#define ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT 57
#define ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT 58
#define ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT 59
#define ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT 60
#define ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT 61
#define ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT 62
#define ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT 63
#define ETHTOOL_LINK_MODE_200000baseLR4_ER4_FR4_Full_BIT 64
#define ETHTOOL_LINK_MODE_200000baseDR4_Full_BIT 65
#define ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT 66
#endif
#define SUPPORTED_100000baseCR4_Full 0
#define ADVERTISED_100000baseCR4_Full 0
#define SUPPORTED_100000baseSR4_Full 0
#define ADVERTISED_100000baseSR4_Full 0
#define SUPPORTED_100000baseKR4_Full 0
#define ADVERTISED_100000baseKR4_Full 0
#define SUPPORTED_1000000baseLR4_Full 0
#define ADVERTISED_1000000baseLR4_Full 0
#define SUPPORTED_100baseTX_Full 0
#define ADVERTISED_100baseTX_Full 0
#define SUPPORTED_25000baseCR_Full 0
#define ADVERTISED_25000baseCR_Full 0
#define SUPPORTED_25000baseKR_Full 0
#define ADVERTISED_25000baseKR_Full 0
#define SUPPORTED_25000baseSR_Full 0
#define ADVERTISED_25000baseSR_Full 0
#define SUPPORTED_50000baseCR2_Full 0
#define ADVERTISED_50000baseCR2_Full 0
#define SUPPORTED_50000baseKR2_Full 0
#define ADVERTISED_50000baseKR2_Full 0

#ifndef SPEED_UNKNOWN
#define SPEED_UNKNOWN		-1
#endif

#ifndef DUPLEX_UNKNOWN
#define DUPLEX_UNKNOWN		-1
#endif

#ifndef SUPPORTED_40000baseKR4_Full
/* Add missing defines for supported and advertised speed features */
#define SUPPORTED_40000baseKR4_Full     (1 << 23)
#define SUPPORTED_40000baseCR4_Full     (1 << 24)
#define SUPPORTED_40000baseSR4_Full     (1 << 25)
#define SUPPORTED_40000baseLR4_Full     (1 << 26)
#define ADVERTISED_40000baseKR4_Full    (1 << 23)
#define ADVERTISED_40000baseCR4_Full    (1 << 24)
#define ADVERTISED_40000baseSR4_Full    (1 << 25)
#define ADVERTISED_40000baseLR4_Full    (1 << 26)
#endif

#ifndef FLOW_EXT
#define FLOW_EXT	0x80000000
#endif

#ifndef ETHER_FLOW
#define ETHER_FLOW      0x12    /* spec only (ether_spec) */
#endif

#ifndef SPEED_5000
#define SPEED_5000	5000
#endif

#ifndef SPEED_14000
#define SPEED_14000	14000
#endif

#ifndef PFC_STORM_PREVENTION_AUTO
/* The feature wont work, but this will save backport lines */
#define PFC_STORM_PREVENTION_AUTO	0xffff
#define PFC_STORM_PREVENTION_DISABLE	0
#define ETHTOOL_PFC_PREVENTION_TOUT	3
#endif

#endif
