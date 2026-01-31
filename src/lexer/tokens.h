#ifndef TOKENS_H
#define TOKENS_H

/* -------------------------------------------------
 * SIMPL Language Token Definitions
 * -------------------------------------------------
 * Tokens are grouped by category for clarity.
 * Values start above ASCII range to avoid clashes.
 */

/* ---------- Keywords ---------- */
#define LET        256
#define SET        257
#define BE         258
#define TO         259

#define IF         260
#define THEN       261
#define ELSE       262
#define END        263

#define WHILE      264
#define DO         265

#define PRINT      266

/* ---------- Built-in Data Types ---------- */
#define STACK      270
#define QUEUE      271
#define TREE       272
#define GRAPH      273

/* ---------- Stack Operations ---------- */
#define PUSH       280
#define POP        281

/* ---------- Queue Operations ---------- */
#define ENQUEUE    282
#define DEQUEUE    283

/* ---------- Tree Operations ---------- */
#define INSERT     284
#define REMOVE     285

/* ---------- Graph Operations ---------- */
#define ADD_EDGE   286
#define REMOVE_EDGE 287

/* ---------- Relational Operators ---------- */
#define EQ         300   /* == */
#define NEQ        301   /* != */
#define LT         302   /* <  */
#define GT         303   /* >  */

/* ---------- Arithmetic Operators ---------- */
#define PLUS       310   /* + */
#define MINUS      311   /* - */
#define MUL        312   /* * */
#define DIV        313   /* / */

/* ---------- Identifiers & Literals ---------- */
#define IDENTIFIER 400
#define NUMBER     401

#endif /* TOKENS_H */
