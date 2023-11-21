#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <libudev.h>
#include <scsi/scsi_netlink_fc.h>
#include <scsi/fc/fc_els.h>

#include "parser.h"
#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "main.h"
#include "debug.h"
#include "util.h"
#include "sysfs.h"

#include "fpin.h"
#include "devmapper.h"

static pthread_cond_t fpin_li_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t fpin_li_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fpin_li_marginal_dev_mutex = PTHREAD_MUTEX_INITIALIZER;

static LIST_HEAD(els_marginal_list_head);
static LIST_HEAD(fpin_li_marginal_dev_list_head);


#define DEF_RX_BUF_SIZE	4096
#define DEV_NAME_LEN	128
#define FCH_EVT_LINKUP 0x2
#define FCH_EVT_LINK_FPIN 0x501
#define FCH_EVT_RSCN 0x5

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

/* max ELS frame Size */
#define FC_PAYLOAD_MAXLEN   2048

struct els_marginal_list {
	uint32_t event_code;
	uint16_t host_num;
	uint16_t length;
	char payload[FC_PAYLOAD_MAXLEN];
	struct list_head node;
};
/* Structure to store the marginal devices info */
struct marginal_dev_list {
	char dev_t[BLK_DEV_SIZE];
	uint32_t host_num;
	struct list_head node;
};

static void _udev_device_unref(void *p)
{
	udev_device_unref(p);
}


/*set/unset the path state to marginal*/
static void fpin_set_pathstate(struct path *pp, bool set)
{
	const char *action = set ? "set" : "unset";

	condlog(3, "%s: %s marginal path %s (fpin)",
		pp->mpp ? pp->mpp->alias : "orphan", action, pp->dev_t);
	pp->marginal = set;
	if (pp->mpp)
		pp->mpp->fpin_must_reload = true;
}

/* This will unset marginal state of a device*/
static void fpin_path_unsetmarginal(char *devname, struct vectors *vecs)
{
	struct path *pp;

	pp = find_path_by_dev(vecs->pathvec, devname);
	if (!pp)
		pp = find_path_by_devt(vecs->pathvec, devname);
	if (pp)
		fpin_set_pathstate(pp, false);
}

/*This will set the marginal state of a device*/
static void  fpin_path_setmarginal(struct path *pp)
{
	fpin_set_pathstate(pp, true);
}

/* Unsets all the devices in the list from marginal state */
static void
fpin_unset_marginal_dev(uint32_t host_num, struct vectors *vecs)
{
	struct marginal_dev_list *tmp_marg = NULL;
	struct marginal_dev_list *marg = NULL;
	struct multipath *mpp;
	int ret = 0;
	int i;

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();

	pthread_mutex_lock(&fpin_li_marginal_dev_mutex);
	pthread_cleanup_push(cleanup_mutex, &fpin_li_marginal_dev_mutex);
	pthread_testcancel();
	if (list_empty(&fpin_li_marginal_dev_list_head)) {
		condlog(4, "Marginal List is empty\n");
		goto empty;
	}
	list_for_each_entry_safe(marg, tmp_marg, &fpin_li_marginal_dev_list_head, node) {
		if (marg->host_num != host_num)
			continue;
		condlog(4, " unsetting marginal dev: is %s %d\n",
				tmp_marg->dev_t, tmp_marg->host_num);
		fpin_path_unsetmarginal(marg->dev_t, vecs);
		list_del(&marg->node);
		free(marg);
	}
empty:
	pthread_cleanup_pop(1);
	/* walk backwards because reload_and_sync_map() can remove mpp */
	vector_foreach_slot_backwards(vecs->mpvec, mpp, i) {
		if (mpp->fpin_must_reload) {
			ret = reload_and_sync_map(mpp, vecs);
			if (ret == 2)
				condlog(2, "map removed during reload");
			else
				mpp->fpin_must_reload = false;
		}
	}
	pthread_cleanup_pop(1);
}

