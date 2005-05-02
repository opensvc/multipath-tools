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
	r += store_hwe(hw, "COMPAQ", "HSV110 (C)COMPAQ", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "COMPAQ", "MSA1000", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "COMPAQ", "MSA1000 VOLUME", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "DDN", "SAN DataDirector", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "DEC", "HSG80", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "EMC", "SYMMETRIX", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "FSC", "CentricStor", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HITACHI", "DF400", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HITACHI", "DF500", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HITACHI", "DF600", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HP", "HSV110", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HP", "A6189A", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "HP", "OPEN-", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "IBM", "ProFibre 4000R", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "NETAPP", "LUN", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "SGI", "TP9100", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "SGI", "TP9300", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "STK", "OPENstorage D280", GROUP_BY_SERIAL, DEFAULT_GETUID);
	r += store_hwe(hw, "SUN", "StorEdge 3510", MULTIBUS, DEFAULT_GETUID);
	r += store_hwe(hw, "SUN", "T4", MULTIBUS, DEFAULT_GETUID);

	r += store_hwe_ext(hw, "DGC", "*", GROUP_BY_PRIO, DEFAULT_GETUID,
		   "/sbin/pp_emc /dev/%n", "1 emc", "1 queue_if_no_path",
		   "emc_clariion");
	r += store_hwe_ext(hw, "IBM", "3542", GROUP_BY_SERIAL, DEFAULT_GETUID,
		   NULL, "0", "0", "tur");
	r += store_hwe_ext(hw, "SGI", "TP9400", MULTIBUS, DEFAULT_GETUID,
		   NULL, "0", "0", "tur");
	r += store_hwe_ext(hw, "SGI", "TP9500", FAILOVER, DEFAULT_GETUID,
		   NULL, "0", "0", "tur");

	return r;
}

