#include <stdio.h>

#include "checkers.h"
#include "vector.h"
#include "defaults.h"
#include "structs.h"
#include "config.h"
#include "pgpolicies.h"
#include "prio.h"
#include "hwtable.h"

/*
 * Tuning suggestions on these parameters should go to
 * dm-devel@redhat.com (subscribers-only, see README)
 *
 * You are welcome to claim maintainership over a controller
 * family. Please mail the currently enlisted maintainer and
 * the upstream package maintainer.
 *
 * Please, use the TEMPLATE below to add new hardware.
 *
 * WARNING:
 *
 * Devices with a proprietary handler must also be included in
 * the kernel side. Currently at drivers/scsi/scsi_dh.c
 *
 * Moreover, if a device needs a special treatment by the SCSI
 * subsystem it should be included in drivers/scsi/scsi_devinfo.c
 */

#if 0
	/*
	 * Copy this TEMPLATE to add new hardware.
	 *
	 * Keep only mandatory(.vendor and .product) and modified attributes.
	 * Attributes with default values must be removed.
	 * .vendor, .product, .revision and .bl_product are POSIX Extended regex.
	 *
	 * COMPANY_NAME
	 *
	 * Maintainer: NAME <email>
	 */
	{
		/* Product Name */
		.vendor        = "VENDOR",
		.product       = "PRODUCT",
		.revision      = "REVISION",
		.bl_product    = "BL_PRODUCT",
		.pgpolicy      = FAILOVER,
		.uid_attribute = "ID_SERIAL",
		.selector      = "service-time 0",
		.checker_name  = TUR,
		.alias_prefix  = "mpath",
		.features      = "0",
		.hwhandler     = "0",
		.prio_name     = PRIO_CONST,
		.prio_args     = "",
		.pgfailback    = -FAILBACK_MANUAL,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = 1000,
		.minio_rq      = 1,
		.flush_on_last_del = FLUSH_DISABLED,
		.user_friendly_names = USER_FRIENDLY_NAMES_OFF,
		.fast_io_fail  = 5,
		.dev_loss      = 600,
		.retain_hwhandler = RETAIN_HWHANDLER_ON,
		.detect_prio   = DETECT_PRIO_ON,
		.detect_checker = DETECT_CHECKER_ON,
		.deferred_remove = DEFERRED_REMOVE_OFF,
		.delay_watch_checks = DELAY_CHECKS_OFF,
		.delay_wait_checks = DELAY_CHECKS_OFF,
		.skip_kpartx   = SKIP_KPARTX_OFF,
		.max_sectors_kb = MAX_SECTORS_KB_UNDEF,
		.ghost_delay   = GHOST_DELAY_OFF,
	},
#endif

