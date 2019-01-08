#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <scsi/scsi.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <libudev.h>
#include "mpath_pr_ioctl.h"
#include "mpath_persist.h"
#include "unaligned.h"

#include "debug.h"

#define FILE_NAME_SIZE          256

#define TIMEOUT 2000
#define MAXRETRY 5

int prin_do_scsi_ioctl(char * dev, int rq_servact, struct prin_resp *resp, int noisy);
void mpath_format_readkeys(struct prin_resp *pr_buff, int len , int noisy);
void mpath_format_readfullstatus(struct prin_resp *pr_buff, int len, int noisy);
int mpath_translate_response (char * dev, struct sg_io_hdr io_hdr,
			      SenseData_t *Sensedata, int noisy);
void dumpHex(const char* str, int len, int no_ascii);
int prout_do_scsi_ioctl( char * dev, int rq_servact, int rq_scope,
		unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy);
uint32_t  format_transportids(struct prout_param_descriptor *paramp);
void convert_be32_to_cpu(uint32_t *num);
void convert_be16_to_cpu(uint16_t *num);
void decode_transport_id(struct prin_fulldescr *fdesc, unsigned char * p, int length);
int get_prin_length(int rq_servact);
int mpath_isLittleEndian(void);

unsigned int mpath_mx_alloc_len;

int prout_do_scsi_ioctl(char * dev, int rq_servact, int rq_scope,
		unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy)
{

	int status, paramlen = 24, ret = 0;
	uint32_t translen=0;
	int retry = MAXRETRY;
	SenseData_t Sensedata;
	struct sg_io_hdr io_hdr;
	char devname[FILE_NAME_SIZE];
	int fd = -1;

	snprintf(devname, FILE_NAME_SIZE, "/dev/%s",dev);
	fd = open(devname, O_RDONLY);
	if(fd < 0){
		condlog (1, "%s: unable to open device.", dev);
		return MPATH_PR_FILE_ERROR;
	}

	unsigned char cdb[MPATH_PROUT_CMDLEN] =
	{MPATH_PROUT_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0};


	if (paramp->sa_flags & MPATH_F_SPEC_I_PT_MASK)
	{
		translen = format_transportids(paramp);
		paramlen = 24 + translen;
	}
	else
		paramlen = 24;

	if ( rq_servact > 0)
		cdb[1] = (unsigned char)(rq_servact & 0x1f);
	cdb[2] = (((rq_scope & 0xf) << 4) | (rq_type & 0xf));
	cdb[7] = (unsigned char)((paramlen >> 8) & 0xff);
	cdb[8] = (unsigned char)(paramlen & 0xff);

retry :
	condlog(4, "%s: rq_servact = %d", dev, rq_servact);
	condlog(4, "%s: rq_scope = %d ", dev, rq_scope);
	condlog(4, "%s: rq_type = %d ", dev, rq_type);
	condlog(4, "%s: paramlen = %d", dev, paramlen);

	if (noisy)
	{
		condlog(4, "%s: Persistent Reservation OUT parameter:", dev);
		dumpHex((const char *)paramp, paramlen,1);
	}

	memset(&Sensedata, 0, sizeof(SenseData_t));
	memset(&io_hdr,0 , sizeof( struct sg_io_hdr));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = MPATH_PROUT_CMDLEN;
	io_hdr.cmdp = cdb;
	io_hdr.sbp = (void *)&Sensedata;
	io_hdr.mx_sb_len = sizeof (SenseData_t);
	io_hdr.timeout = TIMEOUT;

	if (paramlen > 0) {
		io_hdr.dxferp = (void *)paramp;
		io_hdr.dxfer_len = paramlen;
		io_hdr.dxfer_direction = SG_DXFER_TO_DEV ;
	}
	else {
		io_hdr.dxfer_direction = SG_DXFER_NONE;
	}
	ret = ioctl(fd, SG_IO, &io_hdr);
	if (ret < 0)
	{
		condlog(0, "%s: ioctl failed %d", dev, ret);
		close(fd);
		return ret;
	}

	condlog(4, "%s: Duration=%u (ms)", dev, io_hdr.duration);

	status = mpath_translate_response(dev, io_hdr, &Sensedata, noisy);
	condlog(3, "%s: status = %d", dev, status);

	if (status == MPATH_PR_SENSE_UNIT_ATTENTION && (retry > 0))
	{
		--retry;
		condlog(3, "%s: retrying for Unit Attention. Remaining retries = %d",
			dev, retry);
		goto retry;
	}

	if (((status == MPATH_PR_SENSE_NOT_READY )&& (Sensedata.ASC == 0x04)&&
				(Sensedata.ASCQ == 0x07))&& (retry > 0))
	{
		usleep(1000);
		--retry;
		condlog(3, "%s: retrying for sense 02/04/07."
			" Remaining retries = %d", dev, retry);
		goto retry;
	}

	close(fd);
	return status;
}

