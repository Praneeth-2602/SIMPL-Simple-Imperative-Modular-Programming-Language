%{
#include "../parser/simpl.tab.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
%}

DIGIT      [0-9]
LETTER     [a-zA-Z_]
ID         {LETTER}({LETTER}|{DIGIT})*
NUMBER     {DIGIT}+

%%
"let"       { return LET; }
"set"       { return SET; }
"be"        { return BE; }
"to"        { return TO; }

"if"        { return IF; }
"then"      { return THEN; }
"else"      { return ELSE; }
"end"       { return END; }

"while"     { return WHILE; }
"do"        { return DO; }

"print"     { return PRINT; }

"stack"     { return STACK; }
"queue"     { return QUEUE; }
"tree"      { return TREE; }
"graph"     { return GRAPH; }

"push"      { return PUSH; }
"pop"       { return POP; }

"enqueue"   { return ENQUEUE; }
"dequeue"   { return DEQUEUE; }

"insert"    { return INSERT; }
"remove"    { return REMOVE; }

"add_edge"      { return ADD_EDGE; }
"remove_edge"   { return REMOVE_EDGE; }

"=="        { return EQ; }
"!="        { return NEQ; }
"<"         { return LT; }
">"         { return GT; }

"+"         { return PLUS; }
"-"         { return MINUS; }
"*"         { return MUL; }
"/"         { return DIV; }

{NUMBER} {
    yylval.num = atoi(yytext);
    return NUMBER;
}

{ID} {
    yylval.str = strdup(yytext);
    return IDENTIFIER;
}

[ \t\r\n]+  { /* ignore */ }

"#".*       { /* ignore comments */ }

. {
    printf("Lexical error: unknown character '%s'\n", yytext);
}
%%

int yywrap(void) {
    return 1;
}