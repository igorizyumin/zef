#ifndef ZEF_H
#define ZEF_H

#include "zef_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t ZBool;
typedef uint16_t ZTopic;
typedef uint16_t ZReactorId;
typedef uint8_t ZPriority; /* strict priority value; equal to the Reactor ID */
typedef ZEF_TICK_TYPE ZTick;
typedef uint32_t ZCriticalState;

typedef enum {
    Z_OK = 0,
    Z_EMPTY,
    Z_NOT_RUNNING,
    Z_NOT_FOUND
} ZResult;

typedef enum {
    Z_PANIC_INVALID_ARGUMENT,
    Z_PANIC_CONFIGURATION,
    Z_PANIC_NO_MEMORY,
    Z_PANIC_MAILBOX_FULL,
    Z_PANIC_OWNERSHIP,
    Z_PANIC_LIFECYCLE,
    Z_PANIC_STATE,
    Z_PANIC_OVERFLOW,
    Z_PANIC_PAYLOAD_SIZE,
    Z_PANIC_INTERNAL
} ZPanicReason;

typedef struct {
    ZPanicReason reason;
    void const *object;
    uintptr_t detail;
    char const *file;
    uint32_t line;
} ZPanicInfo;

typedef void (*ZPanicHandler)(ZPanicInfo const *info);

/* Applications must provide this non-returning panic hook. */
_Noreturn void z_panic(ZPanicInfo const *info);

/* GCC and Clang provide the basename-only extension; retain portability for
 * compilers such as MSVC that only provide the standard __FILE__. */
#if defined(__FILE_NAME__)
#define ZEF_SOURCE_FILE __FILE_NAME__
#else
#define ZEF_SOURCE_FILE __FILE__
#endif

/* Small helpers for application- and framework-side validation. */
#define ZEF_PANIC(reason_, object_, detail_)                                      \
    do {                                                                          \
        ZPanicInfo zef_panic_info_ = {                                            \
            (reason_), (object_), (detail_), ZEF_SOURCE_FILE, (uint32_t)__LINE__  \
        };                                                                        \
        z_panic(&zef_panic_info_);                                                \
    } while (0)

#if ZEF_ENABLE_ASSERTIONS
#define ZEF_REQUIRE(condition_, reason_, object_, detail_)                       \
    do {                                                                          \
        if (!(condition_)) {                                                      \
            ZEF_PANIC((reason_), (object_), (detail_));                           \
        }                                                                         \
    } while (0)
#else
/* Disabled builds omit both the condition evaluation and panic metadata. */
#define ZEF_REQUIRE(condition_, reason_, object_, detail_)                       \
    do {                                                                          \
        (void)sizeof(condition_);                                                 \
    } while (0)
#endif

typedef struct ZMessagePool ZMessagePool;
typedef struct ZMessagePoolSet ZMessagePoolSet;
typedef struct ZMsg ZMsg;
typedef struct ZMailbox ZMailbox;
typedef struct ZStateNode ZStateNode;
typedef struct ZMachine ZMachine;
typedef struct ZReactor ZReactor;
typedef struct ZActor ZActor;
typedef struct ZKernel ZKernel;
typedef struct ZTopicTable ZTopicTable;
typedef struct ZClock ZClock;
typedef struct ZTimer ZTimer;
typedef struct ZPort ZPort;

typedef enum {
    Z_MSG_KIND_DYNAMIC = 0,
    Z_MSG_KIND_STATIC = 1,
    Z_MSG_KIND_RECLAIMED = 2
} ZMessageKind;

/* Framework-owned members are reserved; use the public operations below. */
struct ZMessagePool {
    void *storage;
    size_t storage_size;
    size_t block_size;
    uint16_t block_count;
    uint16_t free_count;
    uint16_t min_free;
    void *free_list;
    ZBool initialized;
};

struct ZMessagePoolSet {
    ZMessagePool *pools;
    uint16_t count;
};