uint32_t  format_transportids(struct prout_param_descriptor *paramp)
{
	int i = 0, len;
	uint32_t buff_offset = 4;
	memset(paramp->private_buffer, 0, MPATH_MAX_PARAM_LEN);
	for (i=0; i < paramp->num_transportid; i++ )
	{
		paramp->private_buffer[buff_offset] = (uint8_t)((paramp->trnptid_list[i]->format_code & 0xff)|
							(paramp->trnptid_list[i]->protocol_id & 0xff));
		buff_offset += 1;
		switch(paramp->trnptid_list[i]->protocol_id)
		{
			case MPATH_PROTOCOL_ID_FC:
				buff_offset += 7;
				memcpy(&paramp->private_buffer[buff_offset], &paramp->trnptid_list[i]->n_port_name, 8);
				buff_offset +=8 ;
				buff_offset +=8 ;
				break;
			case MPATH_PROTOCOL_ID_SAS:
				buff_offset += 3;
				memcpy(&paramp->private_buffer[buff_offset], &paramp->trnptid_list[i]->sas_address, 8);
				buff_offset += 12;
				break;
			case MPATH_PROTOCOL_ID_ISCSI:
				buff_offset += 1;
				len = (paramp->trnptid_list[i]->iscsi_name[1] & 0xff)+2;
				memcpy(&paramp->private_buffer[buff_offset], &paramp->trnptid_list[i]->iscsi_name,len);
				buff_offset += len ;
				break;
		}

	}
	buff_offset -= 4;
	paramp->private_buffer[0] = (unsigned char)((buff_offset >> 24) & 0xff);
	paramp->private_buffer[1] = (unsigned char)((buff_offset >> 16) & 0xff);
	paramp->private_buffer[2] = (unsigned char)((buff_offset >> 8) & 0xff);
	paramp->private_buffer[3] = (unsigned char)(buff_offset & 0xff);
	buff_offset += 4;
	return buff_offset;
}

void mpath_format_readkeys( struct prin_resp *pr_buff, int len, int noisy)
{
	convert_be32_to_cpu(&pr_buff->prin_descriptor.prin_readkeys.prgeneration);
	convert_be32_to_cpu(&pr_buff->prin_descriptor.prin_readkeys.additional_length);
}

void mpath_format_readresv(struct prin_resp *pr_buff, int len, int noisy)
{

	convert_be32_to_cpu(&pr_buff->prin_descriptor.prin_readkeys.prgeneration);
	convert_be32_to_cpu(&pr_buff->prin_descriptor.prin_readkeys.additional_length);

	return;
}

void mpath_format_reportcapabilities(struct prin_resp *pr_buff, int len, int noisy)
{
	convert_be16_to_cpu(&pr_buff->prin_descriptor.prin_readcap.length);
	convert_be16_to_cpu(&pr_buff->prin_descriptor.prin_readcap.pr_type_mask);

	return;
}

