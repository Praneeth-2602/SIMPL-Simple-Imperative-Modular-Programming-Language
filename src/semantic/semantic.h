#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "../parser/ast.h"

/* ============================================================
 * SEMANTIC ANALYSIS - Features GCC Cannot Provide
 * ============================================================
 * 
 * 1. ADT Safety Guarantees
 *    - Stacks/queues/trees/graphs cannot be printed
 *    - ADTs cannot be used in arithmetic expressions
 *    - ADTs cannot be assigned integer values
 * 
 * 2. Compile-Time State Tracking
 *    - Detect stack/queue underflow at compile time
 *    - Track graph edge existence
 *    - Detect operations on non-existent edges
 * 
 * 3. Transparent Optimization Decisions
 *    - Report what optimizations are applied and why
 * 
 * 4. Semantic Optimization
 *    - Remove dead ADT operations (push then pop)
 *    - Detect and warn about redundant operations
 * ============================================================ */

/* Semantic analysis options */
typedef struct {
    int verbose;                    /* Print detailed analysis info */
    int show_optimizations;         /* Show optimization decisions */
    int strict_mode;                /* Extra strict checking */
} SemanticOptions;

/* Semantic analysis result */
typedef struct {
    int error_count;
    int warning_count;
    int optimizations_applied;
} SemanticResult;

/* Perform semantic analysis on the AST
 * Returns: SemanticResult with error/warning counts
 */
SemanticResult semantic_check(ASTNode *root);

/* Perform semantic analysis with options */
SemanticResult semantic_check_with_options(ASTNode *root, SemanticOptions opts);

/* Legacy API (returns error count) */
int semantic_analyze(ASTNode *root);

#endif