/*
 * On Receiving the frame from HBA driver, insert the frame into link
 * integrity frame list which will be picked up later by consumer thread for
 * processing.
 */
static int
fpin_els_add_li_frame(struct fc_nl_event *fc_event)
{
	struct els_marginal_list *els_mrg = NULL;
	int ret = 0;

	if (fc_event->event_datalen > FC_PAYLOAD_MAXLEN)
		return -EINVAL;

	pthread_mutex_lock(&fpin_li_mutex);
	pthread_cleanup_push(cleanup_mutex, &fpin_li_mutex);
	pthread_testcancel();
	els_mrg = calloc(1, sizeof(struct els_marginal_list));
	if (els_mrg != NULL) {
		els_mrg->host_num = fc_event->host_no;
		els_mrg->event_code = fc_event->event_code;
		els_mrg->length = fc_event->event_datalen;
		memcpy(els_mrg->payload, &(fc_event->event_data), fc_event->event_datalen);
		list_add_tail(&els_mrg->node, &els_marginal_list_head);
		pthread_cond_signal(&fpin_li_cond);
	} else
		ret = -ENOMEM;
	pthread_cleanup_pop(1);
	return ret;

}

/*Sets the rport port_state to marginal*/
static void fpin_set_rport_marginal(struct udev_device *rport_dev)
{
	static const char marginal[] = "Marginal";
	ssize_t ret;

	ret = sysfs_attr_set_value(rport_dev, "port_state",
				   marginal, sizeof(marginal) - 1);
	if (ret != sizeof(marginal) - 1)
		log_sysfs_attr_set_value(2, ret,
					 "%s: failed to set port_state to marginal",
					 udev_device_get_syspath(rport_dev));
}

/*Add the marginal devices info into the list and return 0 on success*/
static int
fpin_add_marginal_dev_info(uint32_t host_num, char *devname)
{
	struct marginal_dev_list *newdev = NULL;

	newdev = calloc(1, sizeof(struct marginal_dev_list));
	if (newdev != NULL) {
		newdev->host_num = host_num;
		strlcpy(newdev->dev_t, devname, BLK_DEV_SIZE);
		condlog(4, "\n%s hostno %d devname %s\n", __func__,
				host_num, newdev->dev_t);
		pthread_mutex_lock(&fpin_li_marginal_dev_mutex);
		list_add_tail(&(newdev->node),
				&fpin_li_marginal_dev_list_head);
		pthread_mutex_unlock(&fpin_li_marginal_dev_mutex);
	} else
		return -ENOMEM;
	return 0;
}

/*
 * This function compares Transport Address Controller Port pn,
 * Host Transport Address Controller Port pn with the els wwpn ,attached_wwpn
 * and return 1 (match) or 0 (no match) or a negative error code
 */
static int  extract_nvme_addresses_chk_path_pwwn(const char *address,
		uint64_t els_wwpn, uint64_t els_attached_wwpn)

{
	uint64_t traddr;
	uint64_t host_traddr;

	/*
	 *  Find the position of "traddr=" and "host_traddr="
	 *  and the address will be in the below format
	 *  "traddr=nn-0x200400110dff9400:pn-0x200400110dff9400,
	 *  host_traddr=nn-0x200400110dff9400:pn-0x200400110dff9400"
	 */
	const char *traddr_start = strstr(address, "traddr=");
	const char *host_traddr_start = strstr(address, "host_traddr=");

	if (!traddr_start || !host_traddr_start)
		return -EINVAL;

	/* Extract traddr pn */
	if (sscanf(traddr_start, "traddr=nn-%*[^:]:pn-%" SCNx64, &traddr) != 1)
		return -EINVAL;

	/* Extract host_traddr pn*/
	if (sscanf(host_traddr_start, "host_traddr=nn-%*[^:]:pn-%" SCNx64,
				&host_traddr) != 1)
		return -EINVAL;
	condlog(4, "traddr 0x%" PRIx64 " hosttraddr 0x%" PRIx64 " els_wwpn 0x%"
		PRIx64" els_host_traddr 0x%" PRIx64,
			traddr, host_traddr,
			els_wwpn, els_attached_wwpn);
	if ((host_traddr == els_attached_wwpn) && (traddr == els_wwpn))
		return 1;
	return 0;
}