/* A message is mutable only while held through its construction pointer.
 * Handlers receive borrowed const pointers; retain extends their lifetime. */
struct ZMsg {
    ZMessageKind kind;
    ZTopic topic;
    size_t payload_size;
    void const *payload;
    ZEF_CLAIM_TYPE claims;
    ZMessagePool *pool;
    ZBool valid;
};

struct ZMailbox {
    ZMsg const **slots;
    uint16_t capacity;
    uint32_t head;
    uint32_t tail;
    uint16_t min_free;
    ZReactor *owner;             /* reserved: set by Reactor registration */
    ZBool initialized;          /* reserved */
};

typedef enum {
    Z_ACTION_UNHANDLED,
    Z_ACTION_HANDLED,
    Z_ACTION_TRANSITION
} ZActionKind;

typedef struct {
    ZActionKind kind;
    ZStateNode const *target;
} ZAction;

static inline ZAction z_action_unhandled(void)
{
    return (ZAction){ .kind = Z_ACTION_UNHANDLED, .target = NULL };
}

static inline ZAction z_action_handled(void)
{
    return (ZAction){ .kind = Z_ACTION_HANDLED, .target = NULL };
}

static inline ZAction z_action_transition(ZStateNode const *target)
{
    return (ZAction){ .kind = Z_ACTION_TRANSITION, .target = target };
}

typedef ZAction (*ZStateHandler)(void *context, ZMsg const *message);
typedef void (*ZStateHook)(void *context);

/* Declare typed state callbacks while keeping the generic Machine ABI. */
#define Z_STATE_HANDLER(Type_, name_)                                             \
    static ZAction name_(Type_ *ctx, ZMsg const *message);                        \
    static ZAction name_##_zef_adapter(void *context, ZMsg const *message)         \
    {                                                                              \
        return name_((Type_ *)context, message);                                   \
    }                                                                              \
    static ZAction name_(Type_ *ctx, ZMsg const *message)

#define Z_STATE_HOOK(Type_, name_)                                                 \
    static void name_(Type_ *ctx);                                                 \
    static void name_##_zef_adapter(void *context)                                 \
    {                                                                              \
        name_((Type_ *)context);                                                   \
    }                                                                              \
    static void name_(Type_ *ctx)

struct ZStateNode {
    ZStateNode const *parent;
    ZStateNode const *initial;
    ZStateHandler handle;
    ZStateHook enter;
    ZStateHook leave;
};

struct ZMachine {
    ZStateNode const *root;      /* reserved */
    ZStateNode const *current;   /* reserved */
    void *context;               /* application context */
    ZBool initialized;           /* reserved */
    ZBool started;               /* reserved */
};

typedef struct {
    ZMachine *machine;
    ZMailbox *mailbox;
    void *context;
} ZReactorConfig;

struct ZReactor {
    ZMachine *machine;           /* reserved after init */
    ZMailbox *mailbox;           /* reserved after init */
    void *context;               /* application context */
    ZKernel *kernel;             /* reserved */
    ZReactorId id;               /* reserved */
    ZPriority priority;          /* reserved: equal to id */
    ZBool initialized;           /* reserved */
    ZBool registered;            /* reserved */
    ZBool started;               /* reserved */
};

/* A convenience composition of one Machine, Mailbox, and Reactor. */
struct ZActor {
    ZReactor reactor;
    ZMailbox mailbox;
    ZMachine machine;
};

struct ZPort {
    void (*request_reschedule)(void);
    void (*idle)(void);
};

typedef enum {
    Z_NO_WORK = 0,
    Z_WORK_DONE
} ZRunResult;

struct ZKernel {
    ZPort const *port;            /* reserved */
    ZReactor *reactors[Z_MAX_REACTORS]; /* reserved */
    uint32_t runnable;            /* reserved */
    uint16_t reactor_count;       /* reserved: number of registered Reactors */
    uint16_t reactor_limit;       /* reserved: one past highest assigned ID */
    ZPriority active_priority;    /* reserved */
    ZBool dispatching;            /* reserved */
    ZBool initialized;            /* reserved */
};

