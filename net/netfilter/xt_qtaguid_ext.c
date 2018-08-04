/*
 * Copyright (C) [2017] Motorola Mobility LLC.
 * All Rights Reserved.
 * Motorola Mobility Confidential Restricted.
 */

#define DEBUG

#include <linux/cred.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>
#include <linux/netfilter.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <net/ip.h>

#include "xt_qtaguid_ext.h"
#include "xt_qtaguid_internal.h"

#define ROOT_UID 0
#define SYSTEM_UID 1000
#define MOBILE_IFACE "rmnet"
#define MOBILE_IFACE_LEN 5
#define UNKNOWN_CMDLINE "unknown"
#define MAX_CMD_LINE_LEN 128

#define pr_data_consume(fmt, hdr) \
	pr_debug("androidOS data consumed proto:%d %s_bytes:%d by %s"\
		" pid:%d uid:%d src: " fmt " dst: " fmt "\n", proto,\
		dir == IFS_TX ? "tx" : "rx", len, cmdline,\
		pid, uid, &hdr.saddr, &hdr.daddr)
#define pr_family_err(family) \
	pr_err("androidOS unknown NFPROTO:%d\n", family)
#define pr_proto_err(proto) \
	pr_err("androidOS unknown protocol:%d\n", proto)

struct data_consume_work {
	struct work_struct work;
	struct sock *sk;
	union {
		struct iphdr iph4;
		struct ipv6hdr iph6;
	} hdr;
	int dir;
	int proto;
	int len;
	uid_t uid;
	u8 family;
};

struct data_consumer_args {
	const struct sk_buff *skb;
	uid_t uid;
	int proto;
	int dir;
	int family;
	int debug;
};

static bool
check_sock_in_task(struct sock *sk, struct task_struct *task)
{
	struct files_struct *files = NULL;
	struct fdtable *fdt = NULL;
	struct file *file = NULL;
	struct socket *sock = NULL;
	struct fd f;
	int i;
	int err = 0;

	if (!sk || !task)
		goto err_ret;

	files = task->files;
	if (!files)
		goto err_ret;

	fdt = rcu_dereference_raw(files->fdt);

	for (i = 0; i < fdt->max_fds; i++) {
		file = rcu_dereference_raw(fdt->fd[i]);
		f = __to_fd((unsigned long)file);

		if (f.file) {
			sock = sock_from_file(f.file, &err);
			if (sock && sock->sk == sk)
				return true;
		}
	}

err_ret:
	return false;
}

static inline bool
equal_uid(struct task_struct *task, uid_t uid)
{
	kuid_t kuid = __task_cred(task)->uid;

	return from_kuid(&init_user_ns, kuid) == uid;
}

static void
work_handler(struct work_struct *w)
{
	char cmdline[MAX_CMD_LINE_LEN] = UNKNOWN_CMDLINE;
	struct data_consume_work *wrapper;
	struct task_struct *p;
	int proto;
	int dir;
	int len;
	uid_t uid;
	pid_t pid = -1;

	wrapper = container_of(w, struct data_consume_work, work);

	if (!wrapper)
		goto end;

	proto = wrapper->proto;
	dir = wrapper->dir;
	len = wrapper->len;
	uid =  wrapper->uid;

	for_each_process(p) {
		if (!equal_uid(p, uid))
			continue;

		if (check_sock_in_task(wrapper->sk, p)) {
			get_cmdline(p, cmdline, MAX_CMD_LINE_LEN);
			pid = p->pid;
			break;
		}

		if (uid != SYSTEM_UID && uid != ROOT_UID)
			break;
	}

	switch (wrapper->family) {
	case NFPROTO_IPV4:
		pr_data_consume("%pI4", (wrapper->hdr).iph4);
		break;
	case NFPROTO_IPV6:
		pr_data_consume("%pI6", (wrapper->hdr).iph6);
		break;
	default:
		pr_family_err(wrapper->family);
	}

end:
	kfree(wrapper);
}

void
pr_data_consumer(struct data_consumer_args *args)
{
	struct data_consume_work *work;
	struct iphdr *ip4h;
	struct ipv6hdr *ip6h;
	bool flag = false;

	if (args->debug & DEBUG_ALL_MASK) {
		flag = true;
	} else if ((args->debug & DEBUG_OS_MASK) &&
			(args->uid == SYSTEM_UID || args->uid == ROOT_UID)) {
		flag = true;
	}

	if (!flag)
		return;

	switch (args->proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_ICMP:
	case IPPROTO_IGMP:
	case IPPROTO_IP:
		work = kzalloc(sizeof(*work), GFP_ATOMIC);

		if (!work)
			return;

		INIT_WORK(&work->work, work_handler);

		if (args->family == NFPROTO_IPV4) {
			ip4h = ip_hdr(args->skb);
			if (!ip4h) {
				kfree(work);
				return;
			}
			memcpy(&work->hdr, ip4h, sizeof(*ip4h));
		} else if (args->family == NFPROTO_IPV6) {
			ip6h = ipv6_hdr(args->skb);
			if (!ip6h) {
				kfree(work);
				return;
			}
			memcpy(&work->hdr, ip6h, sizeof(*ip6h));
		}
		work->sk = args->skb->sk;
		work->dir = args->dir;
		work->proto = args->proto;
		work->len = args->skb->len;
		work->family = args->family;
		work->uid = args->uid;
		schedule_work(&work->work);
		break;
	default:
		pr_proto_err(args->proto);
	}
}

