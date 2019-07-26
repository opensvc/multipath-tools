#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include "checkers.h"
#include "vector.h"
#include "config.h"
#include "structs.h"
#include <getopt.h>
#include <libudev.h>
#include "mpath_persist.h"
#include "main.h"
#include "debug.h"
#include <pthread.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "version.h"

static const char * pr_type_strs[] = {
	"obsolete [0]",
	"Write Exclusive",
	"obsolete [2]",
	"Exclusive Access",
	"obsolete [4]",
	"Write Exclusive, registrants only",
	"Exclusive Access, registrants only",
	"Write Exclusive, all registrants",
	"Exclusive Access, all registrants",
	"obsolete [9]", "obsolete [0xa]", "obsolete [0xb]", "obsolete [0xc]",
	"obsolete [0xd]", "obsolete [0xe]", "obsolete [0xf]",
};

int get_transportids_length(unsigned char * transportid_arr, int max_transportid, int num_transportids);
void mpath_print_buf_readcap(struct prin_resp *pr_buff);
void mpath_print_buf_readfullstat(struct prin_resp *pr_buff);
void mpath_print_buf_readresv(struct prin_resp *pr_buff);
void mpath_print_buf_readkeys(struct prin_resp *pr_buff);
void dumpHex(const char* str, int len, int no_ascii);
void * mpath_alloc_prin_response(int prin_sa);
void mpath_print_transport_id(struct prin_fulldescr *fdesc);
int construct_transportid(const char * inp, struct transportid transid[], int num_transportids);

int logsink;
struct config *multipath_conf;

struct config *get_multipath_config(void)
{
	return multipath_conf;
}

void put_multipath_config(void * arg)
{
	/* Noop for now */
}

void rcu_register_thread_memb(void) {}

void rcu_unregister_thread_memb(void) {}

struct udev *udev;

static int verbose, loglevel, noisy;

static int handle_args(int argc, char * argv[], int line);

static int do_batch_file(const char *batch_fn)
{
	char command[] = "mpathpersist";
	const int ARGV_CHUNK = 2;
	const char delims[] = " \t\n";
	size_t len = 0;
	char *line = NULL;
	ssize_t n;
	int nline = 0;
	int argl = ARGV_CHUNK;
	FILE *fl;
	char **argv = calloc(argl, sizeof(*argv));
	int ret = MPATH_PR_SUCCESS;

	if (argv == NULL)
		return MPATH_PR_OTHER;

	fl = fopen(batch_fn, "r");
	if (fl == NULL) {
		fprintf(stderr, "unable to open %s: %s\n",
			batch_fn, strerror(errno));
		free(argv);
		return MPATH_PR_SYNTAX_ERROR;
	} else {
		if (verbose >= 2)
			fprintf(stderr, "running batch file %s\n",
				batch_fn);
	}

	while ((n = getline(&line, &len, fl)) != -1) {
		char *_token, *token;
		int argc = 0;
		int rv;

		nline++;
		argv[argc++] = command;

		if (line[n-1] == '\n')
			line[n-1] = '\0';
		if (verbose >= 3)
			fprintf(stderr, "processing line %d: %s\n",
				nline, line);

		for (token = strtok_r(line, delims, &_token);
		     token != NULL && *token != '#';
		     token = strtok_r(NULL, delims, &_token)) {

			if (argc >= argl) {
				int argn = argl + ARGV_CHUNK;
				char **tmp;

				tmp = realloc(argv, argn * sizeof(*argv));
				if (tmp == NULL)
					break;
				argv = tmp;
				argl = argn;
			}

			if (argc == 1 && !strcmp(token, command))
				continue;

			argv[argc++] = token;
		}

		if (argc <= 1)
			continue;

		if (verbose >= 2) {
			int i;

			fprintf(stderr, "## file %s line %d:", batch_fn, nline);
			for (i = 0; i < argc; i++)
				fprintf(stderr, " %s", argv[i]);
			fprintf(stderr, "\n");
		}

		optind = 0;
		rv = handle_args(argc, argv, nline);
		if (rv != MPATH_PR_SUCCESS)
			ret = rv;
	}

	fclose(fl);
	free(argv);
	free(line);
	return ret;
}

