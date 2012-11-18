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
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <asm/checksum_32.h>

#include "datapath.h"
#include "remote.h"

#ifndef ETH_IP_HLEN
#define ETH_IP_HLEN 2
#endif
#ifndef IP_HLEN
#define IP_HLEN 20
#endif

/**
 * add new mac header and ip header.
 */
int __remote_encapulation(struct datapath *dp, struct sk_buff *skb, int dst_ip)
{
    struct ethhdr *eth;
    struct iphdr *iph;

    if (skb_cow_head(skb, ETH_HLEN+ETH_IP_HLEN+IP_HLEN) < 0) { //make enough room at the head, new ether and ip header
        kfree_skb(skb);
        return -1;
    }

    /*add new eth_ip header*/
    skb_push(skb,ETH_IP_HLEN);
    memset(skb->data,0,ETH_IP_HLEN);

    /*add new ip header*/
    iph = (struct iphdr *)skb_push(skb,IP_HLEN);
    iph->ihl=5;
    iph->version=4;
    iph->tos=0;
    iph->tot_len = htons(sizeof(struct iphdr)+skb->len);
    iph->id = htonl(0);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = LC_REMOTE_IP_PROTO;
    iph->saddr=htonl(dp->local_ip);
    iph->daddr=htonl(dst_ip);
    iph->check=ip_fast_csum(iph,iph->ihl); //csum here

    /*add new ethernet header*/
    eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);
    memcpy(skb->data, skb->data+ETH_IP_HLEN+ETH_HLEN+IP_HLEN, ETH_HLEN);
    eth->h_proto = htons(ETH_P_IP); //ip packet

    skb->mac_header -= (ETH_IP_HLEN+IP_HLEN+ETH_HLEN);

    return 0;
}

/**
 * decapulate the mac header and ip header.
 */
int __remote_decapulation(struct sk_buff *skb)
{
    struct ethhdr *eth = (struct ethhdr *)skb_pull(skb,ETH_HLEN);
    if (eth->h_proto != htons(ETH_P_IP)) {
#ifdef DEBUG
        pr_info("__remote_decapulation() no ip proto existed?\n");
#endif
        return -1;
    }
    struct iphdr *iph = (struct iphdr *)skb_pull(skb,IP_HLEN);
    return 0;
}
