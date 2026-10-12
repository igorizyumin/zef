#include "zef_internal.h"

#include <stdint.h>

/* @relation(ZEF-IF-002, scope=panic-and-result-boundary) */
_Noreturn void zef_fail(ZPanicReason reason, void const *object, uintptr_t detail)
{
    ZPanicInfo info;
    info.reason = reason;
    info.object = object;
    info.detail = detail;
    info.file = NULL;
    info.line = 0u;
    z_panic(&info);
}

static ZBool zef_aligned(void const *pointer)
{
    return ((uintptr_t)pointer % (uintptr_t)ZEF_POOL_ALIGNMENT) == 0u;
}

static ZBool zef_pool_contains(ZMessagePool const *pool, void const *block)
{
    uintptr_t start = (uintptr_t)pool->storage;
    uintptr_t end = start + pool->storage_size;
    uintptr_t address = (uintptr_t)block;
    return address >= start && address < end &&
           ((address - start) % pool->block_size) == 0u;
}

/* @relation(ZEF-IF-012, scope=fixed-message-pools) */
void z_message_pool_init(ZMessagePool *pool, void *storage, size_t storage_size,
                         size_t block_size, uint16_t block_count)
{
    if (pool == NULL || storage == NULL || block_count == 0u || block_size == 0u ||
        !zef_aligned(storage) || block_size % ZEF_POOL_ALIGNMENT != 0u ||
        block_size < sizeof(ZMsg) ||
        block_count > storage_size / block_size) {
        zef_fail(Z_PANIC_CONFIGURATION, pool, block_size);
    }

    pool->storage = storage;
    pool->storage_size = storage_size;
    pool->block_size = block_size;
    pool->block_count = block_count;
    pool->free_count = block_count;
    pool->min_free = block_count;
    pool->initialized = 1u;
    pool->free_list = NULL;

    for (uint16_t index = 0u; index < block_count; ++index) {
        unsigned char *block = (unsigned char *)storage + (size_t)index * block_size;
        *(void **)block = pool->free_list;
        pool->free_list = block;
    }
}

/* @relation(ZEF-IF-012, scope=pool-set) */
void z_message_pool_set_init(ZMessagePoolSet *pool_set, ZMessagePool *pools,
                             uint16_t count)
{
    if (pool_set == NULL || pools == NULL || count == 0u || count > Z_MAX_MESSAGE_POOLS) {
        zef_fail(Z_PANIC_CONFIGURATION, pool_set, count);
    }
    for (uint16_t index = 0u; index < count; ++index) {
        if (!pools[index].initialized) {
            zef_fail(Z_PANIC_CONFIGURATION, &pools[index], index);
        }
    }
    pool_set->pools = pools;
    pool_set->count = count;
}

/* @relation(ZEF-IF-010, scope=message-construction) */
void *z_message_begin(ZMessagePoolSet *pool_set, ZTopic topic, size_t payload_size,
                      ZMsg **message)
{
    if (pool_set == NULL || message == NULL || pool_set->pools == NULL ||
        pool_set->count == 0u || payload_size > SIZE_MAX - sizeof(ZMsg)) {
        zef_fail(Z_PANIC_INVALID_ARGUMENT, message, payload_size);
    }

    size_t required = sizeof(ZMsg) + payload_size;
    ZMessagePool *selected = NULL;
    for (uint16_t index = 0u; index < pool_set->count; ++index) {
        ZMessagePool *pool = &pool_set->pools[index];
        if (!pool->initialized) {
            zef_fail(Z_PANIC_CONFIGURATION, pool, index);
        }
        if (pool->block_size >= required && pool->free_count != 0u &&
            (selected == NULL || pool->block_size < selected->block_size)) {
            selected = pool;
        }
    }
    if (selected == NULL) {
        zef_fail(Z_PANIC_NO_MEMORY, pool_set, required);
    }

    void *block = selected->free_list;
    if (block == NULL || !zef_pool_contains(selected, block)) {
        zef_fail(Z_PANIC_INTERNAL, selected, 0u);
    }
    selected->free_list = *(void **)block;
    --selected->free_count;
    if (selected->free_count < selected->min_free) {
        selected->min_free = selected->free_count;
    }

    ZMsg *allocated = (ZMsg *)block;
    allocated->kind = Z_MSG_DYNAMIC;
    allocated->topic = topic;
    allocated->payload_size = payload_size;
    allocated->payload = (unsigned char *)block + sizeof(ZMsg);
    allocated->claims = 1u;
    allocated->pool = selected;
    allocated->valid = 1u;
    *message = allocated;
    return (void *)allocated->payload;
}

