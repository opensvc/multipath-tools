#include <stdio.h>

#include "checkers.h"
#include "vector.h"
#include "defaults.h"
#include "structs.h"
#include "config.h"
#include "pgpolicies.h"
#include "prio.h"

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
 */
static struct hwentry default_hw[] = {
	/*
	 * Apple
	 *
	 * Maintainer : Shyam Sundar
	 * Mail : g.shyamsundar@yahoo.co.in
	 */
	{
		.vendor        = "APPLE",
		.product       = "Xserve RAID",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	/*
	 * HPE
	 */
	{
		.vendor        = "3PARdata",
		.product       = "VV",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.hwhandler     = "1 alua",
		.prio_name     = PRIO_ALUA,
		.no_path_retry = 18,
	},
	{
		/* RA8000 / ESA12000 */
		.vendor        = "DEC",
		.product       = "HSG80",
		.features      = "1 queue_if_no_path",
		.hwhandler     = "1 hp_sw",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = FAILBACK_UNDEF,
		.checker_name  = HP_SW,
		.prio_name     = PRIO_HP_SW,
	},
	{
		/* VIRTUAL ARRAY 7400 */
		.vendor        = "HP",
		.product       = "A6189A",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = 12,
	},
	{
		/* MSA 1000/1500 and EVA 3000/5000, with old firmware */
		.vendor        = "(COMPAQ|HP)",
		.product       = "(MSA|HSV)1.0",
		.features      = "1 queue_if_no_path",
		.hwhandler     = "1 hp_sw",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = 12,
		.minio         = 100,
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
		.minio         = 100,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* EVA 3000/5000 with new firmware, EVA 4000/6000/8000 */
		.vendor        = "(COMPAQ|HP)",
		.product       = "(HSV1[01]1|HSV2[01]0|HSV3[046]0|HSV4[05]0)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 12,
		.minio         = 100,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* MSA2000 family with old firmware */
		.vendor        = "HP",
		.product       = "(MSA2[02]12fc|MSA2012i)",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = 18,
		.minio         = 100,
	},
	{
		/* MSA2000 family with new firmware */
		.vendor        = "HP",
		.product       = "(MSA2012sa|MSA23(12|24)(fc|i|sa)|MSA2000s VOLUME)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 18,
		.minio         = 100,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* MSA 1040/2040 family */
		.vendor        = "HP",
		.product       = "MSA [12]040 SA[NS]",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 18,
		.minio         = 100,
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
		.minio         = 100,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* Smart Array */
		.vendor        = "HP",
		.product       = "LOGICAL VOLUME",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = 12,
	},
	{
		/* P2000 family */
		.vendor        = "HP",
		.product       = "(P2000 G3 FC|P2000G3 FC/iSCSI|P2000 G3 SAS|P2000 G3 iSCSI)",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 18,
		.minio         = 100,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * DataDirect Networks
	 */
	{
		.vendor        = "DDN",
		.product       = "SAN DataDirector",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	/*
	 * EMC
	 *
	 * Maintainer : Edward Goggin
	 * Mail : egoggin@emc.com
	 */
	{
		/* Symmetrix / DMX / VMAX */
		.vendor        = "EMC",
		.product       = "SYMMETRIX",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = 6,
	},
	{
		/* DGC CLARiiON CX/AX and EMC VNX */
		.vendor        = "^DGC",
		.product       = "^(RAID|DISK|VRAID)",
		.bl_product    = "LUNZ",
		.features      = "1 queue_if_no_path",
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
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = 5,
	},
	{
		.vendor        = "XtremIO",
		.product       = "XtremApp",
		.selector      = "queue-length 0",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	/*
	 * DELL
	 */
	{
		.vendor        = "COMPELNT",
		.product       = "Compellent Vol",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
	},
	{
		.vendor        = "DELL",
		.product       = "MD3000",
		.bl_product    = "Universal Xport",
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 15,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		.vendor        = "DELL",
		.product       = "(MD32xx|MD36xx)",
		.bl_product    = "Universal Xport",
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 15,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		.vendor        = "DELL",
		.product       = "(MD34xx|MD38xx)",
		.bl_product    = "Universal Xport",
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 15,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	/*
	 * Fujitsu
	 */
	{
		.vendor        = "FSC",
		.product       = "CentricStor",
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		.vendor        = "FUJITSU",
		.product       = "ETERNUS_DX(H|L|M|400|8000)",
		.features      = "1 queue_if_no_path",
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
		.pgfailback    = FAILBACK_UNDEF,
	},
	/*
	 * Hitachi
	 *
	 * Maintainer : Matthias Rudolph
	 * Mail : matthias.rudolph@hds.com
	 */
	{
		/* USP-V, HUS VM, VSP, VSP G1000 and VSP GX00 families */
		.vendor        = "(HITACHI|HP)",
		.product       = "OPEN-",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		/* AMS 2000 and HUS 100 families */
		.vendor        = "HITACHI",
		.product       = "DF",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_HDS,
	},
	/*
	 * IBM
	 *
	 * Maintainer : Hannes Reinecke
	 * Mail : hare@suse.de
	 */
	{
		.vendor        = "IBM",
		.product       = "ProFibre 4000R",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		/* DS4300 / FAStT600 */
		.vendor        = "IBM",
		.product       = "^1722-600",
		.bl_product    = "Universal Xport",
		.features      = "1 queue_if_no_path",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 300,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS4100 / FAStT100 */
		.vendor        = "IBM",
		.product       = "^1724",
		.bl_product    = "Universal Xport",
		.features      = "1 queue_if_no_path",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 300,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS3200 / DS3300 / DS3400 / Boot DS */
		.vendor        = "IBM",
		.product       = "^1726",
		.bl_product    = "Universal Xport",
		.features      = "1 queue_if_no_path",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 300,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS4400 / DS4500 / FAStT700 / FAStT900 */
		.vendor        = "IBM",
		.product       = "^1742",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS3500 / DS3512 / DS3524 */
		.vendor        = "IBM",
		.product       = "^(1745|1746)",
		.bl_product    = "Universal Xport",
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 15,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS3950 / DS4200 / DS4700 / DS5020 */
		.vendor        = "IBM",
		.product       = "^1814",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS4800 */
		.vendor        = "IBM",
		.product       = "^1815",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS5000 / DS5100 / DS5300 / DCS3700 */
		.vendor        = "IBM",
		.product       = "^1818",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* Netfinity Fibre Channel RAID Controller Unit */
		.vendor        = "IBM",
		.product       = "^3526",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* DS4200 / FAStT200 */
		.vendor        = "IBM",
		.product       = "^3542",
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		/* Enterprise Storage Server / Shark family */
		.vendor        = "IBM",
		.product       = "^2105",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		/* DS6000 */
		.vendor        = "IBM",
		.product       = "^1750500",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* DS8000 */
		.vendor        = "IBM",
		.product       = "^2107900",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		/* Storwize family / SAN Volume Controller / Flex System V7000 */
		.vendor        = "IBM",
		.product       = "^2145",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	{
		.vendor        = "IBM",
		.product       = "S/390 DASD ECKD",
		.bl_product    = "S/390",
		.uid_attribute = "ID_UID",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		.vendor        = "IBM",
		.product       = "S/390 DASD FBA",
		.bl_product    = "S/390",
		.uid_attribute = "ID_UID",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		/* Power RAID */
		.vendor        = "IBM",
		.product       = "^IPR",
		.features      = "1 queue_if_no_path",
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
		.minio         = 100,
		.prio_name     = PRIO_ALUA,
	},
	{
		/* XIV Storage System */
		.vendor        = "IBM",
		.product       = "2810XIV",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = 15,
		.minio         = 15,
	},
		/*
		 * IBM Power Virtual SCSI Devices
		 *
		 * Maintainer : Brian King
		 * Mail : brking@linux.vnet.ibm.com
		 */
	{
		.vendor        = "AIX",
		.product       = "VDASD",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = (300 / DEFAULT_CHECKINT),
	},
	{
		/* 3303 NVDISK */
		.vendor        = "IBM",
		.product       = "3303[ ]+NVDISK",
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = (300 / DEFAULT_CHECKINT),
	},
	{
		.vendor        = "AIX",
		.product       = "NVDISK",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = (300 / DEFAULT_CHECKINT),
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * NetApp
	 */
	{
		/*
		 * ONTAP family
		 *
		 * Maintainer : Martin George
		 * Mail : marting@netapp.com
		 */
		.vendor        = "NETAPP",
		.product       = "LUN",
		.features      = "3 queue_if_no_path pg_init_retries 50",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.flush_on_last_del = FLUSH_ENABLED,
		.minio         = 128,
		.dev_loss      = MAX_DEV_LOSS_TMO,
		.prio_name     = PRIO_ONTAP,
	},
	{
		/*
		 * SANtricity(RDAC) family
		 *
		 * Maintainer : Sean Stewart
		 * Mail : sean.stewart@netapp.com
		 */
		.vendor        = "(NETAPP|LSI|ENGENIO)",
		.product       = "INF-01-00",
		.bl_product    = "Universal Xport",
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 30,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	/*
	 * Nexenta
	 *
	 * Maintainer : Yacine Kheddache
	 * Mail : yacine@alyseo.com
	 */
	{
		.vendor        = "NEXENTA",
		.product       = "COMSTAR",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
		.no_path_retry = 30,
		.minio         = 128,
	},
	/*
	 * SGI
	 */
	{
		.vendor        = "SGI",
		.product       = "TP9[13]00",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		.vendor        = "SGI",
		.product       = "TP9[45]00",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* InfiniteStorage ??? */
		.vendor        = "SGI",
		.product       = "IS",
		.bl_product    = "Universal Xport",
		.features      = "2 pg_init_retries 50",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 15,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
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
	{
		/*
		 * Pillar Data
		 *
		 * Maintainer : Srinivasan Ramani
		 * Mail : srinivas.ramani@oracle.com
		 */
		.vendor        = "Pillar",
		.product       = "Axiom",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.prio_name     = PRIO_ALUA,
	},
	{
		.vendor        = "STK",
		.product       = "OPENstorage D280",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		.vendor        = "STK",
		.product       = "FLEXLINE 380",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		.vendor        = "SUN",
		.product       = "(StorEdge 3510|T4)",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	{
		.vendor        = "SUN",
		.product       = "STK6580_6780",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* 6140 */
		.vendor        = "SUN",
		.product       = "CSM200_R",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		/* 2510 / 2530 / 2540 */
		.vendor        = "SUN",
		.product       = "LCSM100_[IEFS]",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	{
		.vendor        = "SUN",
		.product       = "SUN_6180",
		.bl_product    = "Universal Xport",
		.hwhandler     = "1 rdac",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.checker_name  = RDAC,
		.prio_name     = PRIO_RDAC,
	},
	/*
	 * Pivot3
	 *
	 * Maintainer : Bart Brooks
	 * Mail : bartb@pivot3.com
	 */
	{
		.vendor        = "PIVOT3",
		.product       = "RAIGE VOLUME",
		.features      = "1 queue_if_no_path",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.minio         = 100,
	},
	/*
	 * Intel
	 */
	{
		.vendor	       = "(Intel|INTEL)",
		.product       = "Multi-Flex",
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
		.vendor	       = "(LIO-ORG|SUSE)",
		.product       = "RBD",
		.hwhandler     = "1 alua",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = 12,
		.minio         = 100,
		.prio_name     = PRIO_ALUA,
	},
	/*
	 * DataCore
	 */
	{
		.vendor	       = "DataCore",
		.product       = "SANmelody",
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.no_path_retry = NO_PATH_RETRY_QUEUE,
		.prio_name     = PRIO_ALUA,
	},
	{
		.vendor	       = "DataCore",
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
		.vendor        = "PURE",
		.product       = "FlashArray",
		.selector      = "queue-length 0",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.fast_io_fail  = 10,
		.dev_loss      = 60,
	},
	/*
	 * Huawei
	 */
	{
		/* OceanStor V3 */
		.vendor        = "(HUAWEI|HUASY)",
		.product       = "XSG1",
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
	},
	/*
	 * Red Hat
	 *
	 * Maintainer: Mike Christie
	 * Mail: mchristi@redhat.com
	 */
	{
		.vendor        = "Ceph",
		.product       = "RBD",
		.no_path_retry = NO_PATH_RETRY_FAIL,
		.checker_name  = RBD,
		.deferred_remove = DEFERRED_REMOVE_ON,
	},
#if 0
	/*
	 * Copy this TEMPLATE to add new hardware.
	 *
	 * Keep only mandatory and modified attributes.
	 * Attributes with default values must be removed.
	 * .vendor and .product are mandatory, all other are optional.
	 * POSIX Extended regex can be used in .vendor, .product, .revision and .bl_product
	 *
	 * COMPANY_NAME
	 *
	 * Maintainer : XXX
	 * Mail : XXX
	 */
	{
		/* If product-ID is different from marketing name add a comment */
		.vendor        = "VENDOR",
		.product       = "PRODUCT",
		.revision      = "REVISION",
		.bl_product    = "BL_PRODUCT",
		.pgpolicy      = FAILOVER,
		.uid_attribute = "ID_SERIAL",
		.selector      = "service-time 0",
		.checker_name  = TUR,
		.features      = "0",
		.hwhandler     = "0",
		.prio_name     = "const",
		.prio_args     = "",
		.pgfailback    = -FAILBACK_MANUAL,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = 1000,
		.minio_rq      = 1,
		.flush_on_last_del = FLUSH_DISABLED,
		.fast_io_fail  = 5,
		.dev_loss      = 600,
		.retain_hwhandler = RETAIN_HWHANDLER_ON,
		.detect_prio   = DETECT_PRIO_ON,
		.deferred_remove = DEFERRED_REMOVE_OFF,
		.delay_watch_checks = DELAY_CHECKS_OFF,
		.delay_wait_checks = DELAY_CHECKS_OFF,
	},
#endif
	/*
	 * EOL
	 */
	{
		.vendor        = NULL,
		.product       = NULL,
	},
};

extern int
setup_default_hwtable (vector hw)
{
	int r = 0;
	struct hwentry * hwe = default_hw;

	while (hwe->vendor) {
		r += store_hwe(hw, hwe);
		hwe++;
	}
	return r;
}
