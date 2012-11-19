/*
 * Copyright (c) 2007-2012 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/openvswitch.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in6.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <net/dsfield.h>

#include "checksum.h"
#include "datapath.h"
#include "vlan.h"
#include "vport.h"
#include "remote.h"

static int do_execute_actions(struct datapath *dp, struct sk_buff *skb,
			const struct nlattr *attr, int len, bool keep_skb);

static int make_writable(struct sk_buff *skb, int write_len)
{
	if (!skb_cloned(skb) || skb_clone_writable(skb, write_len))
		return 0;

	return pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
}

/* remove VLAN header from packet and update csum accrodingly. */
static int __pop_vlan_tci(struct sk_buff *skb, __be16 *current_tci)
{
	struct vlan_hdr *vhdr;
	int err;

	err = make_writable(skb, VLAN_ETH_HLEN);
	if (unlikely(err))
		return err;

	if (get_ip_summed(skb) == OVS_CSUM_COMPLETE)
		skb->csum = csum_sub(skb->csum, csum_partial(skb->data
					+ ETH_HLEN, VLAN_HLEN, 0));

	vhdr = (struct vlan_hdr *)(skb->data + ETH_HLEN);
	*current_tci = vhdr->h_vlan_TCI;

	memmove(skb->data + VLAN_HLEN, skb->data, 2 * ETH_ALEN);
	__skb_pull(skb, VLAN_HLEN);

	vlan_set_encap_proto(skb, vhdr);
	skb->mac_header += VLAN_HLEN;
	skb_reset_mac_len(skb);

	return 0;
}

static int pop_vlan(struct sk_buff *skb)
{
	__be16 tci;
	int err;

	if (likely(vlan_tx_tag_present(skb))) {
		vlan_set_tci(skb, 0);
	} else {
		if (unlikely(skb->protocol != htons(ETH_P_8021Q) ||
			     skb->len < VLAN_ETH_HLEN))
			return 0;

		err = __pop_vlan_tci(skb, &tci);
		if (err)
			return err;
	}
	/* move next vlan tag to hw accel tag */
	if (likely(skb->protocol != htons(ETH_P_8021Q) ||
		   skb->len < VLAN_ETH_HLEN))
		return 0;

	err = __pop_vlan_tci(skb, &tci);
	if (unlikely(err))
		return err;

	__vlan_hwaccel_put_tag(skb, ntohs(tci));
	return 0;
}

static int push_vlan(struct sk_buff *skb, const struct ovs_action_push_vlan *vlan)
{
	if (unlikely(vlan_tx_tag_present(skb))) { /* already exist vlan tag. */
		u16 current_tag;

		/* push down current VLAN tag */
		current_tag = vlan_tx_tag_get(skb);

        /*put new vlan hdr*/
		if (!__vlan_put_tag(skb, current_tag)) 
			return -ENOMEM;

		if (get_ip_summed(skb) == OVS_CSUM_COMPLETE)
			skb->csum = csum_add(skb->csum, csum_partial(skb->data
					+ ETH_HLEN, VLAN_HLEN, 0));

	}
    /* Set the vlan_tci. */
	__vlan_hwaccel_put_tag(skb, ntohs(vlan->vlan_tci) & ~VLAN_TAG_PRESENT);
	return 0;
}

static int set_eth_addr(struct sk_buff *skb,
			const struct ovs_key_ethernet *eth_key)
{
	int err;
	err = make_writable(skb, ETH_HLEN);
	if (unlikely(err))
		return err;

	memcpy(eth_hdr(skb)->h_source, eth_key->eth_src, ETH_ALEN);
	memcpy(eth_hdr(skb)->h_dest, eth_key->eth_dst, ETH_ALEN);

	return 0;
}

