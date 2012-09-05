/*
 * Copyright (c) 2012-2015 IBM CRL
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

#ifndef DP_DCM_H
#define DP_DCM_H 1

#include <linux/skbuff.h>

#include "vport.h"

//int ovs_dcm_init(void);

void ovs_dcm_process_received_packet(struct vport *p, struct sk_buff *skb);
#endif /* dcm.h */
