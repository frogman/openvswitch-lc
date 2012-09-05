/*
 * Copyright (c) 2007-2012 IBM CRL.
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

#ifdef __KERNEL__
#include<linux/slab.h>
#include<linux/module.h>
#include<linux/init.h>
#include<linux/kernel.h>
#else
#include<stdarg.h>
#include<stdlib.h>
#endif

#include "bf.h"

#define SETBIT(array, i) (array[i/sizeof(char)] |= (1<<(i%sizeof(char))))
#define GETBIT(array, i) (array[i/sizeof(char)] & (1<<(i%sizeof(char))))

/**
 * hash function: SAX.
 */
u32 sax_hash(const char *key)
{
    u32 h=0;
    while(*key) h^=(h<<5)+(h>>2)+(unsigned char)*key++;
    return h;
}

/**
 * hash function: sdbm.
 */
u32 sdbm_hash(const char *key)
{
    u32 h=0;
    while(*key) h=(unsigned char)*key++ + (h<<6) + (h<<16) - h;
    return h;
}

hashfunc_t hashFunctions[] = {sax_hash,sdbm_hash};

/**
 * Create array bloom_filter.
 * @param dp_id: dp_id for the bloom_filter
 * @param len: number of the bits in the bloom_filter
 * @param nfuncs: number of the hash functions
 * @return: Pointer to the created bloom_filter. NULL if failed.
 */
struct bloom_filter *bf_create(u32 dp_id, u32 len, u32 nfuncs)
{
    struct bloom_filter *bf;
    int i;
    
#ifdef __KERNEL__
    if(!(bf=kmalloc(sizeof(struct bloom_filter),GFP_KERNEL))) {
#else
    if(!(bf=malloc(sizeof(struct bloom_filter)))) {
#endif
        /*printk("Error in kmalloc bf\i");*/
        return NULL;
    }
#ifdef __KERNEL__
    if(!(bf->array=kcalloc((len+sizeof(char)-1)/sizeof(char), sizeof(char),GFP_KERNEL))) {
        /*printk("Error in kcalloc bf\i");*/
        kfree(bf);
#else
    if(!(bf->array=calloc((len+sizeof(char)-1)/sizeof(char), sizeof(char)))) {
        free(bf);
#endif
        return NULL;
    }
#ifdef __KERNEL__
    if(!(bf->funcs=(hashfunc_t*)kmalloc(nfuncs*sizeof(hashfunc_t),GFP_KERNEL))) {
        kfree(bf->array);
        kfree(bf);
#else
    if(!(bf->funcs=(hashfunc_t*)malloc(nfuncs*sizeof(hashfunc_t)))) {
        free(bf->array);
        free(bf);
#endif
        /*printk("Error in kmalloc hashfuncs\i");*/
        return NULL;
    }

    for(i=0; i<nfuncs; ++i) {
        bf->funcs[i]=hashFunctions[i];
    }

    bf->dp_id = dp_id;
    bf->len=len;
    bf->nfuncs=nfuncs;

    return bf;
}

/**
 * destroy a bloom_filter.
 * @param bf: the bloom_filter to destroy
 */
int bf_destroy(struct bloom_filter *bf)
{
#ifdef __KERNEL__
    kfree(bf->array);
    kfree(bf->funcs);
    kfree(bf);
#else
    free(bf->array);
    free(bf->funcs);
    free(bf);
#endif
    return 0;
}

/**
 * add a new string to bf
 * @param bf: the bloom_filter to update
 * @param s: the string to add
 */
int bf_add(struct bloom_filter *bf, const char *s)
{
    u32 i;

    for(i=0; i<bf->nfuncs; ++i) {
        SETBIT(bf->array, bf->funcs[i](s)%bf->len);
    }

    return 0;
}

/**
 * test if s is in bf.
 * @param bf: the bloom_filter to test
 * @param s: the string to test
 * @return 1 if true
 */
int bf_check(struct bloom_filter *bf, const char *s)
{
    u32 i;

    if (!bf || !bf->array)
        return 0;

    for(i=0; i<bf->nfuncs; ++i) {
        if(!(GETBIT(bf->array, bf->funcs[i](s)%bf->len))) return 0;
    }

    return 1;
}