struct ZTopicTable {
    ZKernel *kernel;              /* reserved */
    uint32_t subscribers[Z_MAX_TOPICS]; /* reserved */
    ZBool initialized;            /* reserved */
};

struct ZClock {
    ZKernel *kernel;              /* reserved */
    ZTick now;                    /* reserved */
    ZTimer *timers;               /* reserved */
    ZTimer *timer_tail;           /* reserved */
    ZBool initialized;            /* reserved */
};

struct ZTimer {
    ZClock *clock;                /* reserved */
    ZReactorId destination;       /* reserved */
    ZMsg const *message;          /* reserved */
    ZMsg topic_message;           /* reserved static storage */
    ZTick due;                    /* reserved */
    ZTick period;                 /* reserved */
    ZBool initialized;            /* reserved */
    ZBool armed;                  /* reserved */
    ZBool base_claim;             /* reserved */
    ZBool cancelled;              /* reserved */
    ZTimer *next;                 /* reserved */
};

typedef ZBool (*ZMsgPredicate)(ZMsg const *message, void *context);

/* Message pools and payloads. */
/* These target-port hooks are global to the processor, not owned by a Kernel.
 * They must preserve and restore the previous interrupt/critical state and
 * support nesting. The library supplies a no-op host default when a target
 * port does not override them. */
ZCriticalState z_port_critical_enter(void);
void z_port_critical_leave(ZCriticalState state);

void z_message_pool_init(ZMessagePool *pool, void *storage, size_t storage_size,
                         size_t block_size, uint16_t block_count);
void z_message_pool_set_init(ZMessagePoolSet *pool_set, ZMessagePool *pools,
                              uint16_t count);
/* Selects the application-wide pool set used by Z_MSG_NEW. */
void z_message_factory_init(ZMessagePoolSet *pool_set);
/* Stores the allocated Message through *message and returns its mutable payload. */
void *z_message_begin(ZMessagePoolSet *pool_set, ZTopic topic, size_t payload_size,
                      ZMsg **message);
/* Begins a message using the pool set configured by z_message_factory_init. */
void *z_message_begin_default(ZTopic topic, size_t payload_size, ZMsg **message);
void z_message_static_init(ZMsg *message, ZTopic topic,
                           void const *payload, size_t payload_size);
#define Z_MSG_STATIC(name_, topic_) \
    static ZMsg const name_ = { \
        .kind = Z_MSG_KIND_STATIC, \
        .topic = (topic_), \
        .payload_size = 0u, \
        .payload = NULL, \
        .claims = 0u, \
        .pool = NULL, \
        .valid = 1u \
    }
void *z_message_payload_mut(ZMsg *message);
void *z_message_payload_mut_checked(ZMsg *message, size_t expected_size);
ZTopic z_message_topic(ZMsg const *message);
size_t z_message_payload_size(ZMsg const *message);
void const *z_message_payload(ZMsg const *message);
void const *z_message_payload_checked(ZMsg const *message, size_t expected_size);
void z_message_retain(ZMsg const *message);
void z_message_release(ZMsg const *message);

#define Z_MSG_BEGIN(pool_set_, topic_, Type_, msg_) \
    ((Type_ *)z_message_begin((pool_set_), (topic_), sizeof(Type_), (msg_)))
#define Z_MSG_NEW(topic_, Type_, msg_) \
    ((Type_ *)z_message_begin_default((topic_), sizeof(Type_), (msg_)))
#define Z_MSG_PAYLOAD(message_, Type_) \
    ((Type_ const *)z_message_payload_checked((message_), sizeof(Type_)))
#define Z_MSG_PAYLOAD_MUT(msg_, Type_) \
    ((Type_ *)z_message_payload_mut_checked((msg_), sizeof(Type_)))

/* Mailboxes. */
void z_mailbox_init(ZMailbox *mailbox, ZMsg const **slots, uint16_t capacity);
/* ISR-safe. Transfers *message to the Mailbox and sets the caller's pointer
 * to NULL. */
