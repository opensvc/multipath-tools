#include <stdio.h>

#include <checkers.h>

#include "vector.h"
#include "defaults.h"
#include "structs.h"
#include "config.h"
#include "pgpolicies.h"

/*
 * Tuning suggestions on these parameters should go to
 * dm-devel@redhat.com
 * 
 * You are welcome to claim maintainership over a controller
 * family. Please mail the currently enlisted maintainer and
 * the upstream package maintainer.
 */
static struct hwentry default_hw[] = {
	/*
	 * Apple controller family
	 *
	 * Maintainer : Shyam Sundar
	 * Mail : g.shyamsundar@yahoo.co.in
	 */
	{
		.vendor        = "APPLE*",
		.product       = "Xserve RAID ",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = DEFAULT_CHECKER,
	},
	/*
	 * StorageWorks controller family
	 *
	 * Maintainer : Christophe Varoqui
	 * Mail : christophe.varoqui@free.fr
	 */
	{
		.vendor        = "3PARdata",
		.product       = "VV",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = DEFAULT_CHECKER,
	},
	{
		.vendor        = "DEC",
		.product       = "HSG80",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = "1 hp_sw",
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = HP_SW,
	},
	{
		.vendor        = "(COMPAQ|HP)",
		.product       = "(MSA|HSV)1.*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = "1 hp_sw",
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = HP_SW,
	},
	{
		.vendor        = "HP",
		.product       = "A6189A",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	{
		.vendor        = "HP",
		.product       = "HSV20.*",
		.revision      = "[123].*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = HP_SW,
	},
	{
		.vendor        = "HP",
		.product       = "HSV20.*",
		.revision      = "[^123].*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "/sbin/mpath_prio_alua /dev/%n",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		.vendor        = "HP",
		.product       = "HSV21.*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "/sbin/mpath_prio_alua /dev/%n",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	/*
	 * DDN controller family
	 *
	 * Maintainer : Christophe Varoqui
	 * Mail : christophe.varoqui@free.fr
	 */
	{
		.vendor        = "DDN",
		.product       = "SAN DataDirector",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	/*
	 * EMC / Clariion controller family
	 *
	 * Maintainer : Edward Goggin, EMC
	 * Mail : egoggin@emc.com
	 */
	{
		.vendor        = "EMC",
		.product       = "SYMMETRIX",
		.getuid        = "scsi_id -g -u -ppre-spc3-83 -s /block/%n",
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	{
		.vendor        = "DGC",
		.product       = ".*",
		.bl_product    = "LUNZ",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_emc /dev/%n",
		.features      = "1 queue_if_no_path",
		.hwhandler     = "1 emc",
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = (300 / DEFAULT_CHECKINT),
		.minio         = DEFAULT_MINIO,
		.checker_name  = EMC_CLARIION,
	},
	/*
	 * Fujitsu controller family
	 *
	 * Maintainer : Christophe Varoqui
	 * Mail : christophe.varoqui@free.fr
	 */
	{
		.vendor        = "FSC",
		.product       = "CentricStor",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	/*
	 * Hitachi controller family
	 *
	 * Maintainer : Matthias Rudolph
	 * Mail : matthias.rudolph@hds.com
	 */
	{
		.vendor        = "(HITACHI|HP)",
		.product       = "OPEN-.*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	{
		.vendor        = "HITACHI",
		.product       = "DF.*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_hds_modular %d",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	/*
	 * IBM controller family
	 *
	 * Maintainer : Hannes Reinecke, SuSE
	 * Mail : hare@suse.de
	 */
	{
		.vendor        = "IBM",
		.product       = "ProFibre 4000R",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	{
		/* IBM DS4100 / FAStT100 */
		.vendor        = "IBM",
		.product       = "1742",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_tpc /dev/%n",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		/* IBM Netfinity Fibre Channel RAID Controller Unit */
		.vendor        = "IBM",
		.product       = "3526",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_tpc /dev/%n",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		/* IBM DS4200 / FAStT200 */
		.vendor        = "IBM",
		.product       = "3542",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		/* IBM ESS F20 aka Shark */
		.vendor        = "IBM",
		.product       = "2105(800|F20)",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = "1 queue_if_no_path",
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		/* IBM DS6000 */
		.vendor        = "IBM",
		.product       = "1750500",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_alua /dev/%n",
		.features      = "1 queue_if_no_path",
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		/* IBM DS8000 */
		.vendor        = "IBM",
		.product       = "2107900",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = "1 queue_if_no_path",
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_SERIAL,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		/* IBM SAN Volume Controller */
		.vendor        = "IBM",
		.product       = "2145",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_alua /dev/%n",
		.features      = "1 queue_if_no_path",
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	{
		/* IBM S/390 ECKD DASD */
		.vendor        = "IBM",
		.product       = "S/390 DASD ECKD",
		.bl_product       = "S/390.*",
		.getuid        = "dasd_id /dev/%n",
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = DIRECTIO,
	},
 	/*
	 * NETAPP controller family
	 *
	 * Maintainer : Dave Wysochanski
	 * Mail : davidw@netapp.com
	 */
	{
		.vendor        = "NETAPP",
		.product       = "LUN.*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_netapp /dev/%n",
		.features      = "1 queue_if_no_path",
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = 128,
		.checker_name  = READSECTOR0,
	},
 	/*
	 * IBM NSeries (NETAPP) controller family
	 *
	 * Maintainer : Dave Wysochanski
	 * Mail : davidw@netapp.com
	 */
	{
		.vendor        = "IBM",
		.product       = "Nseries.*",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_netapp /dev/%n",
		.features      = "1 queue_if_no_path",
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = 128,
		.checker_name  = READSECTOR0,
	},
	/*
	 * Pillar Data controller family
	 *
	 * Maintainer : Christophe Varoqui
	 * Mail : christophe.varoqui@free.fr
	 */
	{
		.vendor        = "Pillar",
		.product       = "Axiom 500",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_alua %d",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	/*
	 * SGI arrays
	 *
	 * Maintainer : Christophe Varoqui
	 * Mail : christophe.varoqui@free.fr
	 */
	{
		.vendor        = "SGI",
		.product       = "TP9[13]00",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	{
		.vendor        = "SGI",
		.product       = "TP9[45]00",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_tpc /dev/%n",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	/*
	 * STK arrays
	 *
	 * Maintainer : Christophe Varoqui
	 * Mail : christophe.varoqui@free.fr
	 */
	{
		.vendor        = "STK",
		.product       = "OPENstorage D280",
		.getuid        = DEFAULT_GETUID,
		.getprio       = "mpath_prio_tpc /dev/%n",
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = GROUP_BY_PRIO,
		.pgfailback    = -FAILBACK_IMMEDIATE,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = TUR,
	},
	/*
	 * SUN arrays
	 *
	 * Maintainer : Christophe Varoqui
	 * Mail : christophe.varoqui@free.fr
	 */
	{
		.vendor        = "SUN",
		.product       = "(StorEdge 3510|T4)",
		.getuid        = DEFAULT_GETUID,
		.getprio       = NULL,
		.features      = DEFAULT_FEATURES,
		.hwhandler     = DEFAULT_HWHANDLER,
		.selector      = DEFAULT_SELECTOR,
		.pgpolicy      = MULTIBUS,
		.pgfailback    = FAILBACK_UNDEF,
		.rr_weight     = RR_WEIGHT_NONE,
		.no_path_retry = NO_PATH_RETRY_UNDEF,
		.minio         = DEFAULT_MINIO,
		.checker_name  = READSECTOR0,
	},
	/*
	 * EOL
	 */
	{
		.vendor        = NULL,
		.product       = NULL,
		.getuid        = NULL,
		.getprio       = NULL,
		.features      = NULL,
		.hwhandler     = NULL,
		.selector      = NULL,
		.pgpolicy      = 0,
		.pgfailback    = 0,
		.rr_weight     = 0,
		.no_path_retry = 0,
		.minio         = 0,
		.checker_name  = NULL,
	},
};

extern int
setup_default_hwtable (vector hw)
{
	int r = 0;
	struct hwentry * hwe = default_hw;

	while (hwe->vendor) {
		hwe->checker = checker_lookup(hwe->checker_name);
		r += store_hwe(hw, hwe);
		hwe++;
	}
	return r;
}
