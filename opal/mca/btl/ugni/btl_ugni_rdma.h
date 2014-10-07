/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011-2014 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2011      UT-Battelle, LLC. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#if !defined(MCA_BTL_UGNI_RDMA_H)
#define MCA_BTL_UGNI_RDMA_H

#include "btl_ugni.h"
#include "btl_ugni_frag.h"

int mca_btl_ugni_start_eager_get (mca_btl_base_endpoint_t *ep,
                                  mca_btl_ugni_eager_ex_frag_hdr_t hdr,
                                  mca_btl_ugni_base_frag_t *frag);

static inline void init_gni_post_desc (opal_common_ugni_post_desc_t *post_desc,
                                       gni_post_type_t op_type,
                                       uint64_t lcl_addr,
                                       gni_mem_handle_t lcl_mdh,
                                       uint64_t rem_addr,
                                       gni_mem_handle_t rem_mdh,
                                       uint64_t bufsize,
                                       gni_cq_handle_t cq_hndl) {
    post_desc->base.type            = op_type;
    post_desc->base.cq_mode         = GNI_CQMODE_GLOBAL_EVENT;
    post_desc->base.dlvr_mode       = GNI_DLVMODE_PERFORMANCE;
    post_desc->base.local_addr      = (uint64_t) lcl_addr;
    post_desc->base.local_mem_hndl  = lcl_mdh;
    post_desc->base.remote_addr     = (uint64_t) rem_addr;
    post_desc->base.remote_mem_hndl = rem_mdh;
    post_desc->base.length          = bufsize;
    post_desc->base.rdma_mode       = 0;
    post_desc->base.src_cq_hndl     = cq_hndl;
    post_desc->tries                = 0;
}

static inline int mca_btl_ugni_post_fma (struct mca_btl_base_endpoint_t *endpoint, gni_post_type_t op_type,
                                         size_t size, void *local_address, uint64_t remote_address,
                                         mca_btl_base_registration_handle_t *local_handle,
                                         mca_btl_base_registration_handle_t *remote_handle,
                                         mca_btl_base_rdma_completion_fn_t cbfunc,
                                         void *cbcontext, void *cbdata)
{
    mca_btl_ugni_post_descriptor_t *post_desc;
    gni_return_t grc;

    mca_btl_ugni_alloc_post_descriptor (endpoint, local_handle, cbfunc, cbcontext, cbdata, &post_desc);
    if (OPAL_UNLIKELY(NULL == post_desc)) {
        return OPAL_ERR_OUT_OF_RESOURCE;
    }

    /* Post descriptor (CQ is ignored for FMA transactions) -- The CQ associated with the endpoint
     * is used. */
    init_gni_post_desc (&post_desc->desc, op_type, (intptr_t) local_address, local_handle->gni_handle,
                        remote_address, remote_handle->gni_handle, size, 0);

    OPAL_THREAD_LOCK(&endpoint->btl->device->dev_lock);
    grc = GNI_PostFma (endpoint->rdma_ep_handle, &post_desc->desc.base);
    OPAL_THREAD_UNLOCK(&endpoint->btl->device->dev_lock);
    if (OPAL_UNLIKELY(GNI_RC_SUCCESS != grc)) {
        mca_btl_ugni_return_post_descriptor (endpoint->btl, post_desc);

        if (GNI_RC_ALIGNMENT_ERROR == grc) {
            BTL_VERBOSE(("GNI_PostFma failed with an alignment error"));
            return OPAL_ERR_NOT_AVAILABLE;
        }

        BTL_VERBOSE(("GNI_PostFma failed with gni rc: %d", grc));
        return OPAL_ERR_OUT_OF_RESOURCE;
    }

    return OPAL_SUCCESS;
}