void mpath_format_readfullstatus(struct prin_resp *pr_buff, int len, int noisy)
{
	int num, k, tid_len_len=0;
	uint32_t fdesc_count=0;
	unsigned char *p;
	char  *ppbuff;
	uint32_t additional_length;
	char tempbuff[MPATH_MAX_PARAM_LEN];
	struct prin_fulldescr fdesc;

	convert_be32_to_cpu(&pr_buff->prin_descriptor.prin_readfd.prgeneration);
	convert_be32_to_cpu(&pr_buff->prin_descriptor.prin_readfd.number_of_descriptor);

	if (pr_buff->prin_descriptor.prin_readfd.number_of_descriptor == 0)
	{
		condlog(3, "No registration or reservation found.");
		return;
	}

	additional_length = pr_buff->prin_descriptor.prin_readfd.number_of_descriptor;
	if (additional_length > MPATH_MAX_PARAM_LEN) {
		condlog(3, "PRIN length %u exceeds max length %d", additional_length,
			MPATH_MAX_PARAM_LEN);
		return;
	}

	memset(&fdesc, 0, sizeof(struct prin_fulldescr));

	memcpy( tempbuff, pr_buff->prin_descriptor.prin_readfd.private_buffer,MPATH_MAX_PARAM_LEN );
	memset(&pr_buff->prin_descriptor.prin_readfd.private_buffer, 0, MPATH_MAX_PARAM_LEN);

	p =(unsigned char *)tempbuff;
	ppbuff = (char *)pr_buff->prin_descriptor.prin_readfd.private_buffer;

	for (k = 0; k < additional_length; k += num, p += num) {
		memcpy(&fdesc.key, p, 8 );
		fdesc.flag = p[12];
		fdesc.scope_type =  p[13];
		fdesc.rtpi = get_unaligned_be16(&p[18]);

		tid_len_len = get_unaligned_be32(&p[20]);
		if (tid_len_len + 24 + k > additional_length) {
			condlog(0,
				"%s: corrupt PRIN response: status descriptor end %d exceeds length %d",
				__func__, tid_len_len + k + 24,
				additional_length);
			tid_len_len = additional_length - k - 24;
		}

		if (tid_len_len > 0)
			decode_transport_id( &fdesc, &p[24], tid_len_len);

		num = 24 + tid_len_len;
		memcpy(ppbuff, &fdesc, sizeof(struct prin_fulldescr));
		pr_buff->prin_descriptor.prin_readfd.descriptors[fdesc_count]= (struct prin_fulldescr *)ppbuff;
		ppbuff += sizeof(struct prin_fulldescr);
		++fdesc_count;
	}

	pr_buff->prin_descriptor.prin_readfd.number_of_descriptor = fdesc_count;

	return;
}

void
decode_transport_id(struct prin_fulldescr *fdesc, unsigned char * p, int length)
{
	int num, k;
	int jump;
	for (k = 0, jump = 24; k < length; k += jump, p += jump) {
		fdesc->trnptid.format_code = ((p[0] >> 6) & 0x3);
		fdesc->trnptid.protocol_id = (p[0] & 0xf);
		switch (fdesc->trnptid.protocol_id) {
		case MPATH_PROTOCOL_ID_FC:
			memcpy(&fdesc->trnptid.n_port_name, &p[8], 8);
			jump = 24;
			break;
		case MPATH_PROTOCOL_ID_ISCSI:
			num = get_unaligned_be16(&p[2]);
			if (num >= sizeof(fdesc->trnptid.iscsi_name))
				num = sizeof(fdesc->trnptid.iscsi_name);
			memcpy(&fdesc->trnptid.iscsi_name, &p[4], num);
			jump = (((num + 4) < 24) ? 24 : num + 4);
			break;
		case MPATH_PROTOCOL_ID_SAS:
			memcpy(&fdesc->trnptid.sas_address, &p[4], 8);
			jump = 24;
			break;
		default:
			jump = 24;
			break;
		}
	}
}