static void set_ip_addr(struct sk_buff *skb, struct iphdr *nh,
				__be32 *addr, __be32 new_addr)
{
	int transport_len = skb->len - skb_transport_offset(skb);

	if (nh->protocol == IPPROTO_TCP) {
		if (likely(transport_len >= sizeof(struct tcphdr)))
			inet_proto_csum_replace4(&tcp_hdr(skb)->check, skb,
						 *addr, new_addr, 1);
	} else if (nh->protocol == IPPROTO_UDP) {
		if (likely(transport_len >= sizeof(struct udphdr))) {
			struct udphdr *uh = udp_hdr(skb);

			if (uh->check ||
			    get_ip_summed(skb) == OVS_CSUM_PARTIAL) {
				inet_proto_csum_replace4(&uh->check, skb,
							 *addr, new_addr, 1);
				if (!uh->check)
					uh->check = CSUM_MANGLED_0;
			}
		}
	}

	csum_replace4(&nh->check, *addr, new_addr);
	skb_clear_rxhash(skb);
	*addr = new_addr;
}

static void set_ip_ttl(struct sk_buff *skb, struct iphdr *nh, u8 new_ttl)
{
	csum_replace2(&nh->check, htons(nh->ttl << 8), htons(new_ttl << 8));
	nh->ttl = new_ttl;
}

static int set_ipv4(struct sk_buff *skb, const struct ovs_key_ipv4 *ipv4_key)
{
	struct iphdr *nh;
	int err;

	err = make_writable(skb, skb_network_offset(skb) +
				 sizeof(struct iphdr));
	if (unlikely(err))
		return err;

	nh = ip_hdr(skb);

	if (ipv4_key->ipv4_src != nh->saddr)
		set_ip_addr(skb, nh, &nh->saddr, ipv4_key->ipv4_src);

	if (ipv4_key->ipv4_dst != nh->daddr)
		set_ip_addr(skb, nh, &nh->daddr, ipv4_key->ipv4_dst);

	if (ipv4_key->ipv4_tos != nh->tos)
		ipv4_change_dsfield(nh, 0, ipv4_key->ipv4_tos);

	if (ipv4_key->ipv4_ttl != nh->ttl)
		set_ip_ttl(skb, nh, ipv4_key->ipv4_ttl);

	return 0;
}

/* Must follow make_writable() since that can move the skb data. */
static void set_tp_port(struct sk_buff *skb, __be16 *port,
			 __be16 new_port, __sum16 *check)
{
	inet_proto_csum_replace2(check, skb, *port, new_port, 0);
	*port = new_port;
	skb_clear_rxhash(skb);
}

static void set_udp_port(struct sk_buff *skb, __be16 *port, __be16 new_port)
{
	struct udphdr *uh = udp_hdr(skb);

	if (uh->check && get_ip_summed(skb) != OVS_CSUM_PARTIAL) {
		set_tp_port(skb, port, new_port, &uh->check);

		if (!uh->check)
			uh->check = CSUM_MANGLED_0;
	} else {
		*port = new_port;
		skb_clear_rxhash(skb);
	}
}

static int set_udp(struct sk_buff *skb, const struct ovs_key_udp *udp_port_key)
{
	struct udphdr *uh;
	int err;

	err = make_writable(skb, skb_transport_offset(skb) +
				 sizeof(struct udphdr));
	if (unlikely(err))
		return err;

	uh = udp_hdr(skb);
	if (udp_port_key->udp_src != uh->source)
		set_udp_port(skb, &uh->source, udp_port_key->udp_src);

	if (udp_port_key->udp_dst != uh->dest)
		set_udp_port(skb, &uh->dest, udp_port_key->udp_dst);

	return 0;
}

static int set_tcp(struct sk_buff *skb, const struct ovs_key_tcp *tcp_port_key)
{
	struct tcphdr *th;
	int err;

	err = make_writable(skb, skb_transport_offset(skb) +
				 sizeof(struct tcphdr));
	if (unlikely(err))
		return err;

	th = tcp_hdr(skb);
	if (tcp_port_key->tcp_src != th->source)
		set_tp_port(skb, &th->source, tcp_port_key->tcp_src, &th->check);

	if (tcp_port_key->tcp_dst != th->dest)
		set_tp_port(skb, &th->dest, tcp_port_key->tcp_dst, &th->check);

	return 0;
}

/**
 * Send packet out through given port.
 * @param dp: datapath to send out.
 * @param skb: packet to send out.
 * @param out_port: port to send out through.
 */
