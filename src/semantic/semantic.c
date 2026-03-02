/* ============================================================
 * SIMPL Semantic Analyzer
 * ============================================================
 * 
 * This module performs semantic analysis that GCC CANNOT do:
 * 
 * 1. ADT Safety Guarantees
 * 2. Compile-Time Stack/Queue Underflow Detection
 * 3. Graph Edge Existence Tracking
 * 4. Transparent Optimization Decisions
 * 5. Semantic ADT Optimization
 * 
 * ============================================================ */

#include "semantic.h"
#include "symbol_table.h"
#include "../parser/ast.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 * GLOBAL STATE
 * ============================================================ */

static int error_count = 0;
static int warning_count = 0;
static int optimization_count = 0;
static int declaration_count = 0;
static int assignment_count = 0;
static int arithmetic_ops = 0;
static int adt_ops = 0;
static int constant_exprs = 0;
static int current_loop_depth = 0;
static int max_loop_depth = 0;
static SemanticOptions current_opts = {0, 0, 0};

/* ============================================================
 * ERROR/WARNING REPORTING
 * ============================================================ */

static void semantic_error(const char *msg, const char *name) {
    fprintf(stderr, "Semantic error: %s", msg);
    if (name) {
        fprintf(stderr, " '%s'", name);
    }
    fprintf(stderr, "\n");
    error_count++;
}

static void semantic_warning(const char *msg, const char *name) {
    fprintf(stderr, "Warning: %s", msg);
    if (name) {
        fprintf(stderr, " '%s'", name);
    }
    fprintf(stderr, "\n");
    warning_count++;
}

static void optimization_info(const char *msg) {
    if (current_opts.show_optimizations) {
        fprintf(stdout, "[OPT] %s\n", msg);
    }
    optimization_count++;
}

/* ============================================================
 * HELPER FUNCTIONS
 * ============================================================ */

static Type adt_code_to_type(int code) {
    switch (code) {
        case ADT_STACK: return TYPE_STACK;
        case ADT_QUEUE: return TYPE_QUEUE;
        case ADT_TREE:  return TYPE_TREE;
        case ADT_GRAPH: return TYPE_GRAPH;
        default:        return TYPE_UNKNOWN;
    }
}

static int is_adt_type(Type t) {
    return t == TYPE_STACK || t == TYPE_QUEUE || 
           t == TYPE_TREE || t == TYPE_GRAPH;
}

static int is_valid_adt_op(int op_code, Type type) {
    switch (op_code) {
        case OP_PUSH:
        case OP_POP:
            return type == TYPE_STACK;
        case OP_ENQUEUE:
        case OP_DEQUEUE:
            return type == TYPE_QUEUE;
        case OP_INSERT:
        case OP_REMOVE:
            return type == TYPE_TREE;
        case OP_ADD_EDGE:
        case OP_REMOVE_EDGE:
            return type == TYPE_GRAPH;
        default:
            return 0;
    }
}

static const char *op_code_to_string(int op_code) {
    switch (op_code) {
        case OP_PUSH:        return "push";
        case OP_POP:         return "pop";
        case OP_ENQUEUE:     return "enqueue";
        case OP_DEQUEUE:     return "dequeue";
        case OP_INSERT:      return "insert";
        case OP_REMOVE:      return "remove";
        case OP_ADD_EDGE:    return "add_edge";
        case OP_REMOVE_EDGE: return "remove_edge";
        default:             return "unknown_op";
    }
}

static const char *expected_type_for_op(int op_code) {
    switch (op_code) {
        case OP_PUSH:
        case OP_POP:         return "stack";
        case OP_ENQUEUE:
        case OP_DEQUEUE:     return "queue";
        case OP_INSERT:
        case OP_REMOVE:      return "tree";
        case OP_ADD_EDGE:
        case OP_REMOVE_EDGE: return "graph";
        default:             return "unknown";
    }
}

static int get_constant_value(ASTNode *node, int *value) {
    if (!node) return 0;
    if (node->type == AST_NUMBER) {
        *value = node->value;
        return 1;
    }
    return 0;
}