/*
 * This function check that the Transport Address Controller Port pn,
 * Host Transport Address Controller Port pn associated with the path matches
 * with the els wwpn ,attached_wwpn and sets the path state to
 * Marginal
 */
static void fpin_check_set_nvme_path_marginal(uint16_t host_num, struct path *pp,
		uint64_t els_wwpn, uint64_t attached_wwpn)
{
	struct udev_device *ctl = NULL;
	const char *address = NULL;
	int ret = 0;

	ctl = udev_device_get_parent_with_subsystem_devtype(pp->udev, "nvme", NULL);
	if (ctl == NULL) {
		condlog(2, "%s: No parent device for ", pp->dev);
		return;
	}
	address = udev_device_get_sysattr_value(ctl, "address");
	if (!address) {
		condlog(2, "%s: unable to get the address ", pp->dev);
		return;
	}
	condlog(4, "\n address %s: dev :%s\n", address, pp->dev);
	ret = extract_nvme_addresses_chk_path_pwwn(address, els_wwpn, attached_wwpn);
	if (ret <= 0)
		return;
	ret = fpin_add_marginal_dev_info(host_num, pp->dev);
	if (ret < 0)
		return;
	fpin_path_setmarginal(pp);
}

/*
 * This function check the host  number, the target WWPN
 * associated with the path matches with the els wwpn and
 * sets the path and port state to Marginal
 */
static void fpin_check_set_scsi_path_marginal(uint16_t host_num, struct path *pp,
		uint64_t els_wwpn)
{
	char rport_id[42];
	const char *value = NULL;
	struct udev_device *rport_dev = NULL;
	uint64_t wwpn;
	int ret = 0;
	sprintf(rport_id, "rport-%d:%d-%d",
			pp->sg_id.host_no, pp->sg_id.channel, pp->sg_id.transport_id);
	rport_dev = udev_device_new_from_subsystem_sysname(udev,
			"fc_remote_ports", rport_id);
	if (!rport_dev) {
		condlog(2, "%s: No fc_remote_port device for '%s'", pp->dev,
				rport_id);
		return;
	}
	pthread_cleanup_push(_udev_device_unref, rport_dev);
	value = udev_device_get_sysattr_value(rport_dev, "port_name");
	if (!value)
		goto unref;

	wwpn =  strtol(value, NULL, 16);
	/*
	 * If the port wwpn matches sets the path and port state
	 * to marginal
	 */
	if (wwpn == els_wwpn) {
		ret = fpin_add_marginal_dev_info(host_num, pp->dev);
		if (ret < 0)
			goto unref;
		fpin_path_setmarginal(pp);
		fpin_set_rport_marginal(rport_dev);
	}
unref:
	pthread_cleanup_pop(1);
	return;

}

/*
 * This function goes through the vecs->pathvec, and for
 * each path, it checks and sets the path state to marginal
 * if the path's associated port wwpn ,hostnum  matches with
 * els wwnpn ,attached_wwpn
 */