static int do_output(struct datapath *dp, struct sk_buff *skb, int out_port)
{
    pr_info("DP do_output(): will send out pkt through port=%u\n",out_port);
	struct vport *vport;

	if (unlikely(!skb))
		return -ENOMEM;

	vport = ovs_vport_rcu(dp, out_port);
	if (unlikely(!vport)) {
		kfree_skb(skb);
		return -ENODEV;
	}

	ovs_vport_send(vport, skb);
	return 0;
}

/**
 * Send pkt to userspace, will be processed by ovsd.
 */
static int output_userspace(struct datapath *dp, struct sk_buff *skb,
			    const struct nlattr *attr)
{
	struct dp_upcall_info upcall;
	const struct nlattr *a;
	int rem;

	upcall.cmd = OVS_PACKET_CMD_ACTION;
	upcall.key = &OVS_CB(skb)->flow->key;
	upcall.userdata = NULL;
	upcall.pid = 0;

	for (a = nla_data(attr), rem = nla_len(attr); rem > 0;
		 a = nla_next(a, &rem)) {
		switch (nla_type(a)) {
		case OVS_USERSPACE_ATTR_USERDATA:
			upcall.userdata = a;
			break;

		case OVS_USERSPACE_ATTR_PID:
			upcall.pid = nla_get_u32(a);
			break;
		}
	}

	return ovs_dp_upcall(dp, skb, &upcall);
}

static int sample(struct datapath *dp, struct sk_buff *skb,
		  const struct nlattr *attr)
{
	const struct nlattr *acts_list = NULL;
	const struct nlattr *a;
	int rem;

	for (a = nla_data(attr), rem = nla_len(attr); rem > 0;
		 a = nla_next(a, &rem)) {
		switch (nla_type(a)) {
		case OVS_SAMPLE_ATTR_PROBABILITY:
			if (net_random() >= nla_get_u32(a))
				return 0;
			break;

		case OVS_SAMPLE_ATTR_ACTIONS:
			acts_list = a;
			break;
		}
	}

	return do_execute_actions(dp, skb, nla_data(acts_list),
						 nla_len(acts_list), true);
}

#ifdef LC_ENABLE
#ifndef ETH_IP_HLEN
#define ETH_IP_HLEN 2
#endif
#ifndef IP_HLEN
#define IP_HLEN 20
#endif
/**
 * Add a encapulation header.
 */
static int do_remote_encapulation(struct datapath *dp, struct sk_buff *skb, unsigned int dst_ip)
{
#ifdef DEBUG
    pr_info("DP do_remote_encapulation()\n");
#endif
    /*put new ether+ip+ether_ip hdr*/
    if (!__remote_encapulation(dp, skb, dst_ip)) 
        return -1;

    /*calculate right sum here.*/
    if (get_ip_summed(skb) == OVS_CSUM_COMPLETE)
        skb->csum = csum_add(skb->csum, csum_partial(skb->data, ETH_HLEN+IP_HLEN+ETH_IP_HLEN, 0));
    return 0;
}

static int do_remote_decapulation(struct sk_buff *skb)
{
#ifdef DEBUG
    pr_info("DP do_remote_decapulation()\n");
#endif
    if (!__remote_decapulation(skb)) 
        return -1;

    return 0;
}

int ovs_execute_decapulation(struct sk_buff *skb)
{
    return do_remote_decapulation(skb);
}
#endif

static int execute_set_action(struct sk_buff *skb,
        const struct nlattr *nested_attr)
{
    int err = 0;

    switch (nla_type(nested_attr)) {
        case OVS_KEY_ATTR_PRIORITY:
            skb->priority = nla_get_u32(nested_attr);
            break;

        case OVS_KEY_ATTR_TUN_ID:
            OVS_CB(skb)->tun_id = nla_get_be64(nested_attr);
            break;

        case OVS_KEY_ATTR_ETHERNET:
            err = set_eth_addr(skb, nla_data(nested_attr));
            break;

        case OVS_KEY_ATTR_IPV4:
            err = set_ipv4(skb, nla_data(nested_attr));
            break;

        case OVS_KEY_ATTR_TCP:
            err = set_tcp(skb, nla_data(nested_attr));
            break;

        case OVS_KEY_ATTR_UDP:
            err = set_udp(skb, nla_data(nested_attr));
            break;
    }

    return err;
}