static int handle_args(int argc, char * argv[], int nline)
{
	int c;
	int fd = -1;
	const char *device_name = NULL;
	int num_prin_sa = 0;
	int num_prout_sa = 0;
	int num_prout_param = 0;
	int prin_flag = 0;
	int prout_flag = 0;
	int ret = 0;
	int hex = 0;
	uint64_t param_sark = 0;
	unsigned int prout_type = 0;
	int param_alltgpt = 0;
	int param_aptpl = 0;
	uint64_t param_rk = 0;
	unsigned int param_rtp = 0;
	int num_transportids = 0;
	struct transportid transportids[MPATH_MX_TIDS];
	int prout = 1;
	int prin = 1;
	int prin_sa = -1;
	int prout_sa = -1;
	int num_transport =0;
	char *batch_fn = NULL;
	void *resp = NULL;
	struct transportid * tmp;

	memset(transportids, 0, MPATH_MX_TIDS * sizeof(struct transportid));

	while (1)
	{
		int option_index = 0;

		c = getopt_long (argc, argv, "v:Cd:hHioYZK:S:PAT:skrGILcRX:l:f:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c)
		{
			case 'f':
				if (nline != 0) {
					fprintf(stderr,
						"ERROR: -f option not allowed in batch file\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}
				if (batch_fn != NULL) {
					fprintf(stderr,
						"ERROR: -f option can be used at most once\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}
				batch_fn = strdup(optarg);
				break;
			case 'v':
				if (nline == 0 && 1 != sscanf (optarg, "%d", &loglevel))
				{
					fprintf (stderr, "bad argument to '--verbose'\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}
				break;

			case 'C':
				prout_sa = MPATH_PROUT_CLEAR_SA;
				++num_prout_sa;
				break;

			case 'd':
				device_name = optarg;
				break;

			case 'h':
				usage ();
				free(batch_fn);
				return 0;

			case 'H':
				hex=1;
				break;

			case 'i':
				prin_flag = 1;
				break;

			case 'o':
				prout_flag = 1;
				break;

			case 'Y':
				param_alltgpt = 1;
				++num_prout_param;
				break;
			case 'Z':
				param_aptpl = 1;
				++num_prout_param;
				break;
			case 'K':
				if (1 != sscanf (optarg, "%" SCNx64 "", &param_rk))
				{
					fprintf (stderr, "bad argument to '--param-rk'\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}
				++num_prout_param;
				break;

			case 'S':
				if (1 != sscanf (optarg, "%" SCNx64 "", &param_sark))
				{
					fprintf (stderr, "bad argument to '--param-sark'\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}
				++num_prout_param;
				break;

			case 'P':
				prout_sa = MPATH_PROUT_PREE_SA;
				++num_prout_sa;
				break;

			case 'A':
				prout_sa = MPATH_PROUT_PREE_AB_SA;
				++num_prout_sa;
				break;

			case 'T':
				if (1 != sscanf (optarg, "%x", &prout_type))
				{
					fprintf (stderr, "bad argument to '--prout-type'\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}
				++num_prout_param;
				break;

			case 's':
				prin_sa = MPATH_PRIN_RFSTAT_SA;
				++num_prin_sa;
				break;

			case 'k':
				prin_sa = MPATH_PRIN_RKEY_SA;
				++num_prin_sa;
				break;

			case 'r':
				prin_sa = MPATH_PRIN_RRES_SA;
				++num_prin_sa;
				break;

			case 'G':
				prout_sa = MPATH_PROUT_REG_SA;
				++num_prout_sa;
				break;

			case 'I':
				prout_sa = MPATH_PROUT_REG_IGN_SA;
				++num_prout_sa;
				break;

			case 'L':
				prout_sa = MPATH_PROUT_REL_SA;
				++num_prout_sa;
				break;

			case 'c':
				prin_sa = MPATH_PRIN_RCAP_SA;
				++num_prin_sa;
				break;

			case 'R':
				prout_sa = MPATH_PROUT_RES_SA;
				++num_prout_sa;
				break;

			case 'X':
				if (0 != construct_transportid(optarg, transportids, num_transport)) {
					fprintf(stderr, "bad argument to '--transport-id'\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}

				++num_transport;
				break;

			case 'l':
				if (1 != sscanf(optarg, "%u", &mpath_mx_alloc_len)) {
					fprintf(stderr, "bad argument to '--alloc-length'\n");
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				} else if (MPATH_MAX_PARAM_LEN < mpath_mx_alloc_len) {
					fprintf(stderr, "'--alloc-length' argument exceeds maximum"
							" limit(%d)\n", MPATH_MAX_PARAM_LEN);
					ret = MPATH_PR_SYNTAX_ERROR;
					goto out;
				}
				break;

			default:
				fprintf(stderr, "unrecognised switch " "code 0x%x ??\n", c);
				ret = MPATH_PR_SYNTAX_ERROR;
				goto out;
		}
	}

	if (optind < argc)
	{

		if (NULL == device_name)
		{
			device_name = argv[optind];
			++optind;
		}
		if (optind < argc)
		{
			for (; optind < argc; ++optind)
				fprintf (stderr, "Unexpected extra argument: %s\n", argv[optind]);
			ret = MPATH_PR_SYNTAX_ERROR;
			goto out;
		}
	}

	if (nline == 0) {
		/* set verbosity */
		noisy = (loglevel >= 3) ? 1 : hex;
		verbose	= (loglevel >= 3)? 3: loglevel;
		ret = mpath_persistent_reserve_init_vecs(verbose);
		if (ret != MPATH_PR_SUCCESS)
			goto out;
	}

	if ((prout_flag + prin_flag) == 0 && batch_fn == NULL)
	{
		fprintf (stderr, "choose either '--in' or '--out' \n");
		ret = MPATH_PR_SYNTAX_ERROR;
		goto out;
	}
	if ((prout_flag + prin_flag) > 1)
	{
		fprintf (stderr, "choose either '--in' or '--out' \n");
		ret = MPATH_PR_SYNTAX_ERROR;
		goto out;
	}
	else if (prout_flag)
	{				/* syntax check on PROUT arguments */
		prin = 0;
		if ((1 != num_prout_sa) || (0 != num_prin_sa))
		{
			fprintf (stderr, " For Persistent Reserve Out only one "
					"appropriate\n service action must be "
					"chosen \n");
			ret = MPATH_PR_SYNTAX_ERROR;
			goto out;
		}
	}
	else if (prin_flag)
	{				/* syntax check on PRIN arguments */
		prout = 0;
		if (num_prout_sa > 0)
		{
			fprintf (stderr, " When a service action for Persistent "
					"Reserve Out is chosen the\n"
					" '--out' option must be given \n");
			ret = MPATH_PR_SYNTAX_ERROR;
			goto out;
		}
		if (0 == num_prin_sa)
		{
			fprintf (stderr,
					" No service action given for Persistent Reserve IN\n");
			ret = MPATH_PR_SYNTAX_ERROR;
			goto out;
		}
		else if (num_prin_sa > 1)
		{
			fprintf (stderr, " Too many service actions given; choose "
					"one only\n");
			ret = MPATH_PR_SYNTAX_ERROR;
			goto out;
		}
	}
	else
	{
		if (batch_fn == NULL)
			ret = MPATH_PR_SYNTAX_ERROR;
		goto out;
	}

	if ((param_rtp) && (MPATH_PROUT_REG_MOV_SA != prout_sa))
	{
		fprintf (stderr, " --relative-target-port"
				" only useful with --register-move\n");
		ret = MPATH_PR_SYNTAX_ERROR;
		goto out;
	}

	if (((MPATH_PROUT_RES_SA == prout_sa) ||
				(MPATH_PROUT_REL_SA == prout_sa) ||
				(MPATH_PROUT_PREE_SA == prout_sa) ||
				(MPATH_PROUT_PREE_AB_SA == prout_sa)) &&
			(0 == prout_type)) {
		fprintf(stderr, "Warning: --prout-type probably needs to be "
				"given\n");
	}
	if ((verbose > 2) && num_transportids)
	{
		fprintf (stderr, "number of tranport-ids decoded from "
				"command line : %d\n", num_transportids);
	}

	if (device_name == NULL)
	{
		fprintf (stderr, "No device name given \n");
		ret = MPATH_PR_SYNTAX_ERROR;
		goto out;
	}

	/* open device */
	if ((fd = open (device_name, O_RDONLY)) < 0)
	{
		fprintf (stderr, "%s: error opening file (rw) fd=%d\n",
				device_name, fd);
		ret = MPATH_PR_FILE_ERROR;
		goto out;
	}


	if (prin)
	{
		resp = mpath_alloc_prin_response(prin_sa);
		if (!resp)
		{
			fprintf (stderr, "failed to allocate PRIN response buffer\n");
			ret = MPATH_PR_OTHER;
			goto out_fd;
		}

		ret = __mpath_persistent_reserve_in (fd, prin_sa, resp, noisy);
		if (ret != MPATH_PR_SUCCESS )
		{
			fprintf (stderr, "Persistent Reserve IN command failed\n");
			goto out_fd;
		}

		switch(prin_sa)
		{
			case MPATH_PRIN_RKEY_SA:
				mpath_print_buf_readkeys(resp);
				break;
			case MPATH_PRIN_RRES_SA:
				mpath_print_buf_readresv(resp);
				break;
			case MPATH_PRIN_RCAP_SA:
				mpath_print_buf_readcap(resp);
				break;
			case MPATH_PRIN_RFSTAT_SA:
				mpath_print_buf_readfullstat(resp);
				break;
		}
		free(resp);
	}
	else if (prout)
	{
		int j;
		struct prout_param_descriptor *paramp;

		paramp= malloc(sizeof(struct prout_param_descriptor) + (sizeof(struct transportid *)*(MPATH_MX_TIDS )));

		memset(paramp, 0, sizeof(struct prout_param_descriptor) + (sizeof(struct transportid *)*(MPATH_MX_TIDS)));

		for (j = 7; j >= 0; --j) {
			paramp->key[j] = (param_rk & 0xff);
			param_rk >>= 8;
		}

		for (j = 7; j >= 0; --j) {
			paramp->sa_key[j] = (param_sark & 0xff);
			param_sark >>= 8;
		}

		if (param_alltgpt)
			paramp->sa_flags |= MPATH_F_ALL_TG_PT_MASK;
		if (param_aptpl)
			paramp->sa_flags |= MPATH_F_APTPL_MASK;

		if (num_transport)
		{
			paramp->sa_flags |= MPATH_F_SPEC_I_PT_MASK;
			paramp->num_transportid = num_transport;
			for (j = 0 ; j < num_transport; j++)
			{
				paramp->trnptid_list[j] = (struct transportid *)malloc(sizeof(struct transportid));
				memcpy(paramp->trnptid_list[j], &transportids[j],sizeof(struct transportid));
			}
		}

		/* PROUT commands other than 'register and move' */
		ret = __mpath_persistent_reserve_out (fd, prout_sa, 0, prout_type,
				paramp, noisy);
		for (j = 0 ; j < num_transport; j++)
		{
			tmp = paramp->trnptid_list[j];
			free(tmp);
		}
		free(paramp);
	}

	if (ret != MPATH_PR_SUCCESS)
	{
		switch(ret)
		{
			case MPATH_PR_SENSE_UNIT_ATTENTION:
				printf("persistent reserve out: scsi status: Unit Attention\n");
				break;
			case MPATH_PR_RESERV_CONFLICT:
				printf("persistent reserve out: scsi status: Reservation Conflict\n");
				break;
		}
		printf("PR out: command failed\n");
	}

out_fd:
	close (fd);
out :
	if (ret == MPATH_PR_SYNTAX_ERROR) {
		free(batch_fn);
		if (nline == 0)
			usage();
		else
			fprintf(stderr, "syntax error on line %d in batch file\n",
				nline);
	} else if (batch_fn != NULL) {
		int rv = do_batch_file(batch_fn);

		free(batch_fn);
		ret = ret == 0 ? rv : ret;
	}
	if (nline == 0)
		mpath_persistent_reserve_free_vecs();
	return (ret >= 0) ? ret : MPATH_PR_OTHER;
}

int main(int argc, char *argv[])
{
	int ret;

	if (optind == argc)
	{

		fprintf (stderr, "No parameter used\n");
		usage ();
		exit (1);
	}

	if (getuid () != 0)
	{
		fprintf (stderr, "need to be root\n");
		exit (1);
	}

	udev = udev_new();
	multipath_conf = mpath_lib_init();
	if(!multipath_conf) {
		udev_unref(udev);
		exit(1);
	}

	ret = handle_args(argc, argv, 0);

	mpath_lib_exit(multipath_conf);
	udev_unref(udev);

	return (ret >= 0) ? ret : MPATH_PR_OTHER;
}

int
get_transportids_length(unsigned char * transportid_arr, int max_transportid, int num_transportids)
{
	int compact_len = 0;
	unsigned char * ucp = transportid_arr;
	int k, off, protocol_id, len;
	for (k = 0, off = 0; ((k < num_transportids) && (k < max_transportid));
			++k, off += MPATH_MX_TID_LEN) {
		protocol_id = ucp[off] & 0xf;
		if (5 == protocol_id) {
			len = (ucp[off + 2] << 8) + ucp[off + 3] + 4;
			if (len < 24)
				len = 24;
			if (off > compact_len)
				memmove(ucp + compact_len, ucp + off, len);
			compact_len += len;

		} else {
			if (off > compact_len)
				memmove(ucp + compact_len, ucp + off, 24);
			compact_len += 24;
		}
	}

	return compact_len;
}

void mpath_print_buf_readkeys( struct prin_resp *pr_buff)
{
	int i,j,k, num;
	unsigned char *keyp;
	uint64_t prkey;
	printf("  PR generation=0x%x, ", pr_buff->prin_descriptor.prin_readkeys.prgeneration);

	num = pr_buff->prin_descriptor.prin_readkeys.additional_length / 8;
	if (0 == num) {
		printf("	0 registered reservation key.\n");
		return;
	}
	else if (1 == num)
		printf("	1 registered reservation key follows:\n");
	else
		printf("	%d registered reservation keys follow:\n", num);


	keyp = (unsigned char *)&pr_buff->prin_descriptor.prin_readkeys.key_list[0];
	for (i = 0; i < num ; i++)
	{
		prkey = 0;
		for (j = 0; j < 8; ++j) {

			if (j > 0)
				prkey <<= 8;
			prkey |= keyp[j];
		}
		printf("    0x%" PRIx64 "\n", prkey);
		k=8*i+j;
		keyp = (unsigned char *)&pr_buff->prin_descriptor.prin_readkeys.key_list[k];
	}
}

void mpath_print_buf_readresv( struct prin_resp *pr_buff)
{
	int j, num, scope=0, type=0;
	unsigned char *keyp;
	uint64_t prkey;

	num = pr_buff->prin_descriptor.prin_readresv.additional_length / 8;
	if (0 == num)
	{
		printf("  PR generation=0x%x, there is NO reservation held \n", pr_buff->prin_descriptor.prin_readresv.prgeneration);
		return ;
	}
	else
		printf("  PR generation=0x%x, Reservation follows:\n", pr_buff->prin_descriptor.prin_readresv.prgeneration);
	keyp = (unsigned  char *)&pr_buff->prin_descriptor.prin_readkeys.key_list[0];
	prkey = 0;
	for (j = 0; j < 8; ++j) {
		if (j > 0)
			prkey <<= 8;
		prkey |= keyp[j];
	}

	printf("   Key = 0x%" PRIx64 "\n", prkey);

	scope = (pr_buff->prin_descriptor.prin_readresv.scope_type >> 4) &  0x0f;
	type = pr_buff->prin_descriptor.prin_readresv.scope_type & 0x0f;

	if (scope == 0)
		printf("  scope = LU_SCOPE, type = %s", pr_type_strs[type]);
	else
		printf("  scope = %d, type = %s", scope, pr_type_strs[type]);

	printf("\n");

}

void mpath_print_buf_readcap( struct prin_resp *pr_buff)
{
	if ( pr_buff->prin_descriptor.prin_readcap.length <= 2 ) {
		fprintf(stderr, "Unexpected response for PRIN Report "
				"Capabilities\n");
		return; //MALFORMED;
	}

	printf("Report capabilities response:\n");

	printf("  Compatible Reservation Handling(CRH): %d\n", !!(pr_buff->prin_descriptor.prin_readcap.flags[0] & 0x10));
	printf("  Specify Initiator Ports Capable(SIP_C): %d\n",!!(pr_buff->prin_descriptor.prin_readcap.flags[0] & 0x8));
	printf("  All Target Ports Capable(ATP_C): %d\n",!!(pr_buff->prin_descriptor.prin_readcap.flags[0] & 0x4 ));
	printf("  Persist Through Power Loss Capable(PTPL_C): %d\n",!!(pr_buff->prin_descriptor.prin_readcap.flags[0]));
	printf("  Type Mask Valid(TMV): %d\n", !!(pr_buff->prin_descriptor.prin_readcap.flags[1] & 0x80));
	printf("  Allow Commands: %d\n", !!(( pr_buff->prin_descriptor.prin_readcap.flags[1] >> 4) & 0x7));
	printf("  Persist Through Power Loss Active(PTPL_A): %d\n",
			!!(pr_buff->prin_descriptor.prin_readcap.flags[1] & 0x1));

	if(pr_buff->prin_descriptor.prin_readcap.flags[1] & 0x80)
	{
		printf("    Support indicated in Type mask:\n");

		printf("      %s: %d\n", pr_type_strs[7], pr_buff->prin_descriptor.prin_readcap.pr_type_mask & 0x80);
		printf("      %s: %d\n", pr_type_strs[6], pr_buff->prin_descriptor.prin_readcap.pr_type_mask & 0x40);
		printf("      %s: %d\n", pr_type_strs[5], pr_buff->prin_descriptor.prin_readcap.pr_type_mask & 0x20);
		printf("      %s: %d\n", pr_type_strs[3], pr_buff->prin_descriptor.prin_readcap.pr_type_mask & 0x8);
		printf("      %s: %d\n", pr_type_strs[1], pr_buff->prin_descriptor.prin_readcap.pr_type_mask & 0x2);
		printf("      %s: %d\n", pr_type_strs[8], pr_buff->prin_descriptor.prin_readcap.pr_type_mask & 0x100);
	}
}

void mpath_print_buf_readfullstat( struct prin_resp *pr_buff)
{

	int i,j, num;
	uint64_t  prkey;
	uint16_t  rel_pt_addr;
	unsigned char * keyp;

	num = pr_buff->prin_descriptor.prin_readfd.number_of_descriptor;
	if (0 == num)
	{
		printf("  PR generation=0x%x \n", pr_buff->prin_descriptor.prin_readfd.prgeneration);
		return ;
	}
	else
		printf("  PR generation=0x%x \n", pr_buff->prin_descriptor.prin_readfd.prgeneration);

	for (i = 0 ; i < num; i++)
	{
		keyp = (unsigned  char *)&pr_buff->prin_descriptor.prin_readfd.descriptors[i]->key;

		prkey = 0;
		for (j = 0; j < 8; ++j) {
			if (j > 0)
				prkey <<= 8;
			prkey |= *keyp;
			++keyp;
		}
		printf("   Key = 0x%" PRIx64 "\n", prkey);

		if (pr_buff->prin_descriptor.prin_readfd.descriptors[i]->flag & 0x02)
			printf("      All target ports bit set\n");
		else {
			printf("      All target ports bit clear\n");

			rel_pt_addr = pr_buff->prin_descriptor.prin_readfd.descriptors[i]->rtpi;
			printf("      Relative port address: 0x%x\n",
					rel_pt_addr);
		}

		if (pr_buff->prin_descriptor.prin_readfd.descriptors[i]->flag & 0x1) {
			printf("      << Reservation holder >>\n");
			j = ((pr_buff->prin_descriptor.prin_readfd.descriptors[i]->scope_type >> 4) & 0xf);
			if (0 == j)
				printf("      scope: LU_SCOPE, ");
			else
				printf("      scope: %d ", j);
			j = (pr_buff->prin_descriptor.prin_readfd.descriptors[i]->scope_type & 0xf);
			printf(" type: %s\n", pr_type_strs[j]);
		} else
			printf("      not reservation holder\n");
		mpath_print_transport_id(pr_buff->prin_descriptor.prin_readfd.descriptors[i]);
	}
}

static void usage(void)
{
	fprintf(stderr, VERSION_STRING);
	fprintf(stderr,
			"Usage: mpathpersist [OPTIONS] [DEVICE]\n"
			" Options:\n"
			"    --verbose|-v level         verbosity level\n"
			"                   0           Critical messages\n"
			"                   1           Error messages\n"
			"                   2           Warning messages\n"
			"                   3           Informational messages\n"
			"                   4           Informational messages with trace enabled\n"
			"    --clear|-C                 PR Out: Clear\n"
			"    --device=DEVICE|-d DEVICE  query or change DEVICE\n"
			"    --batch-file|-f FILE       run commands from FILE\n"
			"    --help|-h                  output this usage message\n"
			"    --hex|-H                   output response in hex\n"
			"    --in|-i                    request PR In command \n"
			"    --out|-o                   request PR Out command\n"
			"    --param-alltgpt|-Y         PR Out parameter 'ALL_TG_PT\n"
			"    --param-aptpl|-Z           PR Out parameter 'APTPL'\n"
			"    --read-keys|-k             PR In: Read Keys\n"
			"    --param-rk=RK|-K RK        PR Out parameter reservation key\n"
			"    --param-sark=SARK|-S SARK  PR Out parameter service action\n"
			"                               reservation key (SARK is in hex)\n"
			"    --preempt|-P               PR Out: Preempt\n"
			"    --preempt-abort|-A         PR Out: Preempt and Abort\n"
			"    --prout-type=TYPE|-T TYPE  PR Out command type\n"
			"    --read-full-status|-s      PR In: Read Full Status\n"
			"    --read-keys|-k             PR In: Read Keys\n"
			"    --read-reservation|-r      PR In: Read Reservation\n"
			"    --register|-G              PR Out: Register\n"
			"    --register-ignore|-I       PR Out: Register and Ignore\n"
			"    --release|-L               PR Out: Release\n"
			"    --report-capabilities|-c   PR In: Report Capabilities\n"
			"    --reserve|-R               PR Out: Reserve\n"
			"    --transport-id=TIDS|-X TIDS  TransportIDs can be mentioned\n"
			"                                 in several forms\n"
			"    --alloc-length=LEN|-l LEN  PR In: maximum allocation length\n");
}

void
mpath_print_transport_id(struct prin_fulldescr *fdesc)
{
	switch (fdesc->trnptid.protocol_id) {
	case MPATH_PROTOCOL_ID_FC:
		printf("   FCP-2 ");
		if (0 != fdesc->trnptid.format_code)
			printf(" [Unexpected format code: %d]\n",
					fdesc->trnptid.format_code);
		dumpHex((const char *)fdesc->trnptid.n_port_name, 8, 0);
		break;
	case MPATH_PROTOCOL_ID_ISCSI:
		printf("   iSCSI ");
		if (0 == fdesc->trnptid.format_code) {
			printf("name: %.*s\n", (int)sizeof(fdesc->trnptid.iscsi_name),
				fdesc->trnptid.iscsi_name);
		}else if (1 == fdesc->trnptid.format_code){
			printf("world wide unique port id: %.*s\n",
					(int)sizeof(fdesc->trnptid.iscsi_name),
					fdesc->trnptid.iscsi_name);
		}else {
			printf("  [Unexpected format code: %d]\n", fdesc->trnptid.format_code);
			dumpHex((const char *)fdesc->trnptid.iscsi_name,
				 (int)sizeof(fdesc->trnptid.iscsi_name), 0);
		}
		break;
	case MPATH_PROTOCOL_ID_SAS:
		printf("   SAS ");
		if (0 != fdesc->trnptid.format_code)
			printf(" [Unexpected format code: %d]\n",
					fdesc->trnptid.format_code);
		dumpHex((const char *)fdesc->trnptid.sas_address, 8, 0);
		break;
	default:
		return;
	}
}

int
construct_transportid(const char * lcp, struct transportid transid[], int num_transportids)
{
	int k = 0;
	int j, n, b, c, len, alen;
	const char * ecp;
	const char * isip;

	if ((0 == memcmp("fcp,", lcp, 4)) ||
			(0 == memcmp("FCP,", lcp, 4))) {
		lcp += 4;
		k = strspn(lcp, "0123456789aAbBcCdDeEfF");

		len = strlen(lcp);
		if (len != k) {
			fprintf(stderr, "badly formed symbolic FCP TransportID: %s\n",
					lcp);
			return 1;
		}
		transid[num_transportids].format_code = MPATH_PROTOCOL_ID_FC;
		transid[num_transportids].protocol_id = MPATH_WWUI_DEVICE_NAME;
		for (k = 0, j = 0, b = 0; k < 16; ++k) {
			c = lcp[k];
			if (isdigit(c))
				n = c - 0x30;
			else if (isupper(c))
				n = c - 0x37;
			else
				n = c - 0x57;
			if (k & 1) {
				transid[num_transportids].n_port_name[j] = b | n;
				++j;
			} else
				b = n << 4;
		}
		goto my_cont_b;
	}
	if ((0 == memcmp("sas,", lcp, 4)) || (0 == memcmp("SAS,", lcp, 4))) {
		lcp += 4;
		k = strspn(lcp, "0123456789aAbBcCdDeEfF");
		len =strlen(lcp);
		if (len != k) {
			fprintf(stderr, "badly formed symbolic SAS TransportID: %s\n",
					lcp);
			return 1;
		}
		transid[num_transportids].format_code = MPATH_PROTOCOL_ID_SAS;
		transid[num_transportids].protocol_id = MPATH_WWUI_DEVICE_NAME;
		memcpy(&transid[num_transportids].sas_address, lcp, 8);

		goto my_cont_b;
	}
	if (0 == memcmp("iqn.", lcp, 4)) {
		ecp = strpbrk(lcp, " \t");
		isip = strstr(lcp, ",i,0x");
		if (ecp && (isip > ecp))
			isip = NULL;
		len = ecp ? (ecp - lcp) : (int)strlen(lcp);
		transid[num_transportids].format_code = (isip ? MPATH_WWUI_PORT_IDENTIFIER:MPATH_WWUI_DEVICE_NAME);
		transid[num_transportids].protocol_id = MPATH_PROTOCOL_ID_ISCSI;
		alen = len + 1; /* at least one trailing null */
		if (alen < 20)
			alen = 20;
		else if (0 != (alen % 4))
			alen = ((alen / 4) + 1) * 4;
		if (alen > 241) { /* sam5r02.pdf A.2 (Annex) */
			fprintf(stderr, "iSCSI name too long, alen=%d\n", alen);
			return 0;
		}
		transid[num_transportids].iscsi_name[1] = alen & 0xff;
		memcpy(&transid[num_transportids].iscsi_name[2], lcp, len);
		goto my_cont_b;
	}
my_cont_b:
	if (k >= MPATH_MAX_PARAM_LEN) {
		fprintf(stderr, "build_transportid: array length exceeded\n");
		return 1;
	}
	return 0;
}
