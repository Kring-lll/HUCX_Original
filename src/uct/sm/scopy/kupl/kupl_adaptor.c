/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <dlfcn.h>
#include <ucs/debug/log.h>
#include "kupl_adaptor.h"

static const char *kupl_lib_name = "libkupl.so";
static int kupl_singleton = 0;
static void *_kupl_handle = NULL;

typedef struct kupl_func_map {
    #define KUPL_INTERFACE(_ret, _f, _args) \
        _ret (*KUPL_FUNC_PTR(_f)) _args
    #include "kupl_interface.h"
} kupl_func_map_t;

static kupl_func_map_t global_kupl_func_map = { 0 };

ucs_status_t init_kupl_lib()
{
    if (kupl_singleton) {
        return UCS_OK;
    }

    (void)dlerror();
    _kupl_handle = dlopen(kupl_lib_name, RTLD_LAZY);
    if (_kupl_handle == NULL) {
        ucs_debug("dlopen %s failed %s", kupl_lib_name, dlerror());
        return UCS_ERR_NO_RESOURCE;
    }

    #define KUPL_INTERFACE(_ret, _f, _args) \
        do { \
            global_kupl_func_map.KUPL_FUNC_PTR(_f) = \
                (typeof(global_kupl_func_map.KUPL_FUNC_PTR(_f)))dlsym(_kupl_handle, #_f); \
            if (!global_kupl_func_map.KUPL_FUNC_PTR(_f)) { \
                ucs_error("dlsym %s:%s failed %s", kupl_lib_name, #_f, dlerror()); \
                dlclose(_kupl_handle); \
                return UCS_ERR_NO_RESOURCE; \
            } \
        } while (0);
    #include "kupl_interface.h"

    kupl_singleton = 1;
    return UCS_OK;
}

void uninit_kupl_lib()
{
    if (!kupl_singleton) {
        return;
    }
    dlclose(_kupl_handle);
    kupl_singleton = 0;
}

void* KUPL_FUNC(kupl_malloc)(size_t size) 
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_malloc)(size);
}

void KUPL_FUNC(kupl_free)(void *ptr)
{
    global_kupl_func_map.KUPL_FUNC_PTR(kupl_free)(ptr);
}

int KUPL_FUNC(kupl_memcpy_async)(void *dst,
                                 void *src, size_t count,
                                 kupl_queue_h queue, kupl_event_h event)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_memcpy_async)(dst, src, count, queue, event);
}

int KUPL_FUNC(kupl_memcpy)(void *dst, const void *src, size_t count)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_memcpy)(dst, src, count);
}

int KUPL_FUNC(kupl_mlock)(void *buffer, size_t count)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_mlock)(buffer, count);
}

int KUPL_FUNC(kupl_munlock)(void *buffer, size_t count)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_munlock)(buffer, count);
}

kupl_queue_h KUPL_FUNC(kupl_queue_create)(kupl_queue_flag_t flag)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_queue_create)(flag);
}

void KUPL_FUNC(kupl_queue_destroy)(kupl_queue_h queue)
{
    global_kupl_func_map.KUPL_FUNC_PTR(kupl_queue_destroy)(queue);
}

kupl_queue_h KUPL_FUNC(kupl_event_create)(void)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_event_create)();
}

void KUPL_FUNC(kupl_event_destroy)(kupl_event_h event)
{
    global_kupl_func_map.KUPL_FUNC_PTR(kupl_event_destroy)(event);
}

int KUPL_FUNC(kupl_event_wait)(kupl_event_h event)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_event_wait)(event);
}
int KUPL_FUNC(kupl_event_query)(kupl_event_h event)
{
    return global_kupl_func_map.KUPL_FUNC_PTR(kupl_event_query)(event);
}