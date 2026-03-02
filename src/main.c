#include <stdio.h>
#include <stdlib.h>

#include "parser/ast.h"
#include "semantic/semantic.h"
#include "ir/ir.h"

/* Provided by Bison */
int yyparse(void);

/* AST root set inside parser */
extern ASTNode *ast_root;

int main(void)
{

    /* ---------------------------
        Phase 1 & 2: Parsing
    ---------------------------- */
    if (yyparse() != 0)
    {
        fprintf(stderr, "Parsing failed.\n");
        return 1;
    }

    printf("Parsing successful.\n");

    /* ---------------------------
        Phase 3: Semantic Analysis
    ---------------------------- */
    SemanticReport sem_report = semantic_check(ast_root);

    if (sem_report.error_count > 0)
    {
        fprintf(stderr, "Semantic analysis failed with %d error(s).\n", sem_report.error_count);
        return 1;
    }

    printf("Semantic analysis passed.\n");

    /* ---------------------------
        Phase 4: IR Generation
    ---------------------------- */
    IRInstruction *ir = generate_ir(ast_root);

    printf("\nGenerated IR:\n");
    print_ir(ir);

    return 0;
}