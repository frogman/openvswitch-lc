/*
 * Copyright (c) 2007-2011 IBM CRL.
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

#include <linux/if_vlan.h>
#include <linux/skbuff.h>

#include "datapath.h"
#include "remote.h"


int __remote_encapulation(struct datapath *dp, struct sk_buff *skb, int *dst_ip)
{
    struct ether_header *eth;
    struct iphdr *iph;

	if (skb_cow_head(skb, ETH_HLEN+IP_HLEN) < 0) { //make enough room at the head, new ether and ip header
		kfree_skb(skb);
		return -1;
	}

    /*add new ip header*/
    iph = (struct iphdr *)skb_push(skb,IP_HLEN);
    iph->ihl=5;
    iph->version=4;
    iph->tos=0;
    iph->tot_len = sizeof(struct iphdr)+skb->len;
    iph->id = htonl(12345);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_UDP;
    iph->saddr=dp->local_ip;
    iph->daddr=*dst_ip;
    iph->check=0; //csum here

    /*add new ethernet header*/
    eth = (struct ether_header *)skb_push(skb,ETH_HLEN);
	memcpy(skb->data, skb->data + IP_HLEN + ETH_HLEN, ETH_HLEN);

    skb->mac_header -= (IP_HLEN+ETH_HLEN);

    OVS_CB(skb)->encaped = 1;
    //csum?
    return 0;
}
int __remote_decapulation(struct sk_buff *skb)
{
    return 0;
}
