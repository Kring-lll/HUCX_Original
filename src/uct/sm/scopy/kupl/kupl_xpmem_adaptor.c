/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <dlfcn.h>
#include <ucs/debug/log.h>
#include "kupl_xpmem_adaptor.h"

static const char *xpmem_lib_name = "libxpmem.so";
static int xpmem_singleton = 0;
static void *_xpmem_handle = NULL;                      

typedef struct uct_kupl_xpmem_func_map {
    xpmem_make_func xpmem_make;
    xpmem_get_func xpmem_get;
    xpmem_attach_func xpmem_attach;
    xpmem_detach_func xpmem_detach;
    xpmem_release_func xpmem_release;
    xpmem_remove_func xpmem_remove;
}uct_kupl_xpmem_func_map_t;

static uct_kupl_xpmem_func_map_t global_kupl_xpmem_func_map = { 0 };

ucs_status_t kupl_xpmem_init()
{
    if (xpmem_singleton) {
        return UCS_OK;
    }
    (void)dlerror();
    _xpmem_handle = dlopen(xpmem_lib_name, RTLD_LAZY);
    if (_xpmem_handle == NULL) {
        ucs_debug("dlopen %s failed %s", xpmem_lib_name, dlerror());
        return UCS_ERR_NO_RESOURCE;
    }

    if (!(global_kupl_xpmem_func_map.xpmem_make = (xpmem_make_func)dlsym(_xpmem_handle, "xpmem_make")) ||
        !(global_kupl_xpmem_func_map.xpmem_get = (xpmem_get_func)dlsym(_xpmem_handle, "xpmem_get")) ||
        !(global_kupl_xpmem_func_map.xpmem_attach = (xpmem_attach_func)dlsym(_xpmem_handle, "xpmem_attach")) ||
        !(global_kupl_xpmem_func_map.xpmem_detach = (xpmem_detach_func)dlsym(_xpmem_handle, "xpmem_detach")) ||
        !(global_kupl_xpmem_func_map.xpmem_release = (xpmem_release_func)dlsym(_xpmem_handle, "xpmem_release")) ||
        !(global_kupl_xpmem_func_map.xpmem_remove = (xpmem_remove_func)dlsym(_xpmem_handle, "xpmem_remove"))) { 
        ucs_debug("dlsym %s failed %s", xpmem_lib_name, dlerror()); 
        dlclose(_xpmem_handle); 
        return UCS_ERR_NO_RESOURCE; 
    } 

    xpmem_singleton = 1;
    return UCS_OK;
}

void kupl_xpmem_uninit()
{
    if (!xpmem_singleton) {
        return;
    }
    dlclose(_xpmem_handle);
    xpmem_singleton = 0;
}

xpmem_segid_t kupl_xpmem_make_func(void *vaddr, size_t size, int permit_type, void *permit_value)
{
    return global_kupl_xpmem_func_map.xpmem_make(vaddr, size, permit_type, permit_value);
}
xpmem_apid_t kupl_xpmem_get_func(xpmem_segid_t segid, int flags, int permit_type,
                                 void *permit_value)
{
    return global_kupl_xpmem_func_map.xpmem_get(segid, flags, permit_type, permit_value);
}
void *kupl_xpmem_attach_func(struct xpmem_addr addr, size_t size, void *vaddr)
{
    return global_kupl_xpmem_func_map.xpmem_attach(addr, size, vaddr);
}
int kupl_xpmem_detach_func(void *vaddr)
{
    return global_kupl_xpmem_func_map.xpmem_detach(vaddr);
}
int kupl_xpmem_release_func(xpmem_apid_t apid)
{
    return global_kupl_xpmem_func_map.xpmem_release(apid);
}
int kupl_xpmem_remove_func(xpmem_segid_t segid)
{
    return global_kupl_xpmem_func_map.xpmem_remove(segid);
}