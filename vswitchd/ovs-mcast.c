/* Copyright (c) 2008, 2009, 2010, 2011, 2012 IBM CRL.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "ovs-mcast.h"
#include "vlog.h"
#include "bridge.h"

VLOG_DEFINE_THIS_MODULE(vswitchd);

pthread_mutex_t mutex;
//extern static int bridge_update_bf_gdt(const struct bridge *br, struct bloom_filter *bf);

/**
 * send mcast msg.
 * @param group multicast group ip
 * @param port multicast port
 * @param msg content to send
 * @param len_msg length of the content
 * @return 0 if success
 */
void mc_send(struct mc_send_arg* arg)
{
    int sock_id;
    struct sockaddr_in addr;
    socklen_t len;
    int ret;
    struct mcast_msg *msg= malloc(sizeof(struct mcast_msg));
    struct bloom_filter *bf=NULL;

    if (!arg) {
        return ;
    }

    /* open a socket. only udp support multicast */
    if ((sock_id = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket error");
        return;
    }

    /* build address */
    memset((void*)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = arg->group_ip; 
    addr.sin_port = htons(arg->port);
    len = sizeof(addr);

    /* prepare message.*/
    msg->gid = arg->gdt->gid;

    struct dpif_dp_stats *s = malloc(32);

    bridge_get_stat(arg->br,s);
    VLOG_INFO("Get stat from dp.");
    msg->s.num = 1;
    msg->s.entry[0].src_sw_id = arg->br->local_id;
    msg->s.entry[0].dst_sw_id = arg->br->local_id; //TODO
    msg->s.entry[0].bytes = s->n_hit + s->n_missed;

    /* send the data to the address:port */
    while (!*arg->stop) {
        bf = bf_gdt_find_filter(arg->gdt,arg->local_id);
        if (bf) {//found matched bf
            pthread_mutex_lock (&mutex);
            memcpy(&(msg->bf),bf,sizeof(struct bloom_filter));
            pthread_mutex_unlock(&mutex);
            ret = sendto(sock_id,msg,sizeof(struct mcast_msg),0,(struct sockaddr *)&addr, len);
            if (ret <0) {
                perror("sendto error");
            }  else {
                VLOG_INFO("Send mcast msg to %s:%u with gid=%u,bf_id=%u\n", inet_ntoa(addr.sin_addr.s_addr), ntohs(addr.sin_port),msg->gid,msg->bf.bf_id);
            }
        }
        sleep(5);
    }

    if(msg) free(msg);
    close(sock_id);
    return;
}

/**
 * receive mcast msg, parse and store it.
 * @param group multicast group
 * @param port multicast port
 */
void mc_recv(struct mc_recv_arg* arg)
{
    int sock_id;
    struct sockaddr_in addr, sender;
    struct ip_mreq ipmr;
    socklen_t len;
    int i,ret;
    int count;
    struct mcast_msg *msg = malloc(sizeof(struct mcast_msg));

    /* Step 1: open a socket, and bind */
    if ((sock_id = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket error");
        return;
    }

    memset((void*)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(arg->port);

    if (bind(sock_id, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind error");
        return;
    }

    /* Step 2: fill in a struct ip_mreq */
    memset((void*)&ipmr, 0, sizeof(ipmr));
    ipmr.imr_multiaddr.s_addr = arg->group_ip; /* multicast group ip */
    ipmr.imr_interface.s_addr = htonl(INADDR_ANY);

    /* Step 3: call setsockopt with IP_ADD_MEMBERSHIP to support receiving multicast */
    if (setsockopt(sock_id, IPPROTO_IP, IP_ADD_MEMBERSHIP, &ipmr, sizeof(ipmr)) < 0) {
        perror("setsockopt:IP_ADD_MEMBERSHIP");
        return;
    }

    /* Step 4: call recvfrom to receive multicast packets */
    len = sizeof(sender);
    count = 0;
    while (!*arg->stop) {
        ret = recvfrom(sock_id, msg, sizeof(struct mcast_msg),0,(struct sockaddr *)&sender,&len);
        if (ret < 0) {
            perror("recvfrom error");
            continue;
        }
        VLOG_INFO("[%d] Receive mcast msg from %s:%d gid=%u.\n", count, inet_ntoa(sender.sin_addr.s_addr), ntohs(sender.sin_port),msg->gid);
        if (msg->gid != arg->gdt->gid){
            VLOG_WARN("group %u received mcast msg from other group %u\n",arg->gdt->gid,msg->gid);
            continue;
        }

        pthread_mutex_lock (&mutex);
        ret = bf_gdt_update_filter(arg->gdt,&msg->bf);
        pthread_mutex_unlock (&mutex);
        if(ret > 0) {//sth changed in gdt with msg
            VLOG_INFO("received new bf content from the mcast msg, should update the bf_gdt on dp.");
            bridge_update_bf_gdt(arg->br, &msg->bf);
        } else {
            VLOG_INFO("no new bf content, should ignore.");
        }
        count ++;
    }

    /* Step 5: call setsockopt with IP_DROP_MEMBERSHIP to drop from multicast */
    if (setsockopt(sock_id, IPPROTO_IP, IP_DROP_MEMBERSHIP, &ipmr, sizeof(ipmr)) < 0) {
        perror("setsockopt:IP_DROP_MEMBERSHIP");
        return;
    }

    /* Step 6: close the socket */
    close(sock_id);
    if (msg) free(msg);
    return;
}
