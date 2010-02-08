/*
 *  $Id: libnet_pblock.c,v 1.14 2004/11/09 07:05:07 mike Exp $
 *
 *  libnet
 *  libnet_pblock.c - Memory protocol block routines.
 *
 *  Copyright (c) 1998 - 2004 Mike D. Schiffman <mike@infonexus.com>
 *  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#if (HAVE_CONFIG_H)
#include "../include/config.h"
#endif
#if (!(_WIN32) || (__CYGWIN__)) 
#include "../include/libnet.h"
#else
#include "../include/win32/libnet.h"
#endif
#include <assert.h>

libnet_pblock_t *
libnet_pblock_probe(libnet_t *l, libnet_ptag_t ptag, uint32_t b_len, uint8_t type)
{
    int offset;
    libnet_pblock_t *p;

    if (ptag == LIBNET_PTAG_INITIALIZER)
    {
        return libnet_pblock_new(l, b_len);
    }

    /*
     *  Update this pblock, don't create a new one.  Note that if the
     *  new packet size is larger than the old one we will do a malloc.
     */
    p = libnet_pblock_find(l, ptag);

    if (p == NULL)
    {
        /* err msg set in libnet_pblock_find() */
        return (NULL);
    }
    if (p->type != type)
    {
        snprintf(l->err_buf, LIBNET_ERRBUF_SIZE,
                "%s(): ptag refers to different type than expected (0x%x != 0x%x)",
                __func__, p->type, type);
        return (NULL); 
    }
    /*
     *  If size is greater than the original block of memory, we need 
     *  to malloc more memory.  Should we use realloc?
     */
    if (b_len > p->b_len)
    {
        offset = b_len - p->b_len;  /* how many bytes larger new pblock is */
        free(p->buf);
        p->buf = malloc(b_len);
        if (p->buf == NULL)
        {
            snprintf(l->err_buf, LIBNET_ERRBUF_SIZE,
                    "%s(): can't resize pblock buffer: %s\n", __func__,
                    strerror(errno));
            return (NULL);
        }
        memset(p->buf, 0, b_len);
        p->h_len += offset; /* new length for checksums */
        p->b_len = b_len;       /* new buf len */
        l->total_size += offset;
    }
    else
    {
        offset = p->b_len - b_len;
        p->h_len -= offset; /* new length for checksums */
        p->b_len = b_len;       /* new buf len */
        l->total_size -= offset;
    }
    p->copied = 0;      /* reset copied counter */

    return (p);
}

static void* zmalloc(libnet_t* l, uint32_t size, const char* func)
{
    void* v = malloc(size);
    if(v)
        memset(v, 0, size);
    else
        snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, "%s(): malloc(): %s\n", func, 
                strerror(errno));
    return v;
}

libnet_pblock_t *
libnet_pblock_new(libnet_t *l, uint32_t b_len)
{
    libnet_pblock_t *p = zmalloc(l, sizeof(libnet_pblock_t), __func__);
    if(!p)
        return NULL;

    p->buf = zmalloc(l, b_len, __func__);

    if(!p->buf)
    {
        free(p);
        return NULL;
    }

    p->b_len = b_len;

    l->total_size += b_len;
    l->n_pblocks++;

    /* make the head node if it doesn't exist */
    if (l->protocol_blocks == NULL)
    {
        l->protocol_blocks = p;
        l->pblock_end = p;
    }
    else
    {
        l->pblock_end->next = p;
        p->prev = l->pblock_end;
        l->pblock_end = p;
    }

    return p;
}

int
libnet_pblock_swap(libnet_t *l, libnet_ptag_t ptag1, libnet_ptag_t ptag2)
{
    libnet_pblock_t *p1, *p2;

    p1 = libnet_pblock_find(l, ptag1);
    p2 = libnet_pblock_find(l, ptag2);
    if (p1 == NULL || p2 == NULL)
    {
        /* error set elsewhere */
        return (-1);
    }

    p2->prev = p1->prev;
    p1->next = p2->next;
    p2->next = p1;
    p1->prev = p2;

    if (p1->next)
    {
        p1->next->prev = p1;
    }

    if (p2->prev)
    {
        p2->prev->next = p2;
    }
    else
    {
        /* first node on the list */
        l->protocol_blocks = p2;
    }

    if (l->pblock_end == p2)
    {
        l->pblock_end = p1;
    }
    return (1);
}

