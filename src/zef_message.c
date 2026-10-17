#include "zef_internal.h"

#include <stdint.h>

static ZMessagePoolSet *g_default_message_pool_set;

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
    ZEF_REQUIRE(pool != NULL && storage != NULL && block_count != 0u &&
                    block_size != 0u && zef_aligned(storage) &&
                    block_size % ZEF_POOL_ALIGNMENT == 0u &&
                    block_size >= sizeof(ZMsg) &&
                    block_count <= storage_size / block_size,
                Z_PANIC_CONFIGURATION, pool, block_size);

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
    ZEF_REQUIRE(pool_set != NULL && pools != NULL && count != 0u &&
                    count <= Z_MAX_MESSAGE_POOLS,
                Z_PANIC_CONFIGURATION, pool_set, count);
    for (uint16_t index = 0u; index < count; ++index) {
        ZEF_REQUIRE(pools[index].initialized,
                    Z_PANIC_CONFIGURATION, &pools[index], index);
    }
    pool_set->pools = pools;
    pool_set->count = count;
}

void z_message_factory_init(ZMessagePoolSet *pool_set)
{
    ZEF_REQUIRE(pool_set != NULL,
                Z_PANIC_CONFIGURATION, pool_set, 0u);
    g_default_message_pool_set = pool_set;
}

/* @relation(ZEF-IF-010, scope=message-construction) */
void *z_message_begin(ZMessagePoolSet *pool_set, ZTopic topic, size_t payload_size,
                      ZMsg **message)
{
    ZEF_REQUIRE(pool_set != NULL && message != NULL && pool_set->pools != NULL &&
                    pool_set->count != 0u &&
                    payload_size <= SIZE_MAX - sizeof(ZMsg),
                Z_PANIC_INVALID_ARGUMENT, message, payload_size);

    size_t required = sizeof(ZMsg) + payload_size;
    ZCriticalState state = z_port_critical_enter();
    ZMessagePool *selected = NULL;
    for (uint16_t index = 0u; index < pool_set->count; ++index) {
        ZMessagePool *pool = &pool_set->pools[index];
        ZEF_REQUIRE(pool->initialized,
                    Z_PANIC_CONFIGURATION, pool, index);
        if (pool->block_size >= required && pool->free_count != 0u &&
            (selected == NULL || pool->block_size < selected->block_size)) {
            selected = pool;
        }
    }
    if (selected == NULL) {
        ZEF_PANIC(Z_PANIC_NO_MEMORY, pool_set, required);
    }

    void *block = selected->free_list;
    if (block == NULL || !zef_pool_contains(selected, block)) {
        ZEF_PANIC(Z_PANIC_INTERNAL, selected, 0u);
    }
    selected->free_list = *(void **)block;
    --selected->free_count;
    if (selected->free_count < selected->min_free) {
        selected->min_free = selected->free_count;
    }

    ZMsg *allocated = (ZMsg *)block;
    allocated->kind = Z_MSG_KIND_DYNAMIC;
    allocated->topic = topic;
    allocated->payload_size = payload_size;
    allocated->payload = (unsigned char *)block + sizeof(ZMsg);
    allocated->claims = 1u;
    allocated->pool = selected;
    allocated->valid = 1u;
    *message = allocated;
    z_port_critical_leave(state);
    return (void *)allocated->payload;
}

void *z_message_begin_default(ZTopic topic, size_t payload_size, ZMsg **message)
{
    ZEF_REQUIRE(g_default_message_pool_set != NULL,
                Z_PANIC_LIFECYCLE, NULL, 0u);
    return z_message_begin(g_default_message_pool_set, topic, payload_size, message);
}

/* This operation supplies the static immutable Message form described by WHAT-010. */
void z_message_static_init(ZMsg *message, ZTopic topic, void const *payload,
                           size_t payload_size)
{
    ZEF_REQUIRE(message != NULL && (payload != NULL || payload_size == 0u),
                Z_PANIC_INVALID_ARGUMENT, message, payload_size);
    message->kind = Z_MSG_KIND_STATIC;
    message->topic = topic;
    message->payload_size = payload_size;
    message->payload = payload != NULL ? payload : message;
    message->claims = 0u;
    message->pool = NULL;
    message->valid = 1u;
}

