#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

/* Variable types in SIMPL */
typedef enum {
    TYPE_INT,
    TYPE_STACK,
    TYPE_QUEUE,
    TYPE_TREE,
    TYPE_GRAPH,
    TYPE_UNKNOWN = -1
} Type;

/* ============================================================
 * ADT STATE TRACKING (Compile-Time Semantic Simulation)
 * This is something GCC CANNOT do for C programs!
 * ============================================================ */

/* Maximum tracked elements for compile-time simulation */
#define MAX_TRACKED_SIZE 1000
#define MAX_GRAPH_NODES 100

/* Graph edge representation */
typedef struct {
    int from;
    int to;
    int exists;
} GraphEdge;

/* ADT state for compile-time tracking */
typedef struct {
    int size;                           /* Current size (stack/queue) */
    int min_possible_size;              /* Minimum possible size (for branches) */
    int is_size_known;                  /* Can we track size statically? */
    
    /* Graph-specific tracking */
    GraphEdge edges[MAX_GRAPH_NODES * MAX_GRAPH_NODES];
    int edge_count;
    int nodes[MAX_GRAPH_NODES];
    int node_count;
} ADTState;

/* Symbol table entry */
typedef struct Symbol {
    char *name;
    Type type;
    ADTState adt_state;                 /* Runtime state simulation */
    struct Symbol *next;
} Symbol;

/* Initialize the symbol table */
void symtab_init(void);

/* Insert a new symbol (returns 0 on success, -1 if already exists) */
int symtab_insert(const char *name, Type type);

/* Lookup a symbol (returns TYPE_UNKNOWN if not found) */
Type symtab_lookup(const char *name);

/* Check if a symbol exists */
int symtab_exists(const char *name);

/* Get type name as string (for error messages) */
const char *type_to_string(Type t);

/* ============================================================
 * ADT STATE MANIPULATION (Compile-Time Simulation)
 * ============================================================ */

/* Get ADT state for a variable */
ADTState *symtab_get_adt_state(const char *name);

/* Stack operations */
int adt_stack_push(const char *name);      /* Returns new size, -1 on error */
int adt_stack_pop(const char *name);       /* Returns new size, -1 on underflow */
int adt_stack_size(const char *name);      /* Returns current size */

/* Queue operations */
int adt_queue_enqueue(const char *name);
int adt_queue_dequeue(const char *name);   /* Returns -1 on underflow */
int adt_queue_size(const char *name);

/* Graph operations */
int adt_graph_add_node(const char *name, int node);
int adt_graph_has_node(const char *name, int node);
int adt_graph_add_edge(const char *name, int from, int to);
int adt_graph_has_edge(const char *name, int from, int to);
int adt_graph_remove_edge(const char *name, int from, int to);  /* Returns -1 if doesn't exist */

/* Mark ADT state as unknown (after conditionals) */
void adt_mark_unknown(const char *name);

#endif
