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

#ifndef VSWITCHD_MCAST_H
#define VSWITCHD_MCAST_H

#ifndef LC_BF_DFT_LEN
#define LC_BF_DFT_LEN 1024
#endif

typedef unsigned int u32;

struct mcast_msg {
    unsigned long ovsd_ip;
    unsigned char bf_array[(LC_BF_DFT_LEN+sizeof(char)-1)/sizeof(char)];
};

/**
 * 224.0.0.* reserved
 * 224.0.1.* public multicast for internet
 * 224.0.2.0 ~ 238.255.255.255 temporary multicast for users
 * 239.*.*.* local multicast
 */
int mc_send(char *group,u32 port,struct mcast_msg *buf, int len_buf);
int mc_recv(char *group,u32 port);

#endif