/* Execute a list of actions against 'skb'. */
static int do_execute_actions(struct datapath *dp, struct sk_buff *skb,
        const struct nlattr *attr, int len, bool keep_skb)
{
    /* Every output action needs a separate clone of 'skb', but the common
     * case is just a single output action, so that doing a clone and
     * then freeing the original skbuff is wasteful.  So the following code
     * is slightly obscure just to avoid that. */
    int prev_port = -1;
    const struct nlattr *a;
    int rem;
    unsigned int remote_ip;
#ifdef LC_ENABLE
    uint64_t payload;
#endif

    /*nla_for_each_attr*/
    for (a = attr, rem = len; rem > 0; a = nla_next(a, &rem)) { 
        int err = 0;

        if (prev_port != -1) {
            do_output(dp, skb_clone(skb, GFP_ATOMIC), prev_port);
            prev_port = -1;
        }

        switch (nla_type(a)) {
            case OVS_ACTION_ATTR_OUTPUT: //send output through port
                prev_port = nla_get_u32(a);
                break;

#ifdef LC_ENABLE
            case OVS_ACTION_ATTR_REMOTE: //TODO: print to test here.
                payload = nla_get_u64(a);
                remote_ip = payload & 0xffffffff; //remote_ip
                prev_port = (payload >>32) & 0xffffffff; //port_no
#ifdef DEBUG
                pr_info(">>>DP will execute remote cmd, encap first: oport=%u, ip=0x%x\n",prev_port,remote_ip);
#endif
                do_remote_encapulation(dp,skb,remote_ip); //encapulate with new l2 and l3 header
                break;
#endif
            case OVS_ACTION_ATTR_USERSPACE:
                output_userspace(dp, skb, a);
                break;

            case OVS_ACTION_ATTR_PUSH_VLAN:
                err = push_vlan(skb, nla_data(a));
                if (unlikely(err)) /* skb already freed. */
                    return err;
                break;

            case OVS_ACTION_ATTR_POP_VLAN:
                err = pop_vlan(skb);
                break;

            case OVS_ACTION_ATTR_SET:
                err = execute_set_action(skb, nla_data(a));
                break;

            case OVS_ACTION_ATTR_SAMPLE:
                err = sample(dp, skb, a);
                break;
		}

		if (unlikely(err)) {
			kfree_skb(skb);
			return err;
		}
	}

	if (prev_port != -1) {
		if (keep_skb) skb = skb_clone(skb, GFP_ATOMIC);
		do_output(dp, skb, prev_port);
	} else if (!keep_skb)
		consume_skb(skb);

	return 0;
}

/* We limit the number of times that we pass into execute_actions()
 * to avoid blowing out the stack in the event that we have a loop. */
#define MAX_LOOPS 5

struct loop_counter {
	u8 count;		/* Count. */
	bool looping;		/* Loop detected? */
};

static DEFINE_PER_CPU(struct loop_counter, loop_counters);

static int loop_suppress(struct datapath *dp, struct sw_flow_actions *actions)
{
	if (net_ratelimit())
		pr_warn("%s: flow looped %d times, dropping\n",
				ovs_dp_name(dp), MAX_LOOPS);
	actions->actions_len = 0;
	return -ELOOP;
}

/* Execute a list of actions against 'skb'. */
int ovs_execute_actions(struct datapath *dp, struct sk_buff *skb)
{
	struct sw_flow_actions *acts = rcu_dereference(OVS_CB(skb)->flow->sf_acts);
	struct loop_counter *loop;
	int error;

	/* Check whether we've looped too much. */
    loop = &__get_cpu_var(loop_counters);
    if (unlikely(++loop->count > MAX_LOOPS))
        loop->looping = true;
    if (unlikely(loop->looping)) {
        error = loop_suppress(dp, acts);
        kfree_skb(skb);
        goto out_loop;
    }

#ifdef LC_ENABLE
    OVS_CB(skb)->encaped = 0;
#endif
    OVS_CB(skb)->tun_id = 0;

    error = do_execute_actions(dp, skb, acts->actions,
            acts->actions_len, false);

	/* Check whether sub-actions looped too much. */
	if (unlikely(loop->looping))
		error = loop_suppress(dp, acts);

out_loop:
	/* Decrement loop counter. */
	if (!--loop->count)
		loop->looping = false;

	return error;
}
