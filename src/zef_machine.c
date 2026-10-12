#include "zef_internal.h"

static ZStateNode const *zef_first_child(ZStateNode const *node)
{
    return node->children != NULL ? node->children : node->initial;
}

static void zef_validate_sibling_chain(ZStateNode const *parent,
                                       ZStateNode const *first)
{
    ZStateNode const *slow = first;
    ZStateNode const *fast = first;
    while (fast != NULL && fast->sibling != NULL) {
        slow = slow->sibling;
        fast = fast->sibling->sibling;
        if (slow == fast) {
            zef_fail(Z_PANIC_STATE, parent, 0u);
        }
    }

    for (ZStateNode const *cursor = first; cursor != NULL; cursor = cursor->sibling) {
        if (cursor->parent != parent) {
            zef_fail(Z_PANIC_STATE, cursor, 0u);
        }
    }
}

static void zef_validate_graph_node(ZStateNode const *root,
                                    ZStateNode const *node)
{
    uint16_t depth = 0u;
    ZBool root_reached = 0u;
    for (ZStateNode const *cursor = node; cursor != NULL; cursor = cursor->parent) {
        if (++depth > Z_MAX_STATE_DEPTH) {
            zef_fail(Z_PANIC_STATE, node, depth);
        }
        if (cursor == root) {
            root_reached = 1u;
            break;
        }
    }
    if (!root_reached) {
        zef_fail(Z_PANIC_STATE, node, 0u);
    }

    ZStateNode const *first = zef_first_child(node);
    if (node->children != NULL && node->initial == NULL) {
        zef_fail(Z_PANIC_STATE, node, 0u);
    }
    if (first != NULL) {
        zef_validate_sibling_chain(node, first);
        ZBool initial_found = 0u;
        for (ZStateNode const *cursor = first; cursor != NULL; cursor = cursor->sibling) {
            if (cursor == node->initial) {
                initial_found = 1u;
            }
            zef_validate_graph_node(root, cursor);
        }
        if (!initial_found) {
            zef_fail(Z_PANIC_STATE, node->initial, 0u);
        }
    }
}

static void zef_validate_parent_cycles(ZStateNode const *root)
{
    ZStateNode const *slow = root;
    ZStateNode const *fast = root;
    while (fast != NULL && fast->parent != NULL) {
        slow = slow->parent;
        fast = fast->parent->parent;
        if (slow == fast) {
            zef_fail(Z_PANIC_STATE, root, 0u);
        }
    }
}

/* @relation(ZEF-IF-020, scope=state-machine) */
void z_machine_init(ZMachine *machine, ZStateNode const *root, void *context)
{
    if (machine == NULL || root == NULL || root->parent != NULL) {
        zef_fail(Z_PANIC_STATE, machine, 0u);
    }
    zef_validate_parent_cycles(root);
    zef_validate_graph_node(root, root);
    machine->root = root;
    machine->current = root;
    machine->context = context;
    machine->initialized = 1u;
    machine->started = 0u;
}

static void zef_machine_enter_initial(ZMachine *machine)
{
    uint16_t depth = 0u;
    while (machine->current->initial != NULL) {
        if (++depth >= Z_MAX_STATE_DEPTH) {
            zef_fail(Z_PANIC_STATE, machine->current, depth);
        }
        ZStateNode const *next = machine->current->initial;
        if (next->parent != machine->current) {
            zef_fail(Z_PANIC_STATE, next, 0u);
        }
        machine->current = next;
        if (next->enter != NULL) {
            next->enter(machine->context);
        }
    }
}

static void zef_machine_validate_child_depth(ZMachine const *machine)
{
    uint16_t depth = 0u;
    ZStateNode const *cursor = machine->current;
    while (cursor != NULL) {
        if (++depth >= Z_MAX_STATE_DEPTH) {
            zef_fail(Z_PANIC_STATE, cursor, depth + 1u);
        }
        if (cursor == machine->root) {
            return;
        }
        cursor = cursor->parent;
    }
    zef_fail(Z_PANIC_STATE, machine->current, 0u);
}

void z_machine_start(ZMachine *machine)
{
    if (machine == NULL || !machine->initialized) {
        zef_fail(Z_PANIC_LIFECYCLE, machine, 0u);
    }
    if (machine->started) {
        zef_fail(Z_PANIC_LIFECYCLE, machine, 1u);
    }
    machine->started = 1u;
    machine->current = machine->root;
    if (machine->root->enter != NULL) {
        machine->root->enter(machine->context);
    }
    zef_machine_enter_initial(machine);
}