static void libnet_pblock_remove_from_list(libnet_t *l, libnet_pblock_t *p)
{
    if (p->prev) 
    {
        p->prev->next = p->next;
    }
    else
    {
        l->protocol_blocks = p->next;
    }

    if (p->next)
    {
        p->next->prev = p->prev;
    }
    else
    {
        l->pblock_end = p->prev;
    }
}

int
libnet_pblock_insert_before(libnet_t *l, libnet_ptag_t ptag1,
        libnet_ptag_t ptag2)
{
    libnet_pblock_t *p1, *p2;

    p1 = libnet_pblock_find(l, ptag1);
    p2 = libnet_pblock_find(l, ptag2);
    if (p1 == NULL || p2 == NULL)
    {
        /* error set elsewhere */
        return (-1);
    }

    /* check for already present before */
    if(p2->next == p1)
        return 1;

    libnet_pblock_remove_from_list(l, p2);

    /* insert p2 into list */
    p2->prev = p1->prev;
    p2->next = p1;
    p1->prev = p2;

    if (p2->prev)  
    {
        p2->prev->next = p2;
    }
    else
    {
        /* first node on the list */
        l->protocol_blocks = p2;
    }
    
    return (1);
}

libnet_pblock_t *
libnet_pblock_find(libnet_t *l, libnet_ptag_t ptag)
{
    libnet_pblock_t *p;

    for (p = l->protocol_blocks; p; p = p->next)
    {
        if (p->ptag == ptag)
        {
            return (p); 
        }
    }
    snprintf(l->err_buf, LIBNET_ERRBUF_SIZE,
            "%s(): couldn't find protocol block\n", __func__);
    return (NULL);
}

int
libnet_pblock_append(libnet_t *l, libnet_pblock_t *p, const uint8_t *buf,
            uint32_t len)
{
    if (p->copied + len > p->b_len)
    {
        snprintf(l->err_buf, LIBNET_ERRBUF_SIZE,
                "%s(): memcpy would overflow buffer\n", __func__);
        return (-1);
    }
    memcpy(p->buf + p->copied, buf, len);
    p->copied += len;
    return (1);
}

void
libnet_pblock_setflags(libnet_pblock_t *p, uint8_t flags)
{
    p->flags = flags;
}

/* FIXME both ptag setting and end setting should be done in pblock new and/or pblock probe. */
libnet_ptag_t
libnet_pblock_update(libnet_t *l, libnet_pblock_t *p, uint32_t h_len, uint8_t type)
{
    p->type  =  type;
    p->ptag  =  ++(l->ptag_state);
    p->h_len = h_len;
    l->pblock_end = p;              /* point end of pblock list here */

    return (p->ptag);
}