static int  fpin_chk_wwn_setpath_marginal(uint16_t host_num,  struct vectors *vecs,
		uint64_t els_wwpn, uint64_t attached_wwpn)
{
	struct path *pp;
	struct multipath *mpp;
	int i, k;
	int ret = 0;

	pthread_cleanup_push(cleanup_lock, &vecs->lock);
	lock(&vecs->lock);
	pthread_testcancel();

	vector_foreach_slot(vecs->pathvec, pp, k) {
		if (!pp->mpp)
			continue;
		/*checks if the bus type is nvme  and the protocol is FC-NVMe*/
		if ((pp->bus == SYSFS_BUS_NVME) && (pp->sg_id.proto_id == NVME_PROTOCOL_FC)) {
			fpin_check_set_nvme_path_marginal(host_num, pp, els_wwpn, attached_wwpn);
		} else if ((pp->bus == SYSFS_BUS_SCSI) &&
			(pp->sg_id.proto_id == SCSI_PROTOCOL_FCP) &&
			(host_num ==  pp->sg_id.host_no)) {
			/* Checks the host number and also for the SCSI FCP */
			fpin_check_set_scsi_path_marginal(host_num, pp, els_wwpn);
		}
	}
	/* walk backwards because reload_and_sync_map() can remove mpp */
	vector_foreach_slot_backwards(vecs->mpvec, mpp, i) {
		if (mpp->fpin_must_reload) {
			ret = reload_and_sync_map(mpp, vecs);
			if (ret == 2)
				condlog(2, "map removed during reload");
			else
				mpp->fpin_must_reload = false;
		}
	}
	pthread_cleanup_pop(1);
	return ret;
}

/*
 * This function loops around all the impacted wwns received as part of els
 * frame and sets the associated path and port states to marginal.
 */
static int
fpin_parse_li_els_setpath_marginal(uint16_t host_num, struct fc_tlv_desc *tlv,
		struct vectors *vecs)
{
	uint32_t wwn_count = 0, iter = 0;
	uint64_t wwpn;
	struct fc_fn_li_desc *li_desc = (struct fc_fn_li_desc *)tlv;
	int count = 0;
	int ret = 0;
	uint64_t attached_wwpn;

	/* Update the wwn to list */
	wwn_count = be32_to_cpu(li_desc->pname_count);
	attached_wwpn = be64_to_cpu(li_desc->attached_wwpn);
	condlog(4, "Got wwn count as %d detecting wwn 0x%" PRIx64
		" attached_wwpn 0x%" PRIx64 "\n",
			wwn_count, be64_to_cpu(li_desc->detecting_wwpn), attached_wwpn);

	for (iter = 0; iter < wwn_count; iter++) {
		wwpn = be64_to_cpu(li_desc->pname_list[iter]);
		ret = fpin_chk_wwn_setpath_marginal(host_num, vecs, wwpn, attached_wwpn);
		if (ret < 0)
			condlog(2, "failed to set the path marginal associated with wwpn: 0x%" PRIx64 "\n", wwpn);

		count++;
	}
	return count;
}

/*
 * This function process the ELS frame received from HBA driver,
 * and sets the path associated with the port wwn to marginal
 * and also set the port state to marginal.
 */
static int
fpin_process_els_frame(uint16_t host_num, char *fc_payload, struct vectors *vecs)
{

	int count = -1;
	struct fc_els_fpin *fpin = (struct fc_els_fpin *)fc_payload;
	struct fc_tlv_desc *tlv;

	tlv = (struct fc_tlv_desc *)&fpin->fpin_desc[0];

	/*
	 * Parse the els frame and set the affected paths and port
	 * state to marginal
	 */
	count = fpin_parse_li_els_setpath_marginal(host_num, tlv, vecs);
	if (count <= 0)
		condlog(4, "Could not find any WWNs, ret = %d\n",
					count);
	return count;
}

/*
 * This function process the FPIN ELS frame received from HBA driver,
 * and push the frame to appropriate frame list. Currently we have only FPIN
 * LI frame list.
 */
