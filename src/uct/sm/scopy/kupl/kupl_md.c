/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include <stdbool.h>
#include <uct/api/uct_def.h>
#include <ucs/debug/log.h>
#include <ucs/debug/memtrack_int.h>
#include <dlfcn.h>
#include <ucs/type/spinlock.h>
#include "config.h"
#include "kupl_md.h"


static ucs_recursive_spinlock_t  uct_xpmem_remote_mem_lock;

/* env-config table */
ucs_config_field_t uct_kupl_md_config_table[] = {
    {"", "", NULL, ucs_offsetof(uct_kupl_md_config_t, super), UCS_CONFIG_TYPE_TABLE(uct_md_config_table)},

    {NULL}
};

/* close kupl dev, maybe passthrough */
static void uct_kupl_md_close(uct_md_h md)
{
    uct_kupl_md_t *kupl_md = ucs_derived_of(md, uct_kupl_md_t);
    ucs_free(kupl_md);
    kupl_md = NULL;
    ucs_recursive_spinlock_destroy(&uct_xpmem_remote_mem_lock);

    kupl_xpmem_uninit();
    uninit_kupl_lib();
    ucs_debug("kupl: md mem free");
    return;
}

/* fill md_attr */
static ucs_status_t uct_kupl_md_query(uct_md_h uct_md, uct_md_attr_v2_t *md_attr)
{
    md_attr->flags                  = UCT_MD_FLAG_REG | UCT_MD_FLAG_NEED_RKEY | UCT_MD_FLAG_ALLOC | UCT_MD_FLAG_FIXED;
    md_attr->reg_mem_types          = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->reg_nonblock_mem_types = 0;
    md_attr->cache_mem_types        = 0;
    md_attr->alloc_mem_types        = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->access_mem_types       = UCS_BIT(UCS_MEMORY_TYPE_HOST);
    md_attr->detect_mem_types       = 0;
    md_attr->dmabuf_mem_types       = 0;
    md_attr->max_alloc              = ULONG_MAX;
    md_attr->max_reg                = ULONG_MAX;
    md_attr->rkey_packed_size       = 0;
    md_attr->reg_cost               = ucs_linear_func_make(0, 0);
    memset(&md_attr->local_cpus, 0xff, sizeof(md_attr->local_cpus));
    ucs_trace("uct_kupl_md_query OK!");
    return UCS_OK;
}

static ucs_status_t uct_kupl_mem_reg(uct_md_h md, void *address, size_t length,
                                     const uct_md_mem_reg_params_t *params, uct_mem_h *memh_p)
{
    ucs_status_t status = UCS_OK;
    uct_kupl_key_t *kupl_memh = (uct_kupl_key_t *)ucs_calloc(1, sizeof(uct_kupl_key_t), "uct_kupl_key_t");
    if (kupl_memh == NULL) {
        ucs_error("Failed to allocate memory for kupl_memh");
        return UCS_ERR_NO_MEMORY;
    }
    kupl_memh->address = address;
    kupl_memh->length = length;
    *memh_p = kupl_memh;
    return status;
}

/* unreg memory from user */
static ucs_status_t uct_kupl_mem_dereg(uct_md_h md, const uct_md_mem_dereg_params_t *params)
{
    ucs_status_t status = UCS_OK;
    uct_kupl_key_t *kupl_memh;

    UCT_MD_MEM_DEREG_CHECK_PARAMS(params, 0);

    kupl_memh = (uct_kupl_key_t *)params->memh;
    ucs_free(kupl_memh);
    kupl_memh = NULL;
    return status;
}

/* alloc and reg memory from kupl */
static ucs_status_t uct_kupl_mem_alloc(uct_md_h tl_md, size_t *length_p, 
                                       void **address_p, ucs_memory_type_t mem_type,
                                       unsigned flags, const char *alloc_name, uct_mem_h *memh_p)
{
    ucs_status_t status;
    void *kupl_mem_addr = (void *)ucs_malloc(*length_p, "kupl_mem_addr");

    if (kupl_mem_addr == NULL) {
        ucs_error("Failed to allocate memory for kupl_mem_addr");
        return UCS_ERR_NO_MEMORY;
    }
    status = uct_kupl_mem_reg(tl_md, kupl_mem_addr, *length_p, NULL, memh_p);
    if (status != UCS_OK) {
        ucs_free(kupl_mem_addr);
        kupl_mem_addr = NULL;
        ucs_error("Failed to pinned memory for kupl_mem_addr");
        return status;
    }
    *address_p = kupl_mem_addr;
    ucs_debug("uct_kupl_mem_alloc kupl_mem_addr is %p ", kupl_mem_addr);
    return UCS_OK;
}

