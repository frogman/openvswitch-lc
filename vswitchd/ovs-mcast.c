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
#include <sys/socket.h>
#include <netinet/in.h>
#include "ovs-mcast.h"

/**
 * send mcast msg.
 * @param group multicast group ip
 * @param port multicast port
 * @param buf content to send
 * @param len_buf length of the content
 * @return 0 if success
 */
int mc_send(char *group,u32 port, struct mcast_msg *buf, int len_buf)
{
    int sock_id;
    struct sockaddr_in addr;
    socklen_t len;
    int ret;
    char *group_ip = group;

    if (!buf) {
        return 0;
    }

    /* open a socket. only udp support multicast */
    if ((sock_id = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket error");
        return -1;
    }

    /* build address */
    memset((void*)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(group_ip); /* multicast group ip */ 
    addr.sin_port = htons(port);

    len = sizeof(addr);
    /* it's very easy, just send the data to the address:port */
    printf("Send to %s:%u with %lu:%s\n", inet_ntoa(addr.sin_addr.s_addr), ntohs(addr.sin_port),buf->ovsd_ip, buf->bf_array);
    ret = sendto(sock_id, buf, len_buf, 0, (struct sockaddr *)&addr, len);
    if (ret < 0) {
        perror("sendto error");
        return -1;
    }
    
    close(sock_id);
    return 0;
}

/**
 * receive mcast msg, parse and store it.
 * @param group multicast group
 * @param port multicast port
 * @return 0 if success
 */
int mc_recv(char *group,u32 port)
{
    int sock_id;
    struct sockaddr_in addr, sender;
    struct ip_mreq ipmr;
    socklen_t len;
    int ret;
    int count;
    char *group_ip = group;
    struct mcast_msg *buf = malloc(sizeof(struct mcast_msg));

    /* Step 1: open a socket, and bind */
    if ((sock_id = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket error");
        return -1;
    }

    memset((void*)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock_id, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind error");
        return -1;
    }

    /* Step 2: fill in a struct ip_mreq */
    memset((void*)&ipmr, 0, sizeof(ipmr));
    ipmr.imr_multiaddr.s_addr = inet_addr(group_ip); /* multicast group ip */
    ipmr.imr_interface.s_addr = htonl(INADDR_ANY);

    /* Step 3: call setsockopt with IP_ADD_MEMBERSHIP to support receiving multicast */
    if (setsockopt(sock_id, IPPROTO_IP, IP_ADD_MEMBERSHIP, &ipmr, sizeof(ipmr)) < 0) {
        perror("setsockopt:IP_ADD_MEMBERSHIP");
        if (errno == EBADF)
            printf("EBADF\n");
        else if (errno == EFAULT)
            printf("EFAULT\n");
        else if (errno == EINVAL)
            printf("EINVAL\n");
        else if (errno == ENOPROTOOPT)
            printf("ENOPROTOOPT\n");
        else if (errno == ENOTSOCK)
            printf("ENOTSOCK\n");
        return -1;
    }

    /* Step 4: call recvfrom to receive multicast packets */
    len = sizeof(sender);
    count = 0;
    while (1) {
        ret = recvfrom(sock_id, buf, sizeof(struct mcast_msg), 0, (struct sockaddr *)&sender, &len);
        //buf[ret] = '\0';
        if (ret < 0) {
            perror("recvfrom error");
            return -1;
        }
        printf("[%d] Receive from %s:%u with %lu:%s\n", count, inet_ntoa(sender.sin_addr.s_addr), ntohs(sender.sin_port),buf->ovsd_ip,buf->bf_array);
        count ++;
    }

    /* Step 5: call setsockopt with IP_DROP_MEMBERSHIP to drop from multicast */
    if (setsockopt(sock_id, IPPROTO_IP, IP_DROP_MEMBERSHIP, &ipmr, sizeof(ipmr)) < 0) {
        perror("setsockopt:IP_DROP_MEMBERSHIP");
        return -1;
    }

    /* Step 6: close the socket */
    close(sock_id);
    free(buf);

    return 0;
}