static ZMsg *zef_mutable_message(ZMsg *message)
{
    ZEF_REQUIRE(message != NULL && message->valid &&
                    message->kind == Z_MSG_KIND_DYNAMIC &&
                    message->pool != NULL,
                Z_PANIC_LIFECYCLE, message, 0u);
    return message;
}

void *z_message_payload_mut(ZMsg *message)
{
    return (void *)zef_mutable_message(message)->payload;
}

void *z_message_payload_mut_checked(ZMsg *message, size_t expected_size)
{
    ZMsg *handle = zef_mutable_message(message);
    ZEF_REQUIRE(handle->payload_size == expected_size,
                Z_PANIC_PAYLOAD_SIZE, handle, expected_size);
    return (void *)handle->payload;
}

static ZMsg const *zef_readable_message(ZMsg const *message)
{
    ZEF_REQUIRE(message != NULL && message->valid &&
                    (message->kind == Z_MSG_KIND_DYNAMIC ||
                     message->kind == Z_MSG_KIND_STATIC),
                Z_PANIC_LIFECYCLE, message, 0u);
    ZEF_REQUIRE(message->kind != Z_MSG_KIND_DYNAMIC || message->claims != 0u,
                Z_PANIC_OWNERSHIP, message, 0u);
    return message;
}

ZBool zef_message_is_readable(ZMsg const *message)
{
    return message != NULL && message->valid &&
           ((message->kind == Z_MSG_KIND_STATIC) ||
            (message->kind == Z_MSG_KIND_DYNAMIC && message->claims != 0u));
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
    ZEF_REQUIRE(handle->payload_size == expected_size,
                Z_PANIC_PAYLOAD_SIZE, handle, expected_size);
    return handle->payload;
}

/* @relation(ZEF-IF-011, scope=message-claims) */
void z_message_retain(ZMsg const *message)
{
    ZEF_REQUIRE(message != NULL, Z_PANIC_OWNERSHIP, message, 0u);
    ZCriticalState state = z_port_critical_enter();
    ZEF_REQUIRE(message->valid, Z_PANIC_OWNERSHIP, message, 0u);
    if (message->kind == Z_MSG_KIND_STATIC) {
        z_port_critical_leave(state);
        return;
    }
    ZEF_REQUIRE(message->kind == Z_MSG_KIND_DYNAMIC && message->claims != 0u &&
                    message->claims != (ZEF_CLAIM_TYPE)~(ZEF_CLAIM_TYPE)0u,
                Z_PANIC_OWNERSHIP, message, 0u);
    ++((ZMsg *)message)->claims;
    z_port_critical_leave(state);
}

void z_message_release(ZMsg const *message)
{
    ZEF_REQUIRE(message != NULL, Z_PANIC_OWNERSHIP, message, 0u);
    ZCriticalState state = z_port_critical_enter();
    ZEF_REQUIRE(message->valid, Z_PANIC_OWNERSHIP, message, 0u);
    if (message->kind == Z_MSG_KIND_STATIC) {
        z_port_critical_leave(state);
        return;
    }
    ZEF_REQUIRE(message->kind == Z_MSG_KIND_DYNAMIC && message->claims != 0u &&
                    message->pool != NULL,
                Z_PANIC_OWNERSHIP, message, 0u);

    ZMsg *mutable_message = (ZMsg *)message;
    --mutable_message->claims;
    if (mutable_message->claims != 0u) {
        z_port_critical_leave(state);
        return;
    }

    ZMessagePool *pool = mutable_message->pool;
    mutable_message->kind = Z_MSG_KIND_RECLAIMED;
    mutable_message->valid = 0u;
    mutable_message->pool = pool;
    *(void **)mutable_message = pool->free_list;
    pool->free_list = mutable_message;
    if (pool->free_count == pool->block_count) {
        ZEF_PANIC(Z_PANIC_INTERNAL, pool, 0u);
    }
    ++pool->free_count;
    z_port_critical_leave(state);
}