/* ============================================================
 * FEATURE 1: ADT SAFETY GUARANTEES
 * GCC cannot enforce these - C has no semantic model of ADTs
 * ============================================================ */

static Type check_expression_type(ASTNode *node) {
    if (!node) return TYPE_UNKNOWN;

    switch (node->type) {
        case AST_NUMBER:
            node->inferred_type = TYPE_INT;
            return TYPE_INT;

        case AST_IDENTIFIER: {
            Type t = symtab_lookup(node->name);
            if (t == TYPE_UNKNOWN) {
                semantic_error("undeclared variable", node->name);
            }
            node->inferred_type = t;
            return t;
        }

        case AST_BINOP: {
            Type left_type = check_expression_type(node->left);
            Type right_type = check_expression_type(node->right);

            if (node->op == '+' || node->op == '-' ||
                node->op == '*' || node->op == '/') {
                arithmetic_ops++;
                if (left_type != TYPE_INT || right_type != TYPE_INT) {
                    semantic_error("arithmetic allowed only on integers", NULL);
                }
                if (node->left && node->right &&
                    node->left->type == AST_NUMBER && node->right->type == AST_NUMBER) {
                    constant_exprs++;
                }
                node->inferred_type = TYPE_INT;
                return TYPE_INT;
            }

            if (node->op == '<' || node->op == '>' ||
                node->op == '=' || node->op == '!') {
                if (left_type != TYPE_INT || right_type != TYPE_INT) {
                    semantic_error("conditions must compare integers", NULL);
                }
                node->inferred_type = TYPE_INT;
                return TYPE_INT;
            }

            node->inferred_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
        }

        default:
            node->inferred_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
    }
}

/* ============================================================
 * FEATURE 2 & 3: COMPILE-TIME STATE TRACKING
 * Detect underflow and invalid operations at COMPILE TIME!
 * ============================================================ */

static void simulate_adt_operation(ASTNode *node) {
    if (!node || node->type != AST_ADT_OP) return;

    const char *name = node->left->name;
    int op_code = node->third->value;

    int arg1 = 0, arg2 = 0;
    int has_arg1 = 0, has_arg2 = 0;
    
    if (node->right) {
        if (node->right->type == AST_BINOP && 
            node->right->left && node->right->right) {
            has_arg1 = get_constant_value(node->right->left, &arg1);
            has_arg2 = get_constant_value(node->right->right, &arg2);
        } else {
            has_arg1 = get_constant_value(node->right, &arg1);
        }
    }

    switch (op_code) {
        case OP_PUSH: {
            int result = adt_stack_push(name);
            if (current_opts.verbose && result > 0) {
                printf("  [SIM] Stack '%s' size: %d -> %d\n", 
                       name, result - 1, result);
            }
            break;
        }
        case OP_POP: {
            int size_before = adt_stack_size(name);
            int result = adt_stack_pop(name);
            
            if (result == -1) {
                char msg[200];
                snprintf(msg, sizeof(msg),
                    "STACK UNDERFLOW at compile time! Stack '%s' is empty (size=%d)",
                    name, size_before);
                semantic_error(msg, NULL);
            } else if (current_opts.verbose && result >= 0) {
                printf("  [SIM] Stack '%s' size: %d -> %d\n", 
                       name, size_before, result);
            }
            break;
        }

        case OP_ENQUEUE: {
            int result = adt_queue_enqueue(name);
            if (current_opts.verbose && result > 0) {
                printf("  [SIM] Queue '%s' size: %d -> %d\n", 
                       name, result - 1, result);
            }
            break;
        }
        case OP_DEQUEUE: {
            int size_before = adt_queue_size(name);
            int result = adt_queue_dequeue(name);
            
            if (result == -1) {
                char msg[200];
                snprintf(msg, sizeof(msg),
                    "QUEUE UNDERFLOW at compile time! Queue '%s' is empty (size=%d)",
                    name, size_before);
                semantic_error(msg, NULL);
            } else if (current_opts.verbose && result >= 0) {
                printf("  [SIM] Queue '%s' size: %d -> %d\n", 
                       name, size_before, result);
            }
            break;
        }

        case OP_ADD_EDGE: {
            if (has_arg1 && has_arg2) {
                int result = adt_graph_add_edge(name, arg1, arg2);
                if (current_opts.verbose && result == 1) {
                    printf("  [SIM] Graph '%s': added edge (%d -> %d)\n", 
                           name, arg1, arg2);
                } else if (result == 0) {
                    semantic_warning("adding duplicate edge", name);
                }
            }
            break;
        }
        case OP_REMOVE_EDGE: {
            if (has_arg1 && has_arg2) {
                int result = adt_graph_remove_edge(name, arg1, arg2);
                
                if (result == -1) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                        "INVALID EDGE REMOVAL at compile time! Edge (%d -> %d) does not exist in graph '%s'",
                        arg1, arg2, name);
                    semantic_error(msg, NULL);
                } else if (current_opts.verbose) {
                    printf("  [SIM] Graph '%s': removed edge (%d -> %d)\n", 
                           name, arg1, arg2);
                }
            }
            break;
        }

        default:
            break;
    }
}