static struct hwentry default_hw[] = {
	/*
	 * Generic NVMe devices
	 *
	 * Due to the parsing logic in find_hwe(), generic entries
	 * have to be put on top of this list, and more specific ones
	 * below.
	 */
	{
		/* Generic NVMe */
		.vendor        = "NVME",
		.product       = ".*",
		.uid_attribute = DEFAULT_NVME_UID_ATTRIBUTE,
		.checker_name  = NONE,
		.retain_hwhandler = RETAIN_HWHANDLER_OFF,
	},
	/*
	 * Apple
	 *
	 * Maintainer: Shyam Sundar <g.shyamsundar@yahoo.co.in>
	 */
	{
		/* Xserve RAID */
		.vendor        = "APPLE",
		.product       = "Xserve RAID",
		.pgpolicy      = MULTIBUS,
	},
	/*
	 * HPE
	 */
	{
		/* 3PAR / Primera */
		.vendor        = "3PARdata",
		.product       = "VV",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.hwhandler     = "1 alua",
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 18,
		.fast_io_fail  = 10,
		.dev_loss      = MAX_DEV_LOSS_TMO,
	},
	{
		/* RA8000 / ESA12000 */
		.vendor        = "DEC",
		.product       = "HSG80",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.hwhandler     = "1 hp_sw",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = HP_SW,
		.prio_name     = PRIO_HP_SW,
	},
	{
		/* VIRTUAL ARRAY 7400 */
		.vendor        = "HP",
		.product       = "A6189A",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 12,
	},
	{
		/* MSA 1000/1500 and EVA 3000/5000, with old firmware */
		.vendor        = "(COMPAQ|HP)",
		.product       = "(MSA|HSV)1[01]0",
		.hwhandler     = "1 hp_sw",
		.pgpolicy      = GROUP_BY_PRIO,
		.no_path_retry = 12,
		.checker_name  = HP_SW,
		.prio_name     = PRIO_HP_SW,
	},
	{
		/* MSA 1000/1500 with new firmware */
		.vendor        = "(COMPAQ|HP)",
		.product       = "MSA VOLUME",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 12,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* EVA 3000/5000 with new firmware, EVA 4000/6000/8000 */
		.vendor        = "(COMPAQ|HP)",
		.product       = "(HSV1[01]1|HSV2[01]0|HSV3[046]0|HSV4[05]0)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 12,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* MSA2000 family with old firmware */
		.vendor        = "HP",
		.product       = "(MSA2[02]12fc|MSA2012i)",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 18,
	},
	{
		/* MSA2000 family with new firmware */
		.vendor        = "HP",
		.product       = "(MSA2012sa|MSA23(12|24)(fc|i|sa)|MSA2000s VOLUME)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 18,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* MSA 1040, 1050, 2040 and 2050 families */
		.vendor        = "HP",
		.product       = "MSA [12]0[45]0 SA[NS]",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 18,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* SAN Virtualization Services Platform */
		.vendor        = "HP",
		.product       = "HSVX700",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 12,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* Smart Array */
		.vendor        = "HP",
		.product       = "LOGICAL VOLUME",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 12,
	},
	{
		/* P2000 family */
		.vendor        = "HP",
		.product       = "(P2000 G3 FC|P2000G3 FC/iSCSI|P2000 G3 SAS|P2000 G3 iSCSI)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 18,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* StoreVirtual 4000 and 3200 families */
		.vendor        = "LEFTHAND",
		.product       = "(P4000|iSCSIDisk|FCDISK)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 18,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* Nimble Storage */
		.vendor        = "Nimble",
		.product       = "Server",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
	},
		/* SGI */
	{
		/* Total Performance 9100 */
		.vendor        = "SGI",
		.product       = "TP9100",
		.pgpolicy      = MULTIBUS,
	},
	{
		/* Total Performance family */
		.vendor        = "SGI",
		.product       = "TP9[3457]00",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* (RDAC) InfiniteStorage */
		.vendor        = "SGI",
		.product       = "IS",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* (DDN) InfiniteStorage */
		.vendor        = "SGI",
		.product       = "^DD[46]A-",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	/*
	 * DataDirect Networks
	 */
	{
		/* SAN DataDirector */
		.vendor        = "DDN",
		.product       = "SAN DataDirector",
		.pgpolicy      = MULTIBUS,
	},
	{
		/* EF3010 */
		.vendor        = "DDN",
		.product       = "^EF3010",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 30,
	},
	{
		/* EF3015 / S2A and SFA families */
		.vendor        = "DDN",
		.product       = "^(EF3015|S2A|SFA)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	{
		/*
		 * Nexenta COMSTAR
		 *
		 * Maintainer: Yacine Kheddache <yacine@alyseo.com>
		 */
		.vendor        = "NEXENTA",
		.product       = "COMSTAR",
		.pgpolicy      = GROUP_BY_SERIAL,
		.no_path_retry = 30,
	},
	{
		/* Tegile IntelliFlash */
		.vendor        = "TEGILE",
		.product       = "(ZEBI-(FC|ISCSI)|INTELLIFLASH)",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 10,
	},
	/*
	 * Dell EMC
	 */
	{
		/* Symmetrix / DMX / VMAX / PowerMax */
		.vendor        = "EMC",
		.product       = "SYMMETRIX",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 6,
	},
	{
		/* DGC CLARiiON CX/AX / VNX and Unity */
		.vendor        = "^DGC",
		.product       = "^(RAID|DISK|VRAID)",
		.bl_product    = "LUNZ",
		.hwhandler     = "1 emc",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = (300 / DEFAULT_CHECKINT),
		.checker_name  = EMC_CLARIION,
		.prio_name     = PRIO_EMC,
	},
	{
		/* Invista / VPLEX */
		.vendor        = "EMC",
		.product       = "Invista",
		.bl_product    = "LUNZ",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 5,
	},
	{
		/* XtremIO */
		.vendor        = "XtremIO",
		.product       = "XtremApp",
		.pgpolicy      = MULTIBUS,
	},
	{
		/*
		 * SC Series, formerly Compellent
		 *
		 * Maintainer: Sean McGinnis <sean_mcginnis@dell.com>
		 */
		.vendor        = "COMPELNT",
		.product       = "Compellent Vol",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
	},
	{
		/* MD Series */
		.vendor        = "DELL",
		.product       = "^MD3",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* EMC PowerMax NVMe */
		.vendor        = "NVME",
		.product       = "^EMC PowerMax_",
		.pgpolicy      = MULTIBUS,
	},
	/*
	 * Fujitsu
	 */
	{
		/* CentricStor Virtual Tape */
		.vendor        = "FSC",
		.product       = "CentricStor",
		.pgpolicy      = GROUP_BY_SERIAL,
	},
	{
		/* ETERNUS family */
		.vendor        = "FUJITSU",
		.product       = "ETERNUS_DX(H|L|M|400|8000)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 10,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* FibreCAT S80 */
		.vendor        = "(EUROLOGC|EuroLogc)",
		.product       = "FC2502",
		.pgpolicy      = MULTIBUS,
	},
	{
		/* ETERNUS 2000, 3000 and 4000 */
		.vendor        = "FUJITSU",
		.product       = "E[234]000",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 10,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* ETERNUS 6000 and 8000 */
		.vendor        = "FUJITSU",
		.product       = "E[68]000",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 10,
	},
	/*
	 * Hitachi Vantara
	 *
	 * Maintainer: Matthias Rudolph <Matthias.Rudolph@hitachivantara.com>
	 */
	{
		/* USP-V, HUS VM, VSP, VSP G1X00 and VSP GX00 families / HP XP */
		.vendor        = "(HITACHI|HP)",
		.product       = "^OPEN-",
		.pgpolicy      = MULTIBUS,
	},
	{
		/* AMS other than AMS 2000 */
		.vendor        = "HITACHI",
		.product       = "^DF",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_HDS,
	},
	{
		/* AMS 2000 and HUS 100 families */
		.vendor        = "HITACHI",
		.product       = "^DF600F",
		.pgpolicy      = MULTIBUS,
	},
	/*
	 * IBM
	 *
	 * Maintainer: Hannes Reinecke <hare@suse.de>
	 */
	{
		/* ProFibre 4000R */
		.vendor        = "IBM",
		.product       = "ProFibre 4000R",
		.pgpolicy      = MULTIBUS,
	},
	{
		/* DS4300 / FAStT600 */
		.vendor        = "IBM",
		.product       = "^1722-600",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DS4100 / FAStT100 */
		.vendor        = "IBM",
		.product       = "^1724",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DS3000 / DS3200 / DS3300 / DS3400 / Boot DS */
		.vendor        = "IBM",
		.product       = "^1726",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DS4400 / DS4500 / FAStT700 / FAStT900 */
		.vendor        = "IBM",
		.product       = "^1742",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DS3500 / DS3512 / DS3524 */
		.vendor        = "IBM",
		.product       = "^1746",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DCS3860 */
		.vendor        = "IBM",
		.product       = "^1813",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DS3950 / DS4200 / DS4700 / DS5020 */
		.vendor        = "IBM",
		.product       = "^1814",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DS4800 */
		.vendor        = "IBM",
		.product       = "^1815",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* DS5000 / DS5100 / DS5300 / DCS3700 */
		.vendor        = "IBM",
		.product       = "^1818",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* Netfinity Fibre Channel RAID Controller Unit */
		.vendor        = "IBM",
		.product       = "^3526",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* FAStT200 and FAStT500 */
		.vendor        = "IBM",
		.product       = "^(3542|3552)",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* Enterprise Storage Server(ESS) / Shark family */
		.vendor        = "IBM",
		.product       = "^2105",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = MULTIBUS,
	},
	{
		/* DS6000 / DS6800 */
		.vendor        = "IBM",
		.product       = "^1750500",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* DS8000 family */
		.vendor        = "IBM",
		.product       = "^2107900",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = MULTIBUS,
	},
	{
		/* Storwize family / SAN Volume Controller / Flex System V7000 / FlashSystem V840/V9000/9100 */
		.vendor        = "IBM",
		.product       = "^2145",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* PAV DASD ECKD */
		.vendor        = "IBM",
		.product       = "S/390 DASD ECKD",
		.bl_product    = "S/390",
		.uid_attribute = "ID_UID",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = MULTIBUS,
		.checker_name  = DIRECTIO,
	},
	{
		/* PAV DASD FBA */
		.vendor        = "IBM",
		.product       = "S/390 DASD FBA",
		.bl_product    = "S/390",
		.uid_attribute = "ID_UID",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = MULTIBUS,
		.checker_name  = DIRECTIO,
	},
	{
		/* Power RAID */
		.vendor        = "IBM",
		.product       = "^IPR",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* SAS RAID Controller Module (RSSM) */
		.vendor        = "IBM",
		.product       = "1820N00",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* XIV Storage System / FlashSystem A9000/A9000R */
		.vendor        = "(XIV|IBM)",
		.product       = "(NEXTRA|2810XIV)",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = MULTIBUS,
	},
	{
		/* TMS RamSan / FlashSystem 710/720/810/820/840/900 */
		.vendor        = "(TMS|IBM)",
		.product       = "(RamSan|FlashSystem)",
		.pgpolicy      = MULTIBUS,
	},
	{
		/* (DDN) DCS9900, SONAS 2851-DR1 */
		.vendor        = "IBM",
		.product       = "^(DCS9900|2851)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
		/*
		 * IBM Power Virtual SCSI Devices
		 *
		 * Maintainer: Brian King <brking@linux.vnet.ibm.com>
		 */
	{
		/* AIX VDASD */
		.vendor        = "AIX",
		.product       = "VDASD",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = (300 / DEFAULT_CHECKINT),
	},
	{
		/* 3303 NVDISK */
		.vendor        = "IBM",
		.product       = "3303[ ]+NVDISK",
		.no_path_retry = (300 / DEFAULT_CHECKINT),
	},
	{
		/* AIX NVDISK */
		.vendor        = "AIX",
		.product       = "NVDISK",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = (300 / DEFAULT_CHECKINT),
		.prio_name     = PRIO_ALUA,
	},
        /*
         * Lenovo
         */
        {
                /*
		 * DE Series
		 *
		 * Maintainer: NetApp RDAC team <ng-eseries-upstream-maintainers@netapp.com>
		 */
                .vendor        = "LENOVO",
                .product       = "DE_Series",
                .bl_product    = "Universal Xport",
                .pgpolicy      = GROUP_BY_PRIO,
                .checker_name  = RDAC,
                .features      = "2 pg_init_retries 50",
                .hwhandler     = "1 rdac",
                .prio_name     = PRIO_RDAC,
                .pgfailback    = -FAILBACK_IMMEDIATE,
                .no_path_retry = 30,
        },
	/*
	 * NetApp
	 */
	{
		/*
		 * ONTAP family
		 *
		 * Maintainer: Martin George <marting@netapp.com>
		 */
		.vendor        = "NETAPP",
		.product       = "LUN",
		.features      = "2 pg_init_retries 50",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.flush_on_last_del = FLUSH_ENABLED,
		.dev_loss      = MAX_DEV_LOSS_TMO,
		.prio_name     = PRIO_ONTAP,
		.user_friendly_names = USER_FRIENDLY_NAMES_OFF,
	},
	{
		/*
		 * SANtricity(RDAC) family
		 *
		 * Maintainer: NetApp RDAC team <ng-eseries-upstream-maintainers@netapp.com>
		 */
		.vendor        = "(NETAPP|LSI|ENGENIO)",
		.product       = "INF-01-00",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/*
		 * SolidFir family
		 *
		 * Maintainer: PJ Waskiewicz <pj.waskiewicz@netapp.com>
		 */
		.vendor        = "SolidFir",
		.product       = "SSD SAN",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 24,
	},
	{
		/*
		 * NVMe-FC namespace devices: MULTIBUS, queueing preferred
		 *
		 * The hwtable is searched backwards, so place this after "Generic NVMe"
		 */
		.vendor	       = "NVME",
		.product       = "^NetApp ONTAP Controller",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
	},
	/*
	 * NEC
	 */
	{
		/* M-Series */
		.vendor        = "NEC",
		.product       = "DISK ARRAY",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * Oracle
	 */
		/*
		 * Pillar Data / Oracle FS
		 *
		 * Maintainer: Srinivasan Ramani <srinivas.ramani@oracle.com>
		 */
	{
		/* Axiom */
		.vendor        = "^Pillar",
		.product       = "^Axiom",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* FS */
		.vendor        = "^Oracle",
		.product       = "^Oracle FS",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
		/* Sun - StorageTek */
	{
		/* B210, B220, B240 and B280 */
		.vendor        = "STK",
		.product       = "BladeCtlr",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* 9176, D173, D178, D210, D220, D240 and D280 */
		.vendor        = "STK",
		.product       = "OPENstorage",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* 6540 */
		.vendor        = "STK",
		.product       = "FLEXLINE 380",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* (Dot Hill) 3120, 3310, 3320, 3510 and 3511 */
		.vendor        = "SUN",
		.product       = "StorEdge 3",
		.pgpolicy      = MULTIBUS,
	},
	{
		/* 6580 and 6780 */
		.vendor        = "SUN",
		.product       = "STK6580_6780",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* 6130 / 6140 */
		.vendor        = "SUN",
		.product       = "CSM[12]00_R",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* 2500 / 2510 / 2530 / 2540 */
		.vendor        = "SUN",
		.product       = "LCSM100_[IEFS]",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* 6180 */
		.vendor        = "SUN",
		.product       = "SUN_6180",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* ArrayStorage */
		.vendor        = "SUN",
		.product       = "ArrayStorage",
		.bl_product    = "Universal Xport",
		.pgpolicy      = GROUP_BY_PRIO,
		.checker_name  = RDAC,
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.prio_name     = PRIO_RDAC,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
	},
	{
		/* ZFS Storage Appliances */
		.vendor        = "SUN",
		.product       = "(Sun Storage|ZFS Storage|COMSTAR)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	/*
	 * Pivot3
	 *
	 * Maintainer: Bart Brooks <bartb@pivot3.com>
	 */
	{
		/* Raige */
		.vendor        = "PIVOT3",
		.product       = "RAIGE VOLUME",
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.pgpolicy      = MULTIBUS,
	},
	{
		/* NexGen / vSTAC */
		.vendor        = "(NexGen|Pivot3)",
		.product       = "(TierStore|vSTAC)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
	},
	/*
	 * Intel
	 */
	{
		/* Multi-Flex */
		.vendor        = "(Intel|INTEL)",
		.product       = "Multi-Flex",
		.bl_product    = "VTrak V-LUN",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * Linux-IO Target
	 */
	{
		/* Linux-IO Target */
		.vendor        = "(LIO-ORG|SUSE)",
		.product       = "RBD",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 12,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * DataCore
	 */
	{
		/* SANmelody */
		.vendor        = "DataCore",
		.product       = "SANmelody",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* SANsymphony */
		.vendor        = "DataCore",
		.product       = "Virtual Disk",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * Pure Storage
	 */
	{
		/* FlashArray */
		.vendor        = "PURE",
		.product       = "FlashArray",
		.pgpolicy      = MULTIBUS,
	},
	/*
	 * Huawei
	 */
	{
		/* OceanStor V3 */
		.vendor        = "HUAWEI",
		.product       = "XSG1",
		.pgpolicy      = GROUP_BY_PRIO,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * Kove
	 */
	{
		/* XPD */
		.vendor        = "KOVE",
		.product       = "XPD",
		.pgpolicy      = MULTIBUS,
	},
	/*
	 * Infinidat
	 *
	 * Maintainer: Arnon Yaari <arnony@infinidat.com>
	 */
	{
		/* InfiniBox */
		.vendor        = "NFINIDAT",
		.product       = "InfiniBox",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = 30,
		.prio_name     = PRIO_ALUA,
		.selector      = "round-robin 0",
		.rr_weight     = RR_WEIGHT_PRIO,
		.no_path_retry = NO_PATH_RETRY_FAIL,
		.minio         = 1,
		.minio_rq      = 1,
		.flush_on_last_del = FLUSH_ENABLED,
		.fast_io_fail  = 15,
		.dev_loss      = 15,
	},
	/*
	 * Kaminario
	 */
	{
		/* K2 */
		.vendor        = "KMNRIO",
		.product       = "K2",
		.pgpolicy      = MULTIBUS,
	},
	/*
	 * Imation/Nexsan
	 */
	{
		/* E-Series */
		.vendor        = "NEXSAN",
		.product       = "NXS-B0",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 15,
	},
	{
		/* SATABeast / SATABoy */
		.vendor        = "NEXSAN",
		.product       = "SATAB",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 15,
	},
	{
		/* NST / UNITY */
		.vendor        = "Nexsan",
		.product       = "(NestOS|NST5000)",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	/*
	 * Violin Systems
	 */
	{
		/* 3000 / 6000 Series */
		.vendor        = "VIOLIN",
		.product       = "SAN ARRAY$",
		.pgpolicy      = GROUP_BY_SERIAL,
		.no_path_retry = 30,
	},
	{
		/* 3000 / 6000 Series - ALUA mode */
		.vendor        = "VIOLIN",
		.product       = "SAN ARRAY ALUA",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	{
		/* FSP 7000 family */
		.vendor        = "VIOLIN",
		.product       = "CONCERTO ARRAY",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 30,
	},
		/* Xiotech */
	{
		/* Intelligent Storage Elements family */
		.vendor        = "(XIOTECH|XIOtech)",
		.product       = "ISE",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 12,
	},
	{
		/* iglu blaze family */
		.vendor        = "(XIOTECH|XIOtech)",
		.product       = "IGLU DISK",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 30,
	},
	{
		/* Magnitude family */
		.vendor        = "(XIOTECH|XIOtech)",
		.product       = "Magnitude",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 30,
	},
	/*
	 * Promise Technology
	 */
	{
		/* VTrak family */
		.vendor        = "Promise",
		.product       = "VTrak",
		.bl_product    = "VTrak V-LUN",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	{
		/* Vess family */
		.vendor        = "Promise",
		.product       = "Vess",
		.bl_product    = "Vess V-LUN",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	/*
	 * Infortrend Technology
	 */
	{
		/* EonStor / ESVA */
		.vendor        = "^IFT",
		.product       = ".*",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	/*
	 * Seagate Technology (Dot Hill Systems)
	 */
	{
		/* SANnet family */
		.vendor        = "DotHill",
		.product       = "SANnet",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 30,
	},
	{
		/* R/Evolution family */
		.vendor        = "DotHill",
		.product       = "R/Evo",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	{
		/* AssuredSAN family */
		.vendor        = "DotHill",
		.product       = "^DH",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 30,
	},
	/*
	 * AccelStor
	 */
	{
		/* NeoSapphire */
		.vendor        = "AStor",
		.product       = "NeoSapphire",
		.pgpolicy      = MULTIBUS,
		.no_path_retry = 30,
	},
	/*
	 * INSPUR
	 */
	{
		/* AS5300/AS5500 G2 */
		.vendor        = "INSPUR",
		.product       = "MCS",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * EOL
	 */
	{
		/* NULL */
		.vendor        = NULL,
		.product       = NULL,
	},
};

int setup_default_hwtable(vector hw)
{
	int r = 0;
	struct hwentry * hwe = default_hw;

	while (hwe->vendor) {
		r += store_hwe(hw, hwe);
		hwe++;
	}
	return r;
}