/* unreg and free memory from kupl */
static ucs_status_t uct_kupl_mem_free(uct_md_h md, uct_mem_h memh)
{
    ucs_status_t status = UCS_OK;
    uct_kupl_key_t *kupl_memh = memh;
    void *kupl_mem_addr;
    uct_md_mem_dereg_params_t params = { 0 };

    if(kupl_memh == NULL) {
        return status;
    }
    if(kupl_memh->address == NULL) {
        ucs_free(kupl_memh);
        kupl_memh = NULL;
        return status;
    }

    kupl_mem_addr = kupl_memh->address;
    params.memh = memh;
    params.field_mask = UCT_MD_MEM_DEREG_FIELD_MEMH;

    status = uct_kupl_mem_dereg(md, &params);
    if (status != UCS_OK) {
        ucs_error("kupl_unpin_mem failed , status is %d.", status);
    }
    ucs_free(kupl_mem_addr);
    kupl_mem_addr = NULL;
    return status;
}

static uct_md_ops_t uct_kupl_md_ops = {
    .close              = uct_kupl_md_close,
    .query              = uct_kupl_md_query,
    .mem_alloc          = uct_kupl_mem_alloc,
    .mem_free           = uct_kupl_mem_free,
    .mem_advise         = (uct_md_mem_advise_func_t)ucs_empty_function_return_unsupported,
    .mem_reg            = uct_kupl_mem_reg,
    .mem_dereg          = uct_kupl_mem_dereg,
    .mkey_pack          = (uct_md_mkey_pack_func_t)ucs_empty_function_return_success,
    .detect_memory_type = (uct_md_detect_memory_type_func_t)ucs_empty_function_return_unsupported
};

/* alloc kupl md resource */
static ucs_status_t uct_kupl_md_open(uct_component_t *component, const char *md_name, const uct_md_config_t *config,
                                     uct_md_h *md_p)
{
    
    uct_kupl_md_t *md;
    ucs_status_t status;
    ucs_recursive_spinlock_init(&uct_xpmem_remote_mem_lock, 0);
    md = ucs_malloc(sizeof(*md), "uct_kupl_md_t");
    if(md == NULL) {
        ucs_error("Failed to allocate memory for uct_kupl_md_t");
        return UCS_ERR_NO_MEMORY;
    }
 
    status = kupl_xpmem_init();
    if (status != UCS_OK) {
        ucs_debug("could not open the XPMEM shared library file: %m. Disabling XPMEM shared library");
        return UCS_ERR_NO_MEMORY;
    }
    status = init_kupl_lib();
    if (status != UCS_OK) {
        kupl_xpmem_uninit();
        ucs_debug("could not open the KUPL shared library file: %m. Disabling KUPL shared library");
        return UCS_ERR_NO_MEMORY;
    }
 
    md->super.ops = &uct_kupl_md_ops;
    md->super.component = &uct_kupl_component;

    md->xsegid = -1;
    *md_p = &md->super;
    ucs_debug("uct_kupl_md_open");
    return UCS_OK;
}

/* check if kupl ready */
static ucs_status_t uct_md_query_kupl_md_resource(uct_component_t *component, uct_md_resource_desc_t **resources_p,
                                                  unsigned *num_resources_p)
{
    return uct_md_query_single_md_resource(component, resources_p, num_resources_p);
}

uct_component_t uct_kupl_component = {
    .name = {'k', 'u', 'p', 'l', '\0'},
    .query_md_resources = uct_md_query_kupl_md_resource,
    .md_open = uct_kupl_md_open,
    .cm_open = (uct_component_cm_open_func_t)ucs_empty_function_return_unsupported,
    .rkey_unpack = uct_md_stub_rkey_unpack,
    .rkey_ptr = (uct_component_rkey_ptr_func_t)ucs_empty_function_return_success,
    .rkey_release = (uct_component_rkey_release_func_t)ucs_empty_function_return_success,
    .md_config =
        {
            .name = " kupl memory domain",
            .prefix = "KUPL_",
            .table = uct_kupl_md_config_table,
            .size = sizeof(uct_kupl_md_config_t),
        },
    .cm_config = UCS_CONFIG_EMPTY_GLOBAL_LIST_ENTRY,
    .tl_list = UCT_COMPONENT_TL_LIST_INITIALIZER(&uct_kupl_component),
    .flags = 0,
    .md_vfs_init = (uct_component_md_vfs_init_func_t)ucs_empty_function
};
UCT_COMPONENT_REGISTER(&uct_kupl_component);