static inline int mca_btl_ugni_post_bte (mca_btl_base_endpoint_t *endpoint, gni_post_type_t op_type,
                                         size_t size, void *local_address, uint64_t remote_address,
                                         mca_btl_base_registration_handle_t *local_handle,
                                         mca_btl_base_registration_handle_t *remote_handle,
                                         mca_btl_base_rdma_completion_fn_t cbfunc,
                                         void *cbcontext, void *cbdata)
{
    mca_btl_ugni_post_descriptor_t *post_desc;
    gni_return_t grc;

    mca_btl_ugni_alloc_post_descriptor (endpoint, local_handle, cbfunc, cbcontext, cbdata, &post_desc);
    if (OPAL_UNLIKELY(NULL == post_desc)) {
        return OPAL_ERR_OUT_OF_RESOURCE;
    }

    /* Post descriptor */
    init_gni_post_desc (&post_desc->desc, op_type, (intptr_t) local_address, local_handle->gni_handle,
                        remote_address, remote_handle->gni_handle, size, endpoint->btl->rdma_local_cq);

    OPAL_THREAD_LOCK(&endpoint->btl->device->dev_lock);
    grc = GNI_PostRdma (endpoint->rdma_ep_handle, &post_desc->desc.base);
    OPAL_THREAD_UNLOCK(&endpoint->btl->device->dev_lock);
    if (OPAL_UNLIKELY(GNI_RC_SUCCESS != grc)) {
        mca_btl_ugni_return_post_descriptor (endpoint->btl, post_desc);

        if (GNI_RC_ALIGNMENT_ERROR == grc) {
            BTL_VERBOSE(("GNI_PostRdma failed with an alignment error"));
            return OPAL_ERR_NOT_AVAILABLE;
        }

        BTL_VERBOSE(("GNI_PostRdma failed with gni rc: %d", grc));
        return OPAL_ERR_OUT_OF_RESOURCE;
    }

    return OPAL_SUCCESS;
}

static inline int mca_btl_ugni_post (mca_btl_base_endpoint_t *endpoint, int get, size_t size,
                                     void *local_address, uint64_t remote_address,
                                     mca_btl_base_registration_handle_t *local_handle,
                                     mca_btl_base_registration_handle_t *remote_handle,
                                     mca_btl_base_rdma_completion_fn_t cbfunc,
                                     void *cbcontext, void *cbdata)
{
    const gni_post_type_t fma_ops[2] = {GNI_POST_FMA_PUT, GNI_POST_FMA_GET};
    const gni_post_type_t rdma_ops[2] = {GNI_POST_RDMA_PUT, GNI_POST_RDMA_GET};

    if (size <= mca_btl_ugni_component.ugni_fma_limit) {
        return mca_btl_ugni_post_fma (endpoint, fma_ops[get], size, local_address, remote_address,
                                      local_handle, remote_handle, cbfunc, cbcontext, cbdata);
    }

    return mca_btl_ugni_post_bte (endpoint, rdma_ops[get], size, local_address, remote_address,
                                  local_handle, remote_handle, cbfunc, cbcontext, cbdata);
}

static inline int mca_btl_ugni_repost (mca_btl_ugni_module_t *ugni_module, mca_btl_ugni_post_descriptor_t *post_desc)
{
    gni_return_t grc;

    OPAL_THREAD_LOCK(&ugni_module->device->dev_lock);
    if (GNI_POST_RDMA_PUT == post_desc->desc.base.type ||
        GNI_POST_RDMA_GET == post_desc->desc.base.type) {
        grc = GNI_PostRdma (post_desc->endpoint->rdma_ep_handle, &post_desc->desc.base);
    } else {
        grc = GNI_PostFma (post_desc->endpoint->rdma_ep_handle, &post_desc->desc.base);
    }
    OPAL_THREAD_UNLOCK(&ugni_module->device->dev_lock);

    if (OPAL_UNLIKELY(GNI_RC_SUCCESS != grc)) {
        /* NTH: Should we even retry these? When this code was written there was no indication
         * whether an error in post is recoverable. Clobber this code and the associated data
         * structures if post errors are not recoverable. */
        OPAL_THREAD_LOCK(&ugni_module->pending_descriptors_lock);
        opal_list_append (&ugni_module->pending_descriptors, (opal_list_item_t *) post_desc);
        OPAL_THREAD_UNLOCK(&ugni_module->pending_descriptors_lock);
    }

    return opal_common_rc_ugni_to_opal (grc);
}

#endif /* MCA_BTL_UGNI_RDMA_H */