static int
fpin_handle_els_frame(struct fc_nl_event *fc_event)
{
	int ret = -1;
	uint32_t els_cmd;
	struct fc_els_fpin *fpin = (struct fc_els_fpin *)&fc_event->event_data;
	struct fc_tlv_desc *tlv;
	uint32_t dtag;

	els_cmd = (uint32_t)fc_event->event_data;
	tlv = (struct fc_tlv_desc *)&fpin->fpin_desc[0];
	dtag = be32_to_cpu(tlv->desc_tag);
	condlog(4, "Got CMD in add as 0x%x fpin_cmd 0x%x dtag 0x%x\n",
			els_cmd, fpin->fpin_cmd, dtag);

	if ((fc_event->event_code == FCH_EVT_LINK_FPIN) ||
			(fc_event->event_code == FCH_EVT_LINKUP) ||
			(fc_event->event_code == FCH_EVT_RSCN)) {

		if (els_cmd == ELS_FPIN) {
			/*
			 * Check the type of fpin by checking the tag info
			 * At present we are supporting only LI events
			 */
			if (dtag == ELS_DTAG_LNK_INTEGRITY) {
				/*Push the Payload to FPIN frame queue. */
				ret = fpin_els_add_li_frame(fc_event);
				if (ret != 0)
					condlog(0, "Failed to process LI frame with error %d\n",
							ret);
			} else {
				condlog(4, "Unsupported FPIN received 0x%x\n", dtag);
				return ret;
			}
		} else {
			/*Push the Payload to FPIN frame queue. */
			ret = fpin_els_add_li_frame(fc_event);
			if (ret != 0)
				condlog(0, "Failed to process Linkup/RSCN event with error %d evnt %d\n",
						ret, fc_event->event_code);
		}
	} else
		condlog(4, "Invalid command received: 0x%x\n", els_cmd);
	return ret;
}

/*cleans the global marginal dev list*/
void fpin_clean_marginal_dev_list(__attribute__((unused)) void *arg)
{
	struct marginal_dev_list *tmp_marg = NULL;

	pthread_mutex_lock(&fpin_li_marginal_dev_mutex);
	while (!list_empty(&fpin_li_marginal_dev_list_head)) {
		tmp_marg  = list_first_entry(&fpin_li_marginal_dev_list_head,
				struct marginal_dev_list, node);
		list_del(&tmp_marg->node);
		free(tmp_marg);
	}
	pthread_mutex_unlock(&fpin_li_marginal_dev_mutex);
}

/* Cleans the global els  marginal list */
static void fpin_clean_els_marginal_list(void *arg)
{
	struct list_head *head = (struct list_head *)arg;
	struct els_marginal_list *els_marg;

	while (!list_empty(head)) {
		els_marg  = list_first_entry(head, struct els_marginal_list,
					     node);
		list_del(&els_marg->node);
		free(els_marg);
	}
}

static void rcu_unregister(__attribute__((unused)) void *param)
{
	rcu_unregister_thread();
}
/*
 * This is the FPIN ELS consumer thread. The thread sleeps on pthread cond
 * variable unless notified by fpin_fabric_notification_receiver thread.
 * This thread is only to process FPIN-LI ELS frames. A new thread and frame
 * list will be added if any more ELS frames types are to be supported.
 */
void *fpin_els_li_consumer(void *data)
{
	struct list_head marginal_list_head;
	int ret = 0;
	uint16_t host_num;
	struct els_marginal_list *els_marg;
	uint32_t event_code;
	struct vectors *vecs = (struct vectors *)data;

	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();
	pthread_cleanup_push(fpin_clean_marginal_dev_list, NULL);
	INIT_LIST_HEAD(&marginal_list_head);
	pthread_cleanup_push(fpin_clean_els_marginal_list,
				(void *)&marginal_list_head);
	for ( ; ; ) {
		pthread_mutex_lock(&fpin_li_mutex);
		pthread_cleanup_push(cleanup_mutex, &fpin_li_mutex);
		pthread_testcancel();
		while (list_empty(&els_marginal_list_head))
			pthread_cond_wait(&fpin_li_cond, &fpin_li_mutex);

		if (!list_empty(&els_marginal_list_head)) {
			condlog(4, "Invoke List splice tail\n");
			list_splice_tail_init(&els_marginal_list_head, &marginal_list_head);
		}
		pthread_cleanup_pop(1);

		while (!list_empty(&marginal_list_head)) {
			els_marg  = list_first_entry(&marginal_list_head,
							struct els_marginal_list, node);
			host_num = els_marg->host_num;
			event_code = els_marg->event_code;
			/* Now finally process FPIN LI ELS Frame */
			condlog(4, "Got a new Payload buffer, processing it\n");
			if ((event_code ==  FCH_EVT_LINKUP) || (event_code == FCH_EVT_RSCN))
				 fpin_unset_marginal_dev(host_num, vecs);
			else {
				ret = fpin_process_els_frame(host_num, els_marg->payload, vecs);
				if (ret <= 0)
					condlog(0, "ELS frame processing failed with ret %d\n", ret);
			}
			list_del(&els_marg->node);
			free(els_marg);

		}
	}

	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	return NULL;
}

