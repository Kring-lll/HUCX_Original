/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

/* KUPL external interface function  */
KUPL_INTERFACE(void*, kupl_malloc, (size_t size));
KUPL_INTERFACE(void, kupl_free, (void *ptr));
KUPL_INTERFACE(int, kupl_memcpy_async, 
               (void *dst, void *src, size_t count,
               kupl_queue_h queue, kupl_event_h event));  //copy n bytes from memory area src to memory area dst asynchronously
KUPL_INTERFACE(int, kupl_memcpy, (void *dst, const void *src, size_t count));
KUPL_INTERFACE(int, kupl_mlock, (void *buffer, size_t count));
KUPL_INTERFACE(int, kupl_munlock, (void *buffer, size_t count));
KUPL_INTERFACE(kupl_queue_h, kupl_queue_create, (kupl_queue_flag_t flag));
KUPL_INTERFACE(void, kupl_queue_destroy, (kupl_queue_h queue));
KUPL_INTERFACE(kupl_queue_h, kupl_event_create, (void));
KUPL_INTERFACE(void, kupl_event_destroy, (kupl_event_h event));
KUPL_INTERFACE(int, kupl_event_wait, (kupl_event_h event));
KUPL_INTERFACE(int, kupl_event_query, (kupl_event_h event));

#undef KUPL_INTERFACE