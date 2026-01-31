all:
	lex src/lexer/easy.l
	yacc -d src/parser/easy.y
	gcc lex.yy.c y.tab.c -o easy

clean:
	rm -f lex.yy.c y.tab.c y.tab.h easy