static void receiver_cleanup_list(__attribute__((unused)) void *arg)
{
	pthread_mutex_lock(&fpin_li_mutex);
	fpin_clean_els_marginal_list(&els_marginal_list_head);
	pthread_mutex_unlock(&fpin_li_mutex);
}

/*
 * Listen for ELS frames from driver. on receiving the frame payload,
 * push the payload to a list, and notify the fpin_els_li_consumer thread to
 * process it. Once consumer thread is notified, return to listen for more ELS
 * frames from driver.
 */
void *fpin_fabric_notification_receiver(__attribute__((unused))void *unused)
{
	int ret;
	int fd = -1;
	uint32_t els_cmd;
	struct fc_nl_event *fc_event = NULL;
	struct sockaddr_nl fc_local;
	unsigned char buf[DEF_RX_BUF_SIZE] __attribute__((aligned(sizeof(uint64_t))));
	size_t plen = 0;

	pthread_cleanup_push(rcu_unregister, NULL);
	rcu_register_thread();

	pthread_cleanup_push(receiver_cleanup_list, NULL);
	pthread_cleanup_push(cleanup_fd_ptr, &fd);

	fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_SCSITRANSPORT);
	if (fd < 0) {
		condlog(0, "fc socket error %d", fd);
		goto out;
	}

	memset(&fc_local, 0, sizeof(fc_local));
	fc_local.nl_family = AF_NETLINK;
	fc_local.nl_groups = ~0;
	fc_local.nl_pid = getpid();
	ret = bind(fd, (struct sockaddr *)&fc_local, sizeof(fc_local));
	if (ret == -1) {
		condlog(0, "fc socket bind error %d\n", ret);
		goto out;
	}
	for ( ; ; ) {
		struct nlmsghdr *msghdr;

		condlog(4, "Waiting for ELS...\n");
		ret = read(fd, buf, DEF_RX_BUF_SIZE);
		if (ret < 0) {
			condlog(0, "failed to read the els frame (%d)", ret);
			continue;
		}
		condlog(4, "Got a new request %d\n", ret);
		msghdr = (struct nlmsghdr *)buf;
		if (!NLMSG_OK(msghdr, (unsigned int)ret)) {
			condlog(0, "bad els frame read (%d)", ret);
			continue;
		}
		/* Push the frame to appropriate frame list */
		plen = NLMSG_PAYLOAD(msghdr, 0);
		fc_event = (struct fc_nl_event *)NLMSG_DATA(buf);
		if (plen < sizeof(*fc_event)) {
			condlog(0, "too short (%d) to be an FC event", ret);
			continue;
		}
		els_cmd = (uint32_t)fc_event->event_data;
		condlog(4, "Got host no as %d, event 0x%x, len %d evntnum %d evntcode %d\n",
				fc_event->host_no, els_cmd, fc_event->event_datalen,
				fc_event->event_num, fc_event->event_code);
		fpin_handle_els_frame(fc_event);
	}
out:
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
	return NULL;
}