int
libnet_pblock_coalesce(libnet_t *l, uint8_t **packet, uint32_t *size)
{
    /*
     *  Determine the offset required to keep memory aligned (strict
     *  architectures like solaris enforce this, but's a good practice
     *  either way).  This is only required on the link layer with the
     *  14 byte ethernet offset (others are similarly unkind).
     */
    if (l->injection_type == LIBNET_LINK || 
        l->injection_type == LIBNET_LINK_ADV)
    {
        /* 8 byte alignment should work */
        l->aligner = 8 - (l->link_offset % 8);
    }
    else
    {
        l->aligner = 0;
    }

    *packet = malloc(l->aligner + l->total_size);
    if (*packet == NULL)
    {
        snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, "%s(): malloc(): %s\n",
                __func__, strerror(errno));
        return (-1);
    }

    memset(*packet, 0, l->aligner + l->total_size);

    if (l->injection_type == LIBNET_RAW4 && 
        l->pblock_end->type == LIBNET_PBLOCK_IPV4_H)
    {
        libnet_pblock_setflags(l->pblock_end, LIBNET_PBLOCK_DO_CHECKSUM); 
    }
    
    /* additional sanity checks to perform if we're not in advanced mode */
    if (!(l->injection_type & LIBNET_ADV_MASK))
    {
    	switch (l->injection_type)
    	{
            case LIBNET_LINK:
                if ((l->pblock_end->type != LIBNET_PBLOCK_TOKEN_RING_H) &&
                    (l->pblock_end->type != LIBNET_PBLOCK_FDDI_H)       &&
                    (l->pblock_end->type != LIBNET_PBLOCK_ETH_H)        &&
                    (l->pblock_end->type != LIBNET_PBLOCK_802_1Q_H)     &&
                    (l->pblock_end->type != LIBNET_PBLOCK_ISL_H)        &&
                    (l->pblock_end->type != LIBNET_PBLOCK_802_3_H))
                {
                    snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, 
                    "%s(): packet assembly cannot find a layer 2 header\n",
                    __func__);
                    goto err;
                }
                break;
            case LIBNET_RAW4:
                if ((l->pblock_end->type != LIBNET_PBLOCK_IPV4_H))
                {
                    snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, 
                    "%s(): packet assembly cannot find an IPv4 header\n",
                     __func__);
                    goto err;
                }
                break;
            case LIBNET_RAW6:
                if ((l->pblock_end->type != LIBNET_PBLOCK_IPV6_H))
                {
                    snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, 
                    "%s(): packet assembly cannot find an IPv6 header\n",
                     __func__);
                    goto err;
                }
                break;
            default:
                /* we should not end up here ever */
                snprintf(l->err_buf, LIBNET_ERRBUF_SIZE, 
                "%s(): suddenly the dungeon collapses -- you die\n",
                 __func__);
                goto err;
            break;
        }
    }

    /* Build packet from end to start. */
    {
        /*
           From top to bottom, go through pblocks pairwise:

           q, p   q is prev to p, where prev is "later in the buffer"
           n      offset from start of packet to beginning of block we are writing

           q is NULL on first iteration
           p is NULL on last iteration

           It seems that checksums are done on q, I "think" this might be because
           tcp/udp need to have the ip header written so they can read it for
           their checksum.

           ===> But, when there is an IP options block, how would that work? The
           tcp checksum will be built before the ip header has been written, and
           will use the wrong ip header ip_src... it will be just random data.

           I need to prove this... and then rewrite this to be two loops, once copying
           pblocks across, another writing the checksums.
           */
        libnet_pblock_t *q = NULL;
        libnet_pblock_t *p = NULL;
        uint32_t n;

        for (n = l->aligner + l->total_size, p = l->protocol_blocks; p || q; )
        {
            if (q)
            {
                p = p->next;
            }
            if (p)
            {
                n -= p->b_len;
                /* copy over the packet chunk */
                memcpy(*packet + n, p->buf, p->b_len);
            }
            if (q)
            {
                if (p == NULL || (p->flags & LIBNET_PBLOCK_DO_CHECKSUM))
                {
                    if (q->flags & LIBNET_PBLOCK_DO_CHECKSUM)
                    {
                        uint32_t c;
                        /* offset is from beg of packet, forward to the IP header (
                         * ip_offset is from end of packet) */
                        int offset = (l->total_size + l->aligner) - q->ip_offset;
                        c = libnet_do_checksum(l, *packet + offset,
                                libnet_pblock_p2p(q->type), q->h_len);
                        if (c == -1)
                        {
                            /* err msg set in libnet_do_checksum() */
                            goto err;
                        }
                    }
                    q = p;
                }
            }
            else
            {
                q = p;
            }
        }
    }
    *size = l->aligner + l->total_size;

    /*
     *  Set the packet pointer to the true beginning of the packet and set
     *  the size for transmission.
     */
    if ((l->injection_type == LIBNET_LINK ||
        l->injection_type == LIBNET_LINK_ADV) && l->aligner)
    {
        *packet += l->aligner;
        *size -= l->aligner;
    }
    return (1);