static void zef_machine_transition_general(ZMachine *machine,
                                           ZStateNode const *target)
{
    ZStateNode const *source_path[Z_MAX_STATE_DEPTH];
    ZStateNode const *target_path[Z_MAX_STATE_DEPTH];
    uint16_t source_length = 0u;
    uint16_t target_length = 0u;
    ZBool source_root_found = 0u;
    ZBool target_root_found = 0u;
    ZStateNode const *cursor = machine->current;
    while (cursor != NULL) {
        if (source_length >= Z_MAX_STATE_DEPTH) {
            zef_fail(Z_PANIC_STATE, cursor, Z_MAX_STATE_DEPTH);
        }
        source_path[source_length++] = cursor;
        if (cursor == machine->root) {
            source_root_found = 1u;
            break;
        }
        cursor = cursor->parent;
    }
    if (!source_root_found) {
        zef_fail(Z_PANIC_STATE, machine->current, 0u);
    }

    cursor = target;
    while (cursor != NULL) {
        if (target_length >= Z_MAX_STATE_DEPTH) {
            zef_fail(Z_PANIC_STATE, cursor, Z_MAX_STATE_DEPTH);
        }
        target_path[target_length++] = cursor;
        if (cursor == machine->root) {
            target_root_found = 1u;
            break;
        }
        cursor = cursor->parent;
    }
    if (!target_root_found) {
        zef_fail(Z_PANIC_STATE, target, 0u);
    }

    int source_common = (int)source_length - 1;
    int target_common = (int)target_length - 1;
    while (source_common > 0 && target_common > 0 &&
           source_path[source_common - 1] == target_path[target_common - 1]) {
        --source_common;
        --target_common;
    }

    for (int index = 0; index < source_common; ++index) {
        if (source_path[index]->leave != NULL) {
            source_path[index]->leave(machine->context);
        }
    }
    machine->current = target;
    for (int index = target_common - 1; index >= 0; --index) {
        if (target_path[index]->enter != NULL) {
            target_path[index]->enter(machine->context);
        }
    }
    zef_machine_enter_initial(machine);
}

static void zef_machine_transition(ZMachine *machine, ZStateNode const *target)
{
    if (target == NULL) {
        zef_fail(Z_PANIC_STATE, machine, 0u);
    }

    ZStateNode const *source = machine->current;
    if (target == source) {
        if (source->leave != NULL) {
            source->leave(machine->context);
        }
        if (source->enter != NULL) {
            source->enter(machine->context);
        }
        zef_machine_enter_initial(machine);
        return;
    }

    if (target->parent == source) {
        zef_machine_validate_child_depth(machine);
        machine->current = target;
        if (target->enter != NULL) {
            target->enter(machine->context);
        }
        zef_machine_enter_initial(machine);
        return;
    }

    if (source->parent == target) {
        if (source->leave != NULL) {
            source->leave(machine->context);
        }
        machine->current = target;
        zef_machine_enter_initial(machine);
        return;
    }

    if (source->parent != NULL && source->parent == target->parent) {
        if (source->leave != NULL) {
            source->leave(machine->context);
        }
        machine->current = target;
        if (target->enter != NULL) {
            target->enter(machine->context);
        }
        zef_machine_enter_initial(machine);
        return;
    }

    zef_machine_transition_general(machine, target);
}

void z_machine_dispatch(ZMachine *machine, ZMsg const *message)
{
    if (machine == NULL || !machine->initialized || !machine->started) {
        zef_fail(Z_PANIC_LIFECYCLE, machine, 0u);
    }
    if (!zef_message_is_readable(message)) {
        zef_fail(Z_PANIC_INVALID_ARGUMENT, message, 0u);
    }

    ZStateNode const *cursor = machine->current;
    while (cursor != NULL) {
        ZAction action = { Z_ACTION_UNHANDLED, NULL };
        if (cursor->handle != NULL) {
            action = cursor->handle(machine->context, message);
        }
        if (action.kind == Z_ACTION_UNHANDLED) {
            if (action.target != NULL) {
                zef_fail(Z_PANIC_STATE, cursor, 0u);
            }
            cursor = cursor->parent;
            continue;
        }
        if (action.kind == Z_ACTION_HANDLED) {
            if (action.target != NULL) {
                zef_fail(Z_PANIC_STATE, cursor, 0u);
            }
            return;
        }
        if (action.kind != Z_ACTION_TRANSITION || action.target == NULL) {
            zef_fail(Z_PANIC_STATE, cursor, 0u);
        }
        zef_machine_transition(machine, action.target);
        return;
    }
}

ZStateNode const *z_machine_state(ZMachine const *machine)
{
    if (machine == NULL || !machine->initialized || !machine->started) {
        zef_fail(Z_PANIC_LIFECYCLE, machine, 0u);
    }
    return machine->current;
}
