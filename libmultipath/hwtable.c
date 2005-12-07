#include <stdio.h>

#include "vector.h"
#include "defaults.h"
#include "structs.h"
#include "config.h"
#include "pgpolicies.h"

extern int
setup_default_hwtable (vector hw)
{
	int r = 0;

	r += store_hwe(hw, "3PARdata", "VV", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "COMPAQ", "HSV110 (C)COMPAQ", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "COMPAQ", "MSA1000", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "COMPAQ", "MSA1000 VOLUME", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "DDN", "SAN DataDirector", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "DEC", "HSG80", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "EMC", "SYMMETRIX", MULTIBUS,
		       "/sbin/scsi_id -g -u -ppre-spc3-83 -s /block/%n");
	r += store_hwe(hw, "FSC", "CentricStor", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "HITACHI", "DF400", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HITACHI", "DF500", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HITACHI", "DF600", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HP", "HSV110", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "HP", "HSV210", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HP", "A6189A", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HP", "OPEN-", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "IBM", "ProFibre 4000R", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "SGI", "TP9100", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "SGI", "TP9300", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "STK", "OPENstorage D280", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "SUN", "StorEdge 3510", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "SUN", "T4", MULTIBUS, DEFAULT_GETUID);

	r += store_hwe_ext(hw, "DGC", "^[^LUN_Z]", GROUP_BY_PRIO,
		   DEFAULT_GETUID, "/sbin/mpath_prio_emc /dev/%n", "1 emc",
		   "1 queue_if_no_path", "emc_clariion", -FAILBACK_IMMEDIATE);
	r += store_hwe_ext(hw, "IBM", "3542", GROUP_BY_SERIAL, DEFAULT_GETUID,
		   NULL, "0", "0", "tur", FAILBACK_UNDEF);
	/* IBM ESS F20 aka Shark */
	r += store_hwe_ext(hw, "IBM", "2105F20", GROUP_BY_SERIAL,
		   DEFAULT_GETUID, NULL, "0", "1 queue_if_no_path",
		   "tur", FAILBACK_UNDEF);
       /* IBM DS6000 */
	r += store_hwe_ext(hw, "IBM", "1750500", GROUP_BY_PRIO, DEFAULT_GETUID,
		   "/sbin/mpath_prio_alua /dev/%n", "0", "1 queue_if_no_path",
		   "tur", FAILBACK_UNDEF);
	/* IBM DS8000 */
	r += store_hwe_ext(hw, "IBM", "2107900", GROUP_BY_SERIAL, DEFAULT_GETUID,
		   NULL, "0", "1 queue_if_no_path", "tur", FAILBACK_UNDEF);
	/* IBM SAN Volume Controller */
	r += store_hwe_ext(hw, "IBM", "2145", MULTIBUS, DEFAULT_GETUID,
		   NULL, "0", "1 queue_if_no_path", "tur", FAILBACK_UNDEF);
	/* IBM S/390 ECKD DASD */
	r += store_hwe_ext(hw, "IBM", "S/390 DASD ECKD", MULTIBUS,
		   "/sbin/dasdview -j /dev/%n", NULL, "0", "0",
		   "directio", FAILBACK_UNDEF);
	r += store_hwe_ext(hw, "NETAPP", "LUN", GROUP_BY_PRIO, DEFAULT_GETUID,
		  "/sbin/mpath_prio_netapp /dev/%n", NULL,
		  "1 queue_if_no_path", "readsector0", FAILBACK_UNDEF);
	r += store_hwe_ext(hw, "Pillar", "Axiom 500", GROUP_BY_PRIO,
		   DEFAULT_GETUID, "/sbin/mpath_prio_alua %d", "0", "0",
		   "tur", FAILBACK_UNDEF);
	r += store_hwe_ext(hw, "SGI", "TP9400", GROUP_BY_PRIO, DEFAULT_GETUID,
		   "/sbin/mpath_prio_tpc /dev/%n", "0", "0", "tur", FAILBACK_UNDEF);
	r += store_hwe_ext(hw, "SGI", "TP9500", GROUP_BY_PRIO, DEFAULT_GETUID,
		   "/sbin/mpath_prio_tpc /dev/%n", "0", "0", "tur", FAILBACK_UNDEF);

	return r;
}

