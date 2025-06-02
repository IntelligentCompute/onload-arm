/* SPDX-License-Identifier: GPL-2.0 */
/* X-SPDX-Copyright-Text: (c) Copyright 2016-2020 Xilinx, Inc. */
#include <etherfabric/capabilities.h>
#include <ci/efch/op_types.h>
#include <ci/efhw/efhw_types.h>
#include <ci/efrm/resource.h>
#include <ci/driver/efab/hardware.h>
#include <ci/efrm/efrm_client.h>

#include "efch.h"
#include "linux_char_internal.h"
#include "char_internal.h"

static void get_from_queue_sizes(struct efhw_nic* nic, int q_type,
                                 struct efch_capabilities_out* out)
{
  out->support_rc = 0;
  out->val = nic->q_sizes[q_type];
}


static void get_from_flags(uint64_t have_flags, uint64_t want_flags,
                           struct efch_capabilities_out* out)
{
  if( (have_flags & want_flags) == want_flags ) {
    out->support_rc = 0;
    out->val = 1;
  }
  else {
    out->support_rc = -EOPNOTSUPP;
    out->val = 0;
  }
}

static void get_from_nic_flags(struct efhw_nic* nic, uint64_t flags,
                               struct efch_capabilities_out* out)
{
  get_from_flags(nic->flags, flags, out);
}

static void get_from_filter_flags(struct efhw_nic* nic, uint64_t flags,
                                  struct efch_capabilities_out* out)
{
  get_from_flags(nic->filter_flags, flags, out);
}

int efch_capabilities_op(struct efch_capabilities_in* in,
                         struct efch_capabilities_out* out)
{
  int rc;
  struct efrm_client* client = NULL;
  struct efhw_nic* nic;
  struct efrm_resource* pd = NULL;
  uint32_t masked_cap = (in->cap & ~EF_VI_CAP_F_ALL);

  if( in->ifindex >= 0 ) {
    uint64_t nic_flags_mask = NIC_FLAG_LLCT;
    uint64_t nic_flags = 0;

    /* If we are looking up the properties of an LLCT NIC then we should
     * include the LLCT flag, but we should avoid doing this if we are checking
     * what datapaths are supported to avoid returning -ENODEV. Really, this is
     * a user error, but it will surely lead to confusion otherwise. */
    if( masked_cap != EF_VI_CAP_EXTRA_DATAPATHS && in->cap & EF_VI_CAP_F_LLCT )
      nic_flags |= NIC_FLAG_LLCT;

    /* Query by ifindex. */
    if ((rc = efrm_client_get(in->ifindex, nic_flags, nic_flags_mask, NULL,
                              NULL, &client)) < 0) {
      EFCH_ERR("%s: ERROR: ifindex=%d rc=%d", __FUNCTION__,
               in->ifindex, rc);
      goto out;
    }

    nic = efrm_client_get_nic(client);
  }
  else {
    /* Query by PD. */
    if( (rc = efch_lookup_rs(in->pd_fd, in->pd_id, EFRM_RESOURCE_PD,
                             &pd)) < 0 ) {
      EFCH_ERR("%s: ERROR: PD lookup failed: pd_id=%u rc=%d", __FUNCTION__,
               in->pd_id.index, rc);
      goto out;
    }

    nic = efrm_client_get_nic(pd->rs_client);
  }