/* ============================================================
 * FEATURE 5: SEMANTIC OPTIMIZATION DETECTION
 * Detect patterns like push-then-pop that can be eliminated
 * ============================================================ */

typedef struct {
    char var_name[64];
    int op_code;
} LastOp;

static LastOp last_op = {"", 0};

static void check_optimization_pattern(const char *name, int op_code) {
    if (strcmp(last_op.var_name, name) == 0) {
        if (last_op.op_code == OP_PUSH && op_code == OP_POP) {
            optimization_info("push followed by immediate pop - can be eliminated");
        }
        if (last_op.op_code == OP_ENQUEUE && op_code == OP_DEQUEUE) {
            optimization_info("enqueue followed by immediate dequeue - can be eliminated");
        }
    }

    strncpy(last_op.var_name, name, sizeof(last_op.var_name) - 1);
    last_op.op_code = op_code;
}

/* ============================================================
 * MAIN AST TRAVERSAL
 * ============================================================ */

static void check_node(ASTNode *node) {
    if (!node) return;

    switch (node->type) {

        case AST_PROGRAM:
            if (current_opts.verbose) {
                printf("\n=== SIMPL Semantic Analysis ===\n");
                printf("Features: ADT Safety, State Tracking, Optimization\n");
                printf("===============================\n\n");
            }
            check_node(node->left);
            break;

        case AST_STATEMENT_LIST:
            check_node(node->left);
            check_node(node->right);
            break;

        case AST_DECL: {
            const char *name = node->left->name;
            declaration_count++;

            if (symtab_exists(name)) {
                semantic_error("variable already declared", name);
            } else {
                Type expr_type = check_expression_type(node->right);
                
                if (expr_type != TYPE_INT) {
                    semantic_error("variable declarations must be integer-valued", name);
                } else {
                    symtab_insert(name, TYPE_INT);
                    node->inferred_type = TYPE_INT;

                    if (current_opts.verbose) {
                        printf("  [DECL] %s : int\n", name);
                    }
                }
            }
            break;
        }

        case AST_ADT_DECL: {
            const char *name = node->left->name;
            int adt_code = node->right->value;
            Type adt_type = adt_code_to_type(adt_code);

            if (symtab_exists(name)) {
                semantic_error("variable already declared", name);
            } else {
                symtab_insert(name, adt_type);
                node->inferred_type = adt_type;
                
                if (current_opts.verbose) {
                    printf("  [DECL] %s : %s\n", name, type_to_string(adt_type));
                }
            }
            break;
        }

        case AST_ASSIGN: {
            const char *name = node->left->name;
            Type var_type = symtab_lookup(name);
            assignment_count++;

            if (var_type == TYPE_UNKNOWN) {
                semantic_error("assignment to undeclared variable", name);
            } else {
                Type expr_type = check_expression_type(node->right);
                if (var_type != expr_type) {
                    char msg[200];
                    snprintf(msg, sizeof(msg), "type mismatch in assignment to '%s' (%s <- %s)",
                             name, type_to_string(var_type), type_to_string(expr_type));
                    semantic_error(msg, name);
                }
                node->inferred_type = var_type;
            }
            break;
        }

        case AST_PRINT: {
            Type expr_type = check_expression_type(node->left);
            node->inferred_type = expr_type;
            break;
        }

        case AST_IF: {
            Type cond_type = check_expression_type(node->left);
            if (cond_type != TYPE_INT) {
                semantic_error("conditions must compare integers", NULL);
            }
            check_node(node->right);
            check_node(node->third);
            break;
        }

        case AST_WHILE: {
            Type cond_type = check_expression_type(node->left);
            if (cond_type != TYPE_INT) {
                semantic_error("conditions must compare integers", NULL);
            }
            current_loop_depth++;
            if (current_loop_depth > max_loop_depth) {
                max_loop_depth = current_loop_depth;
            }
            check_node(node->right);
            current_loop_depth--;
            break;
        }

        case AST_ADT_OP: {
            const char *name = node->left->name;
            int op_code = node->third->value;
            Type var_type = symtab_lookup(name);
            adt_ops++;

            if (var_type == TYPE_UNKNOWN) {
                semantic_error("ADT operation on undeclared variable", name);
                break;
            }
            
            if (!is_valid_adt_op(op_code, var_type)) {
                char msg[200];
                snprintf(msg, sizeof(msg),
                    "'%s' requires %s, but '%s' is %s",
                    op_code_to_string(op_code),
                    expected_type_for_op(op_code),
                    name,
                    type_to_string(var_type));
                semantic_error(msg, NULL);
                break;
            }

            check_optimization_pattern(name, op_code);
            simulate_adt_operation(node);

            if (node->right) {
                if (node->right->type == AST_BINOP) {
                    Type t = check_expression_type(node->right);
                    if (t != TYPE_INT) {
                        semantic_error("ADT operation arguments must be integers", NULL);
                    }
                } else {
                    Type arg_type = check_expression_type(node->right);
                    if (arg_type != TYPE_INT) {
                        semantic_error("ADT operation arguments must be integers", NULL);
                    }
                }
            }
            break;
        }

        default:
            check_node(node->left);
            check_node(node->right);
            check_node(node->third);
            break;
    }
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

SemanticReport semantic_check_with_options(ASTNode *root, SemanticOptions opts) {
    error_count = 0;
    warning_count = 0;
    optimization_count = 0;
    declaration_count = 0;
    assignment_count = 0;
    arithmetic_ops = 0;
    adt_ops = 0;
    constant_exprs = 0;
    current_loop_depth = 0;
    max_loop_depth = 0;

    current_opts = opts;
    last_op.var_name[0] = '\0';
    last_op.op_code = 0;
    
    symtab_init();
    check_node(root);

    SemanticReport report;
    report.error_count = error_count;
    report.warning_count = warning_count;
    report.optimizations_applied = optimization_count;
    report.declaration_count = declaration_count;
    report.assignment_count = assignment_count;
    report.arithmetic_ops = arithmetic_ops;
    report.adt_ops = adt_ops;
    report.constant_exprs = constant_exprs;
    report.max_loop_depth = max_loop_depth;

    if (opts.verbose) {
        printf("\n=== SIMPL Semantic Summary ===\n");
        printf("Declarations:          %d\n", declaration_count);
        printf("Assignments:           %d\n", assignment_count);
        printf("Arithmetic Ops:        %d\n", arithmetic_ops);
        printf("ADT Ops:               %d\n", adt_ops);
        printf("Constant Expressions:  %d\n", constant_exprs);
        printf("Max Loop Depth:        %d\n", max_loop_depth);
        printf("\nErrors:   %d\n", error_count);
        printf("Warnings: %d\n", warning_count);
        if (opts.show_optimizations) {
            printf("Optimizations detected: %d\n", optimization_count);
        }
        printf("================================\n");
    }

    return report;
}

SemanticReport semantic_check(ASTNode *root) {
    SemanticOptions opts = {1, 1, 0};
    return semantic_check_with_options(root, opts);
}

int semantic_analyze(ASTNode *root) {
    SemanticOptions opts = {0, 0, 0};
    SemanticReport report = semantic_check_with_options(root, opts);
    return report.error_count;
}