/*-----------------------------------------------------------*/
/* #define DEBUG_RECENT_OWNER */

#define RT_SIZE jhash_size(8)
#define SPORT(x) ((u_int16_t)x[0])
#define DPORT(x) ((u_int16_t)(x[0] >> 16))
#define ADDR_SIZE(family) ((family == NFPROTO_IPV4) ? 1 : 4)

struct recent_owner {
	uid_t uid;
#ifdef DEBUG_RECENT_OWNER
	union nf_inet_addr addr;	/* remote addr */
	__be16 sport;			/* src port */
	__be16 dport;			/* dst port */
#endif
};

static u_int32_t hash_rnd __read_mostly;
static struct recent_owner *table;
static DEFINE_SPINLOCK(recent_owner_lock);

/* These two functions below are copied from xt_qtaguid.c,
 * because 'extern' is not allowed by checkpatch script.
 */
static int ipx_proto(const struct sk_buff *skb,
		     struct xt_action_param *par)
{
	int thoff = 0, tproto;

	switch (par->family) {
	case NFPROTO_IPV6:
		tproto = ipv6_find_hdr(skb, &thoff, -1, NULL, NULL);
		break;
	case NFPROTO_IPV4:
		tproto = ip_hdr(skb)->protocol;
		break;
	default:
		tproto = IPPROTO_RAW;
	}
	return tproto;
}

static void get_dev_and_dir(const struct sk_buff *skb,
			    struct xt_action_param *par,
			    enum ifs_tx_rx *direction,
			    const struct net_device **el_dev)
{
	if (par->in) {
		*el_dev = par->in;
		*direction = IFS_RX;
	} else if (par->out) {
		*el_dev = par->out;
		*direction = IFS_TX;
	} else {
		pr_err("qtaguid[%d]: %s(): no par->in/out?!!\n",
		       par->hooknum, __func__);
	}
	if (unlikely(!(*el_dev)->name)) {
		pr_err("qtaguid[%d]: %s(): no dev->name?!!\n",
		       par->hooknum, __func__);
	}
	if (skb->dev && *el_dev != skb->dev) {
		MT_DEBUG("qtaguid[%d]: skb->dev=%p %s vs par->%s=%p %s\n",
			 par->hooknum, skb->dev, skb->dev->name,
			 *direction == IFS_RX ? "in" : "out",  *el_dev,
			 (*el_dev)->name);
	}
}

static inline bool
is_mobile_iface(const char *iface)
{
	return !memcmp(iface, MOBILE_IFACE, MOBILE_IFACE_LEN);
}

static inline bool
need_fix_uid(const char *iface, int proto)
{
	return is_mobile_iface(iface) && proto == IPPROTO_TCP;
}

static void
get_addr_and_port(const struct sk_buff *skb, int dir,
		  u16 family, u32 ap[])
{
	struct iphdr *iph4;
	struct ipv6hdr *iph6;
	const __be16 *pptr;
	__be16 _ports[2];

	pptr = skb_header_pointer(skb, ip_hdrlen(skb),
				  sizeof(_ports), _ports);
	if (!pptr)
		return;

	if (dir == IFS_TX)
		ap[0] = ntohs(pptr[0]) << 16 | ntohs(pptr[1]);
	else
		ap[0] = ntohs(pptr[1]) << 16 | ntohs(pptr[0]);

	if (family == NFPROTO_IPV4) {
		iph4 = ip_hdr(skb);
		if (dir == IFS_TX)
			ap[1] = iph4->daddr;
		else
			ap[1] = iph4->saddr;
	} else if (family == NFPROTO_IPV6) {
		iph6 = ipv6_hdr(skb);
		if (dir == IFS_TX)
			memcpy(&ap[1], &iph6->daddr, sizeof(iph6->daddr));
		else
			memcpy(&ap[1], &iph6->saddr, sizeof(iph6->saddr));
	}
}

static inline unsigned int
recent_owner_hash4(u32 ap[])
{
	return jhash_1word((__force __u32)(ap[0] ^ ap[1]), hash_rnd ^
			((__force __u16)ap[0] >> 16)) & (RT_SIZE - 1);
}

static inline unsigned int
recent_owner_hash6(u32 ap[])
{
	return jhash_1word((__force __u32)(ap[0] ^ ap[1] ^ ap[2] ^ ap[3] ^
			   ap[4]), hash_rnd ^ ((__force __u16)ap[0] >> 16)) &
			   (RT_SIZE - 1);
}

int
recent_owner_init(void)
{
	table = kcalloc(RT_SIZE, sizeof(struct recent_owner), GFP_ATOMIC);
	if (!table)
		return -ENOMEM;

	get_random_bytes(&hash_rnd, sizeof(hash_rnd));
	return 0;
}

static struct recent_owner *
__recent_owner_lookup(u32 ap[], u16 family)
{
	unsigned int idx;

	if (family == NFPROTO_IPV4)
		idx = recent_owner_hash4(ap);
	else if (family == NFPROTO_IPV6)
		idx = recent_owner_hash6(ap);

#ifdef DEBUG_RECENT_OWNER
	if (family == NFPROTO_IPV4) {
		pr_debug("qtaguid: %s addr(%pI4, %pI4) ",
			 __func__, &ap[1], &table[idx].addr.ip);
		pr_debug("sport(%d, %d) dport(%d, %d) uid:%d idx:%d\n",
			 SPORT(ap), (u_int16_t)table[idx].sport,
			 DPORT(ap), (u_int16_t)table[idx].dport,
			 table[idx].uid, idx);
	} else if (family == NFPROTO_IPV6) {
		pr_debug("qtaguid: %s addr(%pI6, %pI6) ",
			 __func__, &ap[1], &table[idx].addr.ip6);
		pr_debug("sport(%d, %d) dport(%d, %d) uid:%d idx:%d\n",
			 SPORT(ap), (u_int16_t)table[idx].sport,
			 DPORT(ap), (u_int16_t)table[idx].dport,
			 table[idx].uid, idx);
	}
#endif

	return &table[idx];
}

uid_t
recent_owner_lookup(const struct sk_buff *skb,
		    struct xt_action_param *par)
{
	struct recent_owner *entry;
	const struct net_device *net_dev = NULL;
	enum ifs_tx_rx dir = IFS_MAX_DIRECTIONS;
	uid_t uid = 0;
	int proto;
	u32 ap[5];

	proto = ipx_proto(skb, par);
	get_dev_and_dir(skb, par, &dir, &net_dev);

	if (dir == IFS_RX && need_fix_uid(net_dev->name, proto)) {
		get_addr_and_port(skb, dir, par->family, ap);
		spin_lock_bh(&recent_owner_lock);
		entry = __recent_owner_lookup(ap, par->family);
		uid = entry->uid;
		spin_unlock_bh(&recent_owner_lock);
	}

	return uid;
}

void
recent_owner_update(const struct sk_buff *skb,
		    struct xt_action_param *par,
		    uid_t uid,
		    int debug_os_data)
{
	struct recent_owner *entry;
	const struct net_device *net_dev = NULL;
	struct data_consumer_args args;
	enum ifs_tx_rx dir = IFS_MAX_DIRECTIONS;
	int proto;
	u32 ap[5];

	proto = ipx_proto(skb, par);
	get_dev_and_dir(skb, par, &dir, &net_dev);

	if (!unlikely(debug_os_data & DEFAULT_MASK)) {
		args.skb = skb;
		args.uid = uid;
		args.proto = proto;
		args.dir = dir;
		args.family = par->family;
		args.debug = debug_os_data;
		pr_data_consumer(&args);
	}

	if (uid == ROOT_UID ||
	    uid == SYSTEM_UID ||
	    dir != IFS_TX ||
	    !need_fix_uid(net_dev->name, proto))
		return;

	spin_lock_bh(&recent_owner_lock);
	get_addr_and_port(skb, IFS_TX, par->family, ap);
	entry = __recent_owner_lookup(ap, par->family);
	entry->uid = uid;

#ifdef DEBUG_RECENT_OWNER
	if (par->family == NFPROTO_IPV4) {
		if (entry->addr.ip == ap[1] &&
		    entry->sport == SPORT(ap) &&
		    entry->dport == DPORT(ap))
			goto end;
		pr_debug("qtaguid: %s addr(%pI4, %pI4) ",
			 __func__, &ap[1], &entry->addr.ip);
		pr_debug("sport(%d, %d) dport(%d, %d) uid(%d, %d)\n",
			 SPORT(ap), (u_int16_t)entry->sport,
			 DPORT(ap), (u_int16_t)entry->dport,
			 uid, entry->uid);
	} else if (par->family == NFPROTO_IPV6) {
		if (!memcmp(&entry->addr, &ap[1], sizeof(entry->addr)) &&
		    entry->sport == SPORT(ap) &&
		    entry->dport == DPORT(ap))
			goto end;
		pr_debug("qtaguid: %s addr(%pI6, %pI6) ",
			 __func__, &ap[1], &entry->addr.ip);
		pr_debug("sport(%d, %d) dport(%d, %d) uid(%d, %d)\n",
			 SPORT(ap), (u_int16_t)entry->sport,
			 DPORT(ap), (u_int16_t)entry->dport,
			 uid, entry->uid);
	}

	memcpy(&entry->addr, &ap[1], 4 * ADDR_SIZE(par->family));
	entry->sport = SPORT(ap);
	entry->dport = DPORT(ap);
end:
#endif
	spin_unlock_bh(&recent_owner_lock);
}
