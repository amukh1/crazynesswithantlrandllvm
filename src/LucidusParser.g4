parser grammar LucidusParser;
options { tokenVocab=LucidusLexer;}

program
    : (stat | def | struct)*  EOF
    ;
rawtype: (ID);
fptr: '(' (type (',' type)*)? ')' '->' '(' type ')';
type: (ID | fptr) (STAR)*;
idec: ID COL type ;
param: (idec|DOTS);
dec: DECL ID '(' (param (',' param)*)? ')' ARROW type ';';
ret: RETURN expr ';';
vdec: idec '=' expr ';';
infdec: ID DCOL expr ';';
edec: idec ';';
// vdef: ID '=' expr ';';
assign: expr '=' expr ';';
label: LABEL ID ':';
goto: GOTO ID ';';
if: 'if' expr ARROW '{' stat* '}';
imrt: 'import' STRING;

stat
    : vdec
    // | vdef
    | infdec
    | edec
    | assign
    | expr ';'
    | dec
    | ret
    | label
    | goto
    | if
    | imrt
    ;
def : DEF ID '(' (param (',' param)*)? ')' ARROW type '{' stat* '}' ;

expr: ID
    | INT
    | FLOAT
    | STRING
    | expr DOT ID
    // | func=expr '(' (expr (',' expr)*)? ')'
    | func
    | PTR expr
    | expr STAR expr
    | STAR expr
    | NOT expr
    | expr AND expr
    | expr OR expr
    | expr DIV expr
    | expr PLUS expr
    | expr SUB expr
    | expr ARROW '(' type ')'
    | expr '=' '=' expr
    | expr GTR expr
    | expr LESS expr
    | '(' expr ')'
    
    ;
func: ID '(' (expr (',' expr)*)? ')';
// func :  (ID |  expr ) '(' (expr (',' expr)*)? ')' ;
    
struct: 'struct' ID '{' (idec (',' idec)*)? '}' ';';