int prin_do_scsi_ioctl(char * dev, int rq_servact, struct prin_resp * resp, int noisy)
{

	int ret, status, got, fd;
	int mx_resp_len;
	SenseData_t Sensedata;
	int retry = MAXRETRY;
	struct sg_io_hdr io_hdr;
	char devname[FILE_NAME_SIZE];
	unsigned char cdb[MPATH_PRIN_CMDLEN] =
	{MPATH_PRIN_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	snprintf(devname, FILE_NAME_SIZE, "/dev/%s",dev);
	fd = open(devname, O_RDONLY);
	if(fd < 0){
		condlog(0, "%s: Unable to open device ", dev);
		return MPATH_PR_FILE_ERROR;
	}

	if (mpath_mx_alloc_len)
		mx_resp_len = mpath_mx_alloc_len;
	else
		mx_resp_len = get_prin_length(rq_servact);

	if (mx_resp_len == 0) {
		status = MPATH_PR_SYNTAX_ERROR;
		goto out;
	}

	cdb[1] = (unsigned char)(rq_servact & 0x1f);
	cdb[7] = (unsigned char)((mx_resp_len >> 8) & 0xff);
	cdb[8] = (unsigned char)(mx_resp_len & 0xff);

retry :
	memset(&Sensedata, 0, sizeof(SenseData_t));
	memset(&io_hdr,0 , sizeof( struct sg_io_hdr));

	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = MPATH_PRIN_CMDLEN;
	io_hdr.mx_sb_len = sizeof (SenseData_t);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.cmdp = cdb;
	io_hdr.sbp = (void *)&Sensedata;
	io_hdr.timeout = TIMEOUT;



	io_hdr.dxfer_len = mx_resp_len;
	io_hdr.dxferp = (void *)resp;

	ret =ioctl(fd, SG_IO, &io_hdr);
	if (ret < 0){
		condlog(0, "%s: IOCTL failed %d", dev, ret);
		status = MPATH_PR_OTHER;
		goto out;
	}

	got = mx_resp_len - io_hdr.resid;

	condlog(3, "%s: duration = %u (ms)", dev, io_hdr.duration);
	condlog(4, "%s: persistent reservation in: requested %d bytes but got %d bytes)", dev, mx_resp_len, got);

	status = mpath_translate_response(dev, io_hdr, &Sensedata, noisy);

	if (status == MPATH_PR_SENSE_UNIT_ATTENTION && (retry > 0))
	{
		--retry;
		condlog(3, "%s: retrying for Unit Attention. Remaining retries = %d", dev, retry);
		goto retry;
	}

	if (((status == MPATH_PR_SENSE_NOT_READY )&& (Sensedata.ASC == 0x04)&&
				(Sensedata.ASCQ == 0x07))&& (retry > 0))
	{
		usleep(1000);
		--retry;
		condlog(3, "%s: retrying for 02/04/07. Remaining retries = %d", dev, retry);
		goto retry;
	}

	if (status != MPATH_PR_SUCCESS)
		goto out;

	if (noisy)
		dumpHex((const char *)resp, got , 1);


	switch (rq_servact)
	{
		case MPATH_PRIN_RKEY_SA :
			mpath_format_readkeys(resp, got, noisy);
			break;
		case MPATH_PRIN_RRES_SA :
			mpath_format_readresv(resp, got, noisy);
			break;
		case MPATH_PRIN_RCAP_SA :
			mpath_format_reportcapabilities(resp, got, noisy);
			break;
		case MPATH_PRIN_RFSTAT_SA :
			mpath_format_readfullstatus(resp, got, noisy);
	}

out:
	close(fd);
	return status;
}

int mpath_translate_response (char * dev, struct sg_io_hdr io_hdr,
			      SenseData_t *Sensedata, int noisy)
{
	condlog(3, "%s: status driver:%02x host:%02x scsi:%02x", dev,
			io_hdr.driver_status, io_hdr.host_status ,io_hdr.status);
	io_hdr.status &= 0x7e;
	if ((0 == io_hdr.status) &&
	    (0 == io_hdr.host_status) &&
	    (0 == io_hdr.driver_status))
		return MPATH_PR_SUCCESS;

	switch(io_hdr.status) {
	case SAM_STAT_GOOD:
		break;
	case SAM_STAT_CHECK_CONDITION:
		condlog(3, "%s: Sense_Key=%02x, ASC=%02x ASCQ=%02x",
			dev, Sensedata->Sense_Key,
			Sensedata->ASC, Sensedata->ASCQ);
		switch(Sensedata->Sense_Key) {
		case NO_SENSE:
			return MPATH_PR_NO_SENSE;
		case RECOVERED_ERROR:
			return MPATH_PR_SUCCESS;
		case NOT_READY:
			return MPATH_PR_SENSE_NOT_READY;
		case MEDIUM_ERROR:
			return MPATH_PR_SENSE_MEDIUM_ERROR;
		case BLANK_CHECK:
			return MPATH_PR_OTHER;
		case HARDWARE_ERROR:
			return MPATH_PR_SENSE_HARDWARE_ERROR;
		case ILLEGAL_REQUEST:
			return MPATH_PR_ILLEGAL_REQ;
		case UNIT_ATTENTION:
			return MPATH_PR_SENSE_UNIT_ATTENTION;
		case DATA_PROTECT:
		case COPY_ABORTED:
			return MPATH_PR_OTHER;
		case ABORTED_COMMAND:
			return MPATH_PR_SENSE_ABORTED_COMMAND;

		default :
			return MPATH_PR_OTHER;
		}
	case SAM_STAT_RESERVATION_CONFLICT:
		return MPATH_PR_RESERV_CONFLICT;

	default :
		return  MPATH_PR_OTHER;
	}

	switch(io_hdr.host_status) {
	case DID_OK :
		break;
	default :
		return MPATH_PR_OTHER;
	}
	switch(io_hdr.driver_status)
	{
	case DRIVER_OK:
		break;
	default :
		return MPATH_PR_OTHER;
	}
	return MPATH_PR_SUCCESS;
}

void convert_be16_to_cpu(uint16_t *num)
{
	*num = get_unaligned_be16(num);
}

void convert_be32_to_cpu(uint32_t *num)
{
	*num = get_unaligned_be32(num);
}

void
dumpHex(const char* str, int len, int log)
{
	const char * p = str;
	unsigned char c;
	char buff[82];
	const int bpstart = 5;
	int bpos = bpstart;
	int  k;

	if (len <= 0)
		return;
	memset(buff, ' ', 80);
	buff[80] = '\0';
	for (k = 0; k < len; k++) {
		c = *p++;
		bpos += 3;
		if (bpos == (bpstart + (9 * 3)))
			bpos++;
		sprintf(&buff[bpos], "%.2x", (int)(unsigned char)c);
		buff[bpos + 2] = ' ';
		if ((k > 0) && (0 == ((k + 1) % 16))) {
			if (log)
				condlog(0, "%.76s" , buff);
			else
				printf("%.76s" , buff);
			bpos = bpstart;
			memset(buff, ' ', 80);
		}
	}
	if (bpos > bpstart) {
		buff[bpos + 2] = '\0';
		if (log)
			condlog(0, "%s", buff);
		else
			printf("%s\n" , buff);
	}
	return;
}

int get_prin_length(int rq_servact)
{
	int mx_resp_len;
	switch (rq_servact)
	{
		case MPATH_PRIN_RKEY_SA:
			mx_resp_len =  sizeof(struct prin_readdescr);
			break;
		case MPATH_PRIN_RRES_SA :
			mx_resp_len =  sizeof(struct prin_resvdescr);
			break;
		case MPATH_PRIN_RCAP_SA :
			mx_resp_len = sizeof(struct prin_capdescr);
			break;
		case MPATH_PRIN_RFSTAT_SA:
			mx_resp_len = sizeof(struct print_fulldescr_list) + sizeof(struct prin_fulldescr *)*32;
			break;
		default:
			condlog(0, "invalid service action, %d", rq_servact);
			mx_resp_len = 0;
			break;
	}
	return mx_resp_len;
}