void z_mailbox_post(ZMailbox *mailbox, ZMsg **message);
/* ISR-safe convenience wrapper around z_mailbox_post. Transfers a message
 * pointer variable to an Actor's Mailbox and nulls it. The variable may point
 * to a dynamic or static Message. */
#define Z_MSG_POST(actor_, msg_)                                      \
    do {                                                              \
        ZMsg *zef_post_message_ = (ZMsg *)(msg_);                    \
        z_mailbox_post(&(actor_)->mailbox, &zef_post_message_);       \
        (msg_) = zef_post_message_;                                  \
    } while (0)
void z_mailbox_post_front(ZMailbox *mailbox, ZMsg **message);
void z_mailbox_postpone(ZMailbox *mailbox, ZMsg const *message);
/* Move the newest Message from an unassigned source to the front of
 * destination. */
ZResult z_mailbox_recall(ZMailbox *destination, ZMailbox *source);
ZResult z_mailbox_take(ZMailbox *mailbox, ZMsg const **message_out);
uint16_t z_mailbox_count(ZMailbox const *mailbox);
uint16_t z_mailbox_capacity(ZMailbox const *mailbox);
uint16_t z_mailbox_free(ZMailbox const *mailbox);
uint16_t z_mailbox_min_free(ZMailbox const *mailbox);
uint16_t z_mailbox_remove_if(ZMailbox *mailbox, ZMsgPredicate predicate, void *context);

/* Machines. */
void z_machine_init(ZMachine *machine, ZStateNode const *root, void *context);
void z_machine_start(ZMachine *machine);
void z_machine_dispatch(ZMachine *machine, ZMsg const *message);
ZStateNode const *z_machine_state(ZMachine const *machine);

/* Reactors and cooperative scheduling. */
void z_reactor_init(ZReactor *reactor, ZReactorConfig const *config);
/* The explicit ID is also the Reactor's strict priority; larger IDs run first. */
ZReactorId z_reactor_register(ZKernel *kernel, ZReactor *reactor, ZReactorId id);
void z_reactor_start(ZKernel *kernel, ZReactorId id);
void z_actor_init(ZActor *actor, ZStateNode const *root, void *context,
                  ZMsg const **mailbox_slots, uint16_t mailbox_capacity);
ZReactorId z_actor_register(ZKernel *kernel, ZActor *actor, ZReactorId id);
void z_actor_start(ZKernel *kernel, ZActor *actor);
void z_kernel_init(ZKernel *kernel, ZPort const *port);
ZRunResult z_kernel_run_one(ZKernel *kernel);
void z_kernel_run_forever(ZKernel *kernel);
ZResult z_kernel_current_priority(ZKernel const *kernel, ZPriority *priority_out);

/* Topics. */
void z_topic_table_init(ZTopicTable *table, ZKernel *kernel);
void z_topic_subscribe(ZTopicTable *table, ZTopic topic, ZReactorId reactor_id);
ZResult z_topic_unsubscribe(ZTopicTable *table, ZTopic topic, ZReactorId reactor_id);
uint16_t z_topic_publish(ZKernel *kernel, ZTopicTable *table, ZMsg **message);

/* Clock and timers. */
void z_clock_init(ZClock *clock, ZKernel *kernel);
void z_timer_init_topic(ZTimer *timer, ZClock *clock, ZReactorId destination,
                        ZTopic topic);
void z_timer_init_payload(ZTimer *timer, ZClock *clock, ZReactorId destination,
                          ZMessagePoolSet *pool_set, ZTopic topic,
                          void const *payload, size_t payload_size);
void z_timer_arm(ZTimer *timer, ZTick delay, ZTick period);
ZResult z_timer_stop(ZTimer *timer);
ZResult z_timer_cancel(ZTimer *timer);
void z_clock_tick(ZClock *clock);

#ifdef __cplusplus
}
#endif

#endif
