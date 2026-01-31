#include "symbol_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Symbol *table = NULL;

void symtab_init(void) {
    /* Free existing table */
    Symbol *s = table;
    while (s) {
        Symbol *next = s->next;
        free(s->name);
        free(s);
        s = next;
    }
    table = NULL;
}

int symtab_insert(const char *name, Type type) {
    /* Check for redeclaration */
    if (symtab_exists(name)) {
        return -1;
    }

    Symbol *s = malloc(sizeof(Symbol));
    if (!s) return -1;

    s->name = strdup(name);
    s->type = type;
    s->next = table;

    /* Initialize ADT state */
    s->adt_state.size = 0;
    s->adt_state.min_possible_size = 0;
    s->adt_state.is_size_known = 1;  /* Initially known (size = 0) */
    s->adt_state.edge_count = 0;
    s->adt_state.node_count = 0;
    memset(s->adt_state.edges, 0, sizeof(s->adt_state.edges));
    memset(s->adt_state.nodes, 0, sizeof(s->adt_state.nodes));

    table = s;
    return 0;
}

static Symbol *find_symbol(const char *name) {
    for (Symbol *s = table; s; s = s->next) {
        if (strcmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}

Type symtab_lookup(const char *name) {
    Symbol *s = find_symbol(name);
    return s ? s->type : TYPE_UNKNOWN;
}

int symtab_exists(const char *name) {
    return symtab_lookup(name) != TYPE_UNKNOWN;
}

const char *type_to_string(Type t) {
    switch (t) {
        case TYPE_INT:     return "int";
        case TYPE_STACK:   return "stack";
        case TYPE_QUEUE:   return "queue";
        case TYPE_TREE:    return "tree";
        case TYPE_GRAPH:   return "graph";
        case TYPE_UNKNOWN: return "unknown";
        default:           return "invalid";
    }
}

/* ============================================================
 * ADT STATE MANIPULATION (Compile-Time Simulation)
 * This enables detection of underflow, invalid operations, etc.
 * at COMPILE TIME - something GCC cannot do!
 * ============================================================ */

ADTState *symtab_get_adt_state(const char *name) {
    Symbol *s = find_symbol(name);
    return s ? &s->adt_state : NULL;
}

/* Stack operations */
int adt_stack_push(const char *name) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_STACK) return -1;
    if (!s->adt_state.is_size_known) return -2;  /* Unknown state */
    
    s->adt_state.size++;
    return s->adt_state.size;
}

int adt_stack_pop(const char *name) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_STACK) return -1;
    if (!s->adt_state.is_size_known) return -2;  /* Unknown state */
    
    if (s->adt_state.size <= 0) {
        return -1;  /* UNDERFLOW DETECTED AT COMPILE TIME! */
    }
    
    s->adt_state.size--;
    return s->adt_state.size;
}

int adt_stack_size(const char *name) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_STACK) return -1;
    if (!s->adt_state.is_size_known) return -2;
    return s->adt_state.size;
}

/* Queue operations */
int adt_queue_enqueue(const char *name) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_QUEUE) return -1;
    if (!s->adt_state.is_size_known) return -2;
    
    s->adt_state.size++;
    return s->adt_state.size;
}

int adt_queue_dequeue(const char *name) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_QUEUE) return -1;
    if (!s->adt_state.is_size_known) return -2;
    
    if (s->adt_state.size <= 0) {
        return -1;  /* UNDERFLOW DETECTED AT COMPILE TIME! */
    }
    
    s->adt_state.size--;
    return s->adt_state.size;
}

int adt_queue_size(const char *name) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_QUEUE) return -1;
    if (!s->adt_state.is_size_known) return -2;
    return s->adt_state.size;
}

/* Graph operations */
int adt_graph_add_node(const char *name, int node) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_GRAPH) return -1;
    
    /* Check if node already exists */
    for (int i = 0; i < s->adt_state.node_count; i++) {
        if (s->adt_state.nodes[i] == node) {
            return 0;  /* Already exists */
        }
    }
    
    if (s->adt_state.node_count >= MAX_GRAPH_NODES) return -1;
    
    s->adt_state.nodes[s->adt_state.node_count++] = node;
    return 1;
}

int adt_graph_has_node(const char *name, int node) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_GRAPH) return -1;
    
    for (int i = 0; i < s->adt_state.node_count; i++) {
        if (s->adt_state.nodes[i] == node) {
            return 1;
        }
    }
    return 0;
}

int adt_graph_add_edge(const char *name, int from, int to) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_GRAPH) return -1;
    
    /* Auto-add nodes if they don't exist */
    adt_graph_add_node(name, from);
    adt_graph_add_node(name, to);
    
    /* Check if edge already exists */
    for (int i = 0; i < s->adt_state.edge_count; i++) {
        if (s->adt_state.edges[i].from == from && 
            s->adt_state.edges[i].to == to &&
            s->adt_state.edges[i].exists) {
            return 0;  /* Already exists */
        }
    }
    
    /* Add new edge */
    if (s->adt_state.edge_count >= MAX_GRAPH_NODES * MAX_GRAPH_NODES) return -1;
    
    s->adt_state.edges[s->adt_state.edge_count].from = from;
    s->adt_state.edges[s->adt_state.edge_count].to = to;
    s->adt_state.edges[s->adt_state.edge_count].exists = 1;
    s->adt_state.edge_count++;
    return 1;
}

int adt_graph_has_edge(const char *name, int from, int to) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_GRAPH) return -1;
    
    for (int i = 0; i < s->adt_state.edge_count; i++) {
        if (s->adt_state.edges[i].from == from && 
            s->adt_state.edges[i].to == to &&
            s->adt_state.edges[i].exists) {
            return 1;
        }
    }
    return 0;
}

int adt_graph_remove_edge(const char *name, int from, int to) {
    Symbol *s = find_symbol(name);
    if (!s || s->type != TYPE_GRAPH) return -1;
    
    for (int i = 0; i < s->adt_state.edge_count; i++) {
        if (s->adt_state.edges[i].from == from && 
            s->adt_state.edges[i].to == to &&
            s->adt_state.edges[i].exists) {
            s->adt_state.edges[i].exists = 0;
            return 1;
        }
    }
    return -1;  /* EDGE DOESN'T EXIST - DETECTED AT COMPILE TIME! */
}

void adt_mark_unknown(const char *name) {
    Symbol *s = find_symbol(name);
    if (s) {
        s->adt_state.is_size_known = 0;
    }
}