  switch( masked_cap ) {

  case EF_VI_CAP_PIO:
    get_from_nic_flags(nic, NIC_FLAG_PIO, out);
    break;
  case EF_VI_CAP_PIO_BUFFER_SIZE:
    if( nic->flags & NIC_FLAG_PIO ) {
      out->support_rc = 0;
      out->val = nic->pio_size;
    }
    else {
      out->support_rc = -EOPNOTSUPP;
      out->val = 0;
    }
    break;
  case EF_VI_CAP_PIO_BUFFER_COUNT:
    if( nic->flags & NIC_FLAG_PIO ) {
      out->support_rc = 0;
      out->val = nic->pio_num;
    }
    else {
      out->support_rc = -EOPNOTSUPP;
      out->val = 0;
    }
    break;
 
  case EF_VI_CAP_HW_MULTICAST_LOOPBACK:
    get_from_nic_flags(nic, NIC_FLAG_MCAST_LOOP_HW, out);
    break;
  case EF_VI_CAP_HW_MULTICAST_REPLICATION:
    get_from_nic_flags(nic, NIC_FLAG_HW_MULTICAST_REPLICATION, out);
    break;
 
  case EF_VI_CAP_HW_RX_TIMESTAMPING:
    get_from_nic_flags(nic, NIC_FLAG_HW_RX_TIMESTAMPING, out);
    break;
  case EF_VI_CAP_HW_TX_TIMESTAMPING:
    get_from_nic_flags(nic, NIC_FLAG_HW_TX_TIMESTAMPING, out);
    break;

  case EF_VI_CAP_PACKED_STREAM:
    get_from_nic_flags(nic, NIC_FLAG_PACKED_STREAM, out);
    break;

  case EF_VI_CAP_RX_FORCE_EVENT_MERGING:
    get_from_nic_flags(nic, NIC_FLAG_RX_FORCE_EVENT_MERGING, out);
    break;

  case EF_VI_CAP_PACKED_STREAM_BUFFER_SIZES:
    /* ef_vi only presents a subset of the supported buffer sizes, based on
     * whether NIC_FLAG_VAR_PACKED_STREAM is set.
     */
    if( nic->flags & NIC_FLAG_VAR_PACKED_STREAM ) {
      out->support_rc = 0;
      out->val = 1024 | 64;
      break;
    }
    else if( nic->flags & NIC_FLAG_PACKED_STREAM ) {
      out->support_rc = 0;
      out->val = 1024;
      break;
    }
    else {
      out->support_rc = -EOPNOTSUPP;
      out->val = 0;
      break;
    }
 
  case EF_VI_CAP_VPORTS:
    get_from_nic_flags(nic, NIC_FLAG_VPORTS, out);
    break;
 
  case EF_VI_CAP_PHYS_MODE:
    get_from_nic_flags(nic, NIC_FLAG_PHYS_MODE, out);
    break;
  case EF_VI_CAP_BUFFER_MODE:
    get_from_nic_flags(nic, NIC_FLAG_BUFFER_MODE, out);
    break;

  case EF_VI_CAP_MULTICAST_FILTER_CHAINING:
    get_from_nic_flags(nic, NIC_FLAG_MULTICAST_FILTER_CHAINING, out);
    break;

  case EF_VI_CAP_MAC_SPOOFING:
    get_from_nic_flags(nic, NIC_FLAG_MAC_SPOOFING, out);
    break;

  /* We are slightly making some assumptions here, as we don't install filters
   * directly, but rely on the net driver.  These check that the combos of
   * match criteria that we expect to be necessary for the filters that we
   * use are present.
   */
  case EF_VI_CAP_RX_FILTER_TYPE_UDP_LOCAL:
  case EF_VI_CAP_RX_FILTER_TYPE_TCP_LOCAL:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_IP_LOCAL, out);
    break;
  case EF_VI_CAP_RX_FILTER_TYPE_UDP_FULL:
  case EF_VI_CAP_RX_FILTER_TYPE_TCP_FULL:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_IP_FULL, out);
    break;
  case EF_VI_CAP_RX_FILTER_TYPE_IP_VLAN:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_IPX_VLAN_HW, out);
    break;

  /* Hardware support for IPv6 doesn't imply software support - however this
   * API postdates addition of IPv6 support to ef_vi, so we can assume that
   * if the NIC supports it, it's available.
   */
  case EF_VI_CAP_RX_FILTER_TYPE_UDP6_LOCAL:
  case EF_VI_CAP_RX_FILTER_TYPE_TCP6_LOCAL:
    get_from_filter_flags(nic,NIC_FILTER_FLAG_RX_TYPE_IP_LOCAL |
                          NIC_FILTER_FLAG_RX_TYPE_IP6, out);
    break;
  case EF_VI_CAP_RX_FILTER_TYPE_UDP6_FULL:
  case EF_VI_CAP_RX_FILTER_TYPE_TCP6_FULL:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_IP_FULL |
                          NIC_FILTER_FLAG_RX_TYPE_IP6, out);
    break;
  case EF_VI_CAP_RX_FILTER_TYPE_IP6_VLAN:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_IPX_VLAN_HW |
                          NIC_FILTER_FLAG_RX_TYPE_IP6, out);
    break;

  case EF_VI_CAP_RX_FILTER_TYPE_ETH_LOCAL:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_ETH_LOCAL, out);
    break;

  case EF_VI_CAP_RX_FILTER_TYPE_ETH_LOCAL_VLAN:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_ETH_LOCAL_VLAN, out);
    break;

  case EF_VI_CAP_RX_FILTER_TYPE_UCAST_ALL:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_UCAST_ALL, out);
    break;
  case EF_VI_CAP_RX_FILTER_TYPE_MCAST_ALL:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_MCAST_ALL, out);
    break;
  case EF_VI_CAP_RX_FILTER_TYPE_UCAST_MISMATCH:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_UCAST_MISMATCH, out);
    break;
  case EF_VI_CAP_RX_FILTER_TYPE_MCAST_MISMATCH:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_TYPE_MCAST_MISMATCH, out);
    break;

  case EF_VI_CAP_RX_FILTER_TYPE_SNIFF:
    out->support_rc = -ENOSYS;
    out->val = 0;
    break;
  case EF_VI_CAP_TX_FILTER_TYPE_SNIFF:
    out->support_rc = -ENOSYS;
    out->val = 0;
    break;

  case EF_VI_CAP_RX_FILTER_IP4_PROTO:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_IP4_PROTO, out);
    break;

  case EF_VI_CAP_RX_FILTER_ETHERTYPE:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_ETHERTYPE, out);
    break;

  case EF_VI_CAP_RXQ_SIZES:
    get_from_queue_sizes(nic, EFHW_RXQ, out);
    break;

  case EF_VI_CAP_TXQ_SIZES:
    get_from_queue_sizes(nic, EFHW_TXQ, out);
    break;

  case EF_VI_CAP_EVQ_SIZES:
    get_from_queue_sizes(nic, EFHW_EVQ, out);
    break;

  case EF_VI_CAP_ZERO_RX_PREFIX:
    get_from_nic_flags(nic, NIC_FLAG_ZERO_RX_PREFIX, out);
    break;

  /* This checks availability of an ef_vi API flag.  This is policed based
   * on NIC arch, so we use the same test here.
   */
  case EF_VI_CAP_TX_PUSH_ALWAYS:
    out->support_rc = -EOPNOTSUPP;
    out->val = 0;
    break;

  case EF_VI_CAP_NIC_PACE:
    get_from_nic_flags(nic, NIC_FLAG_NIC_PACE, out);
    break;

  case EF_VI_CAP_RX_MERGE:
    get_from_nic_flags(nic, NIC_FLAG_RX_MERGE, out);
    break;

  case EF_VI_CAP_TX_ALTERNATIVES:
    get_from_nic_flags(nic, NIC_FLAG_TX_ALTERNATIVES, out);
    break;

  case EF_VI_CAP_TX_ALTERNATIVES_VFIFOS:
    get_from_nic_flags(nic, NIC_FLAG_TX_ALTERNATIVES, out);
    if( out->support_rc == 0 )
      out->val = nic->tx_alts_vfifos;
    break;

  case EF_VI_CAP_TX_ALTERNATIVES_CP_BUFFERS:
    get_from_nic_flags(nic, NIC_FLAG_TX_ALTERNATIVES, out);
    if( out->support_rc == 0 )
      out->val = nic->tx_alts_cp_bufs;
    break;

  case EF_VI_CAP_TX_ALTERNATIVES_CP_BUFFER_SIZE:
    get_from_nic_flags(nic, NIC_FLAG_TX_ALTERNATIVES, out);
    if( out->support_rc == 0 )
      out->val = nic->tx_alts_cp_buf_size;
    break;

  case EF_VI_CAP_RX_FW_VARIANT:
    out->support_rc = 0;
    out->val = nic->rx_variant;
    break;

  case EF_VI_CAP_TX_FW_VARIANT:
    out->support_rc = 0;
    out->val = nic->tx_variant;
    break;

  case EF_VI_CAP_CTPIO:
    get_from_nic_flags(nic, NIC_FLAG_TX_CTPIO, out);
    break;

  case EF_VI_CAP_CTPIO_ONLY:
    get_from_nic_flags(nic, NIC_FLAG_CTPIO_ONLY, out);
    break;

  case EF_VI_CAP_RX_SHARED:
  case EF_VI_CAP_RX_FILTER_SET_DEST:
    get_from_nic_flags(nic, NIC_FLAG_RX_SHARED, out);
    break;

  case EF_VI_CAP_MIN_BUFFER_MODE_SIZE: {
    int n = efhw_nic_buffer_table_orders_num(nic);
    const int* orders = efhw_nic_buffer_table_orders(nic);
    int i;
    int val = INT_MAX;

    for (i = 0; i < n; ++i)
      val = CI_MIN(val, orders[i]);
    if (val == INT_MAX) {
      /* If we don't have a buffer table then claim basic 4k page support.
       * We don't want to return an error here, because we want to maintain
       * compat with existing apps, so we need to return something. There's
       * an existing requirement in ef_memreg_alloc() for 4k alignment, so
       * let's report that here, though in theory we have no minimum.
       */
      out->val = EFHW_NIC_PAGE_SIZE;
      out->support_rc = 0;
    }
    else {
      out->val = EFHW_NIC_PAGE_SIZE << val;
      out->support_rc = 0;
    }
    break;
  }

  case EF_VI_CAP_RX_FILTER_MAC_IP4_PROTO:
    get_from_filter_flags(nic, NIC_FILTER_FLAG_RX_MAC_IP4_PROTO, out);
    break;

  case EF_VI_CAP_RX_POLL:
    get_from_nic_flags(nic, NIC_FLAG_RX_POLL, out);
    break;

  case EF_VI_CAP_RX_REF:
    get_from_nic_flags(nic, NIC_FLAG_RX_REF, out);
    break;

  case EF_VI_CAP_EXTRA_DATAPATHS: {
    struct efrm_client* llct_client = NULL;
    unsigned int ifindex = in->ifindex;

    out->support_rc = 0;
    out->val = 0;

    if( ifindex < 0 ) {
      /* This function is documented as for diagnostic/logging purposes only,
       * so lets warn the user if they do this that it's ill advised. */
      EFCH_ERR("%s: WARNING: checking EF_VI_CAP_EXTRA_DATAPATHS should be done by ifindex, not pd!",
               __FUNCTION__);
      if( (ifindex = efrm_client_get_ifindex(client)) < 0 ) {
        out->support_rc = -ENODEV;
        out->val = 0;
        break;
      }
    }

    /* Try to find an LLCT client on this ifindex, and use presence of this to
     * indicate support of the LLCT datapath. */
    if( (rc = efrm_client_get(ifindex, NIC_FLAG_LLCT, NIC_FLAG_LLCT, NULL,
                              NULL, &llct_client)) < 0 ) {
      if( rc != -ENODEV ) {
        out->support_rc = rc;
        out->val = 0;
      }
    } else {
      efrm_client_put(llct_client);
      out->support_rc = 0;
      out->val |= EF_VI_EXTRA_DATAPATH_EXPRESS;
    }

    rc = out->support_rc;

    break;
  }

  default:
    out->support_rc = -ENOSYS;
    out->val = 0;
  }

  if( client != NULL )
    efrm_client_put(client);
  if( pd != NULL )
    efrm_resource_release(pd);

 out:
   return rc;
}