err:
    free(*packet);
    *packet = NULL;
    return (-1);
}

void
libnet_pblock_delete(libnet_t *l, libnet_pblock_t *p)
{
    if (p)
    {
        l->total_size -= p->b_len;
        l->n_pblocks--;

        libnet_pblock_remove_from_list(l, p);

        if (p->buf)
        {
            free(p->buf);
            p->buf = NULL;
        }

        free(p);
    }
}

int
libnet_pblock_p2p(uint8_t type)
{
    /* for checksum; return the protocol number given a pblock type*/
    switch (type)
    {
        case LIBNET_PBLOCK_CDP_H:
            return (LIBNET_PROTO_CDP);
        case LIBNET_PBLOCK_ICMPV4_H:
        case LIBNET_PBLOCK_ICMPV4_ECHO_H:
        case LIBNET_PBLOCK_ICMPV4_MASK_H:
        case LIBNET_PBLOCK_ICMPV4_UNREACH_H:
        case LIBNET_PBLOCK_ICMPV4_TIMXCEED_H:
        case LIBNET_PBLOCK_ICMPV4_REDIRECT_H:
        case LIBNET_PBLOCK_ICMPV4_TS_H:
            return (IPPROTO_ICMP);
        case LIBNET_PBLOCK_IGMP_H:
            return (IPPROTO_IGMP);
        case LIBNET_PBLOCK_IPV4_H:
            return (IPPROTO_IP);
        case LIBNET_ISL_H:
            return (LIBNET_PROTO_ISL);
        case LIBNET_PBLOCK_OSPF_H:
            return (IPPROTO_OSPF);
        case LIBNET_PBLOCK_LS_RTR_H:
            return (IPPROTO_OSPF_LSA);
        case LIBNET_PBLOCK_TCP_H:
            return (IPPROTO_TCP);
        case LIBNET_PBLOCK_UDP_H:
            return (IPPROTO_UDP);
        case LIBNET_PBLOCK_VRRP_H:
            return (IPPROTO_VRRP);
        case LIBNET_PBLOCK_GRE_H:
            return (IPPROTO_GRE);
        default:
            return (-1);
    }
}

void
libnet_pblock_record_ip_offset(libnet_t *l, libnet_pblock_t *p)
{
    libnet_pblock_t *c;
    uint32_t ip_offset = 0;

    assert(p->type == LIBNET_PBLOCK_IPV4_H || p->type == LIBNET_PBLOCK_IPV6_H);

    for(c = p; c; c = c->prev)
        ip_offset += c->b_len;

    for(c = p; c; c = c->prev)
        c->ip_offset = ip_offset;

}

void
libnet_pblock_repair_lengths(libnet_t* l)
{
  libnet_pblock_t* p = l->protocol_blocks;
  uint32_t datasz = 0;

  while(p) {
    switch(p->type) {
      case LIBNET_PBLOCK_UDP_H: {
	  struct libnet_udp_hdr* hdr = (struct libnet_udp_hdr*)p->buf;
	  hdr->uh_ulen = htons(p->b_len + datasz);
	  /* FIXME - repair p->h_len? For UDP, I "think" h_len is same as uh_len, and is used for cksum */
        } break;
      /* FIXME - do TCP */
      /* FIXME - do IPv6 */
      case LIBNET_PBLOCK_IPV4_H: {
	  struct libnet_ipv4_hdr* hdr = (struct libnet_ipv4_hdr*)p->buf;
	  uint32_t ip_hl = LIBNET_IPV4_H;
	  hdr->ip_len = htons(p->b_len + datasz);
	  if(p->prev && p->prev->type == LIBNET_PBLOCK_IPO_H) {
	      ip_hl += p->prev->b_len;
	  }
	  hdr->ip_hl = ip_hl / 4;

	  libnet_pblock_record_ip_offset(l, p);
	} break;
    }
    datasz += p->b_len;
    p = p->next;
  }
}

/* EOF */