/* This operation supplies the static immutable Message form described by WHAT-010. */
void z_message_static_init(ZMsg *message, ZTopic topic, void const *payload,
                           size_t payload_size)
{
    if (message == NULL || (payload == NULL && payload_size != 0u)) {
        zef_fail(Z_PANIC_INVALID_ARGUMENT, message, payload_size);
    }
    message->kind = Z_MSG_STATIC;
    message->topic = topic;
    message->payload_size = payload_size;
    message->payload = payload != NULL ? payload : message;
    message->claims = 0u;
    message->pool = NULL;
    message->valid = 1u;
}

static ZMsg *zef_mutable_message(ZMsg *message)
{
    if (message == NULL || !message->valid || message->kind != Z_MSG_DYNAMIC ||
        message->pool == NULL) {
        zef_fail(Z_PANIC_LIFECYCLE, message, 0u);
    }
    return message;
}

void *z_message_payload_mut(ZMsg *message)
{
    return (void *)zef_mutable_message(message)->payload;
}

void *z_message_payload_mut_checked(ZMsg *message, size_t expected_size)
{
    ZMsg *handle = zef_mutable_message(message);
    if (handle->payload_size != expected_size) {
        zef_fail(Z_PANIC_PAYLOAD_SIZE, handle, expected_size);
    }
    return (void *)handle->payload;
}

static ZMsg const *zef_readable_message(ZMsg const *message)
{
    if (message == NULL || !message->valid ||
        (message->kind != Z_MSG_DYNAMIC && message->kind != Z_MSG_STATIC)) {
        zef_fail(Z_PANIC_LIFECYCLE, message, 0u);
    }
    if (message->kind == Z_MSG_DYNAMIC && message->claims == 0u) {
        zef_fail(Z_PANIC_OWNERSHIP, message, 0u);
    }
    return message;
}

ZBool zef_message_is_readable(ZMsg const *message)
{
    return message != NULL && message->valid &&
           ((message->kind == Z_MSG_STATIC) ||
            (message->kind == Z_MSG_DYNAMIC && message->claims != 0u));
}

ZTopic z_message_topic(ZMsg const *message)
{
    return zef_readable_message(message)->topic;
}

size_t z_message_payload_size(ZMsg const *message)
{
    return zef_readable_message(message)->payload_size;
}

void const *z_message_payload(ZMsg const *message)
{
    return zef_readable_message(message)->payload;
}

void const *z_message_payload_checked(ZMsg const *message, size_t expected_size)
{
    ZMsg const *handle = zef_readable_message(message);
    if (handle->payload_size != expected_size) {
        zef_fail(Z_PANIC_PAYLOAD_SIZE, handle, expected_size);
    }
    return handle->payload;
}

/* @relation(ZEF-IF-011, scope=message-claims) */
void z_message_retain(ZMsg const *message)
{
    if (message == NULL || !message->valid) {
        zef_fail(Z_PANIC_OWNERSHIP, message, 0u);
    }
    if (message->kind == Z_MSG_STATIC) {
        return;
    }
    if (message->kind != Z_MSG_DYNAMIC || message->claims == 0u ||
        message->claims == (ZEF_CLAIM_TYPE)~(ZEF_CLAIM_TYPE)0u) {
        zef_fail(Z_PANIC_OWNERSHIP, message, 0u);
    }
    ++((ZMsg *)message)->claims;
}

void z_message_release(ZMsg const *message)
{
    if (message == NULL || !message->valid) {
        zef_fail(Z_PANIC_OWNERSHIP, message, 0u);
    }
    if (message->kind == Z_MSG_STATIC) {
        return;
    }
    if (message->kind != Z_MSG_DYNAMIC || message->claims == 0u || message->pool == NULL) {
        zef_fail(Z_PANIC_OWNERSHIP, message, 0u);
    }

    ZMsg *mutable_message = (ZMsg *)message;
    --mutable_message->claims;
    if (mutable_message->claims != 0u) {
        return;
    }

    ZMessagePool *pool = mutable_message->pool;
    mutable_message->kind = Z_MSG_RECLAIMED;
    mutable_message->valid = 0u;
    mutable_message->pool = pool;
    *(void **)mutable_message = pool->free_list;
    pool->free_list = mutable_message;
    if (pool->free_count == pool->block_count) {
        zef_fail(Z_PANIC_INTERNAL, pool, 0u);
    }
    ++pool->free_count;
}
