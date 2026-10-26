#include<stdio.h>
#include<ctype.h>
#include<string.h>
#include <stdlib.h>
#include "syntax_tree.h"
#include "error_list.h"

#ifndef PARSER_OUTPUT_ENABLED
#define PARSER_OUTPUT_ENABLED 1
#endif

#ifndef PARSER_DEBUG_ENABLED
#define PARSER_DEBUG_ENABLED 0
#endif

typedef struct
{
    const TokenList *tokens;
    size_t pos;

    ErrorList *errors;

    SyntaxNode **nodes;
    size_t nodeCount;
    size_t nodeCapacity;
    int memoryError;
} Parser;

typedef struct
{
    size_t pos;
    size_t nodeCount;
    size_t errorCount;
} ParserMark;

int parseExp(Parser *parser);
int parseBlock(Parser *parser);
int parseCaseStmt(Parser *parser);

const Token *currentToken(const Parser *parser)
{
    if (parser->pos >= parser->tokens->size)
    {
        return NULL;
    }

    return &parser->tokens->data[parser->pos];
}

// 语法树节点栈、检查点与回退
ParserMark markParser(const Parser *parser)
{
    ParserMark mark = {
        parser->pos,
        parser->nodeCount,
        parser->errors->size};

    return mark;
}

int pushParserNode(Parser *parser, SyntaxNode *node)
{
    if (parser->memoryError || node == NULL)
    {
        return 0;
    }

    if (parser->nodeCount == parser->nodeCapacity)
    {
        size_t capacity = parser->nodeCapacity == 0
                              ? 16
                              : parser->nodeCapacity * 2;
        SyntaxNode **nodes = realloc(
            parser->nodes, capacity * sizeof(SyntaxNode *));

        if (nodes == NULL)
        {
            parser->memoryError = 1;
            return 0;
        }

        parser->nodes = nodes;
        parser->nodeCapacity = capacity;
    }

    parser->nodes[parser->nodeCount++] = node;
    return 1;
}

void rollbackParser(Parser *parser, ParserMark mark)
{
    while (parser->nodeCount > mark.nodeCount)
    {
        parser->nodeCount--;
        freeSyntaxTree(parser->nodes[parser->nodeCount]);
        parser->nodes[parser->nodeCount] = NULL;
    }

    parser->pos = mark.pos;
    parser->errors->size = mark.errorCount;
}

int finishNode(Parser *parser, ParserMark mark, const char *name)
{
    if (parser->memoryError)
    {
        rollbackParser(parser, mark);
        return 0;
    }

    SyntaxNode *node = createNonterminalNode(name);
    if (node == NULL)
    {
        parser->memoryError = 1;
        rollbackParser(parser, mark);
        return 0;
    }

    size_t count = parser->nodeCount - mark.nodeCount;

    if (count > 0)
    {
        node->children = malloc(count * sizeof(SyntaxNode *));
        if (node->children == NULL)
        {
            freeSyntaxTree(node);
            parser->memoryError = 1;
            rollbackParser(parser, mark);
            return 0;
        }

        memcpy(node->children,
               parser->nodes + mark.nodeCount,
               count * sizeof(SyntaxNode *));

        node->childCount = count;
        node->childCapacity = count;
    }

    // 将这些子树的所有权从节点栈转移给新父节点
    parser->nodeCount = mark.nodeCount;

    if (!pushParserNode(parser, node))
    {
        freeSyntaxTree(node);
        rollbackParser(parser, mark);
        return 0;
    }

    return 1;
}

void freeParserTrees(Parser *parser)
{
    while (parser->nodeCount > 0)
    {
        parser->nodeCount--;
        freeSyntaxTree(parser->nodes[parser->nodeCount]);
    }

    free(parser->nodes);
    parser->nodes = NULL;
    parser->nodeCapacity = 0;
    parser->pos = 0;
}

int advanceToken(Parser *parser)
{
    const Token *token = currentToken(parser);

    if (parser->memoryError || token == NULL)
    {
        return 0;
    }

    SyntaxNode *node = createTokenNode(token);
    if (node == NULL)
    {
        parser->memoryError = 1;
        return 0;
    }

    if (!pushParserNode(parser, node))
    {
        freeSyntaxTree(node);
        return 0;
    }

    parser->pos++;
    return 1;
}

int checkToken(const Parser *parser, const char *type)
{
    const Token *token = currentToken(parser);

    if (parser->memoryError || token == NULL)
    {
        return 0;
    }

    return strcmp(token->type, type) == 0;
}

// offset=0 查看当前 token，offset=1 查看下一个。
// 只查看，不移动位置、不创建节点。
int checkTokenAt(const Parser *parser,
                 size_t offset,
                 const char *type)
{
    if (parser->memoryError ||
        parser->pos >= parser->tokens->size)
    {
        return 0;
    }

    size_t remaining = parser->tokens->size - parser->pos;

    if (offset >= remaining)
    {
        return 0;
    }

    const Token *token =
        &parser->tokens->data[parser->pos + offset];

    return strcmp(token->type, type) == 0;
}

int checkNextToken(const Parser *parser, const char *type)
{
    return checkTokenAt(parser, 1, type);
}

int matchToken(Parser *parser, const char *type)
{
    if (!checkToken(parser, type))
    {
        return 0;
    }

    return advanceToken(parser);
}

// 仅用于允许按“缺少分隔符”恢复的位置
int expectToken(Parser *parser, const char *type, char errorCode)
{
    if (parser->memoryError)
    {
        return 0;
    }

    // 存在该 token：正常消费并创建终结符节点
    if (checkToken(parser, type))
    {
        return advanceToken(parser);
    }

    // 缺少分隔符：错误行号取前一个实际读取的 token
    int line = 1;

    if (parser->pos > 0)
    {
        line = parser->tokens->data[parser->pos - 1].line;
    }

    if (!addError(parser->errors, line, errorCode))
    {
        parser->memoryError = 1;
        return 0;
    }

    // 恢复成功：保留当前 token，交给后续语法成分解析
    return 1;
}

// Exp 的可能起始 token，用于判断 return 后是否存在可选表达式
int isExpStart(const Parser *parser)
{
    return checkToken(parser, "IDENFR") ||
           checkToken(parser, "INTCON") ||
           checkToken(parser, "CHARCON") ||
           checkToken(parser, "LPARENT") ||
           checkToken(parser, "PLUS") ||
           checkToken(parser, "MINU") ||
           checkToken(parser, "NOT") ||
           checkToken(parser, "INC");
}

// BType → 'int' | 'char'
int parseBType(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (matchToken(parser, "INTTK") ||
        matchToken(parser, "CHARTK"))
    {
        return finishNode(parser, start, "BType");
    }

    rollbackParser(parser, start);
    return 0;
}

// Number → IntConst | CharConst
int parseNumber(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (matchToken(parser, "INTCON") ||
        matchToken(parser, "CHARCON"))
    {
        return finishNode(parser, start, "Number");
    }

    rollbackParser(parser, start);
    return 0;
}

// UnaryOp → '+' | '-' | '!'
int parseUnaryOp(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (matchToken(parser, "PLUS") ||
        matchToken(parser, "MINU") ||
        matchToken(parser, "NOT"))
    {
        return finishNode(parser, start, "UnaryOp");
    }

    rollbackParser(parser, start);
    return 0;
}

// LVal → Ident ['[' Exp ']']
int parseLVal(Parser *parser){
    ParserMark start = markParser(parser);

    if(!matchToken(parser,"IDENFR")){
        return 0;
    }

    if(matchToken(parser,"LBRACK")){
        if(!parseExp(parser)){
            rollbackParser(parser, start);
            return 0;
        }

        if(!expectToken(parser, "RBRACK", 'k')){
            rollbackParser(parser, start);
            return 0;
        }
    }

    return finishNode(parser, start, "LVal");
}

// PrimaryExp → '(' Exp ')' | LVal | Number
int parsePrimaryExp(Parser *parser){
    ParserMark start = markParser(parser);

    if(matchToken(parser,"LPARENT")){
        if(!parseExp(parser)){
            rollbackParser(parser, start);
            return 0;
        }

        if(!expectToken(parser, "RPARENT", 'j')){
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "PrimaryExp");
    }

    if (checkToken(parser, "IDENFR"))
    {
        if (!parseLVal(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "PrimaryExp");
    }

    if (!parseNumber(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "PrimaryExp");
}

// FuncRParams → Exp { ',' Exp }
int parseFuncRParams(Parser * parser){
    ParserMark start = markParser(parser);

    if(!parseExp(parser)){
        rollbackParser(parser, start);
        return 0;
    }

    while(matchToken(parser,"COMMA")){
        if(!parseExp(parser)){
            rollbackParser(parser, start);
            return 0;
        }
    }

    return finishNode(parser, start, "FuncRParams");
}

//UnaryExp → PrimaryExp
//           | Ident '(' [FuncRParams] ')'
//           | UnaryOp UnaryExp
//           | '(' BType ')' UnaryExp
int parseUnaryExp(Parser *parser){
    ParserMark start = markParser(parser);

    // UnaryExp -> '++' LVal
    if (matchToken(parser, "INC"))
    {
        if (!parseLVal(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        return finishNode(parser, start, "UnaryExp");
    }

    // UnaryOp UnaryExp
    if(parseUnaryOp(parser)){
        if(parseUnaryExp(parser)){
            return finishNode(parser, start, "UnaryExp");
        }

        //失败后恢复位置
        rollbackParser(parser, start);
        return 0;
    }

    // Ident '(' [FuncRParams] ')'
    if(checkToken(parser,"IDENFR")&&checkNextToken(parser,"LPARENT")){
        //消费函数名
        if (!advanceToken(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        //消费左括号
        if (!advanceToken(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        // 只有表达式可以开始时才解析实参，允许无参调用缺少右括号后继续恢复。
        if(isExpStart(parser)&&!parseFuncRParams(parser)){
            rollbackParser(parser, start);
            return 0;
        }

        if(!expectToken(parser, "RPARENT", 'j')){
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "UnaryExp");
    }

    //'(' BType ')' UnaryExp
    if (checkToken(parser, "LPARENT") &&(checkNextToken(parser, "INTTK") ||checkNextToken(parser, "CHARTK"))){
        // 消费左括号
        if (!advanceToken(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        if (!parseBType(parser) ||
            !expectToken(parser, "RPARENT", 'j') ||
            !parseUnaryExp(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "UnaryExp");
    }

    //PrimaryExp
    if (!parsePrimaryExp(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "UnaryExp");
}

//MulExp → UnaryExp
//       | MulExp ('*' | '/' | '%') UnaryExp
int parseMulExp(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseUnaryExp(parser) ||
        !finishNode(parser, start, "MulExp"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    // 每次包装保留左子树，恢复原文法的左结合结构。
    while (checkToken(parser, "MULT") ||
           checkToken(parser, "DIV") ||
           checkToken(parser, "MOD"))
    {
        if (!advanceToken(parser) ||
            !parseUnaryExp(parser) ||
            !finishNode(parser, start, "MulExp"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return 1;
}

// AddExp → MulExp | AddExp ('+' | '-') MulExp
int parseAddExp(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseMulExp(parser) ||
        !finishNode(parser, start, "AddExp"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    // 每次包装保留左子树，恢复原文法的左结合结构。
    while (checkToken(parser, "PLUS") ||
           checkToken(parser, "MINU"))
    {
        if (!advanceToken(parser) ||
            !parseMulExp(parser) ||
            !finishNode(parser, start, "AddExp"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return 1;
}

// Exp    → AddExp
int parseExp(Parser *parser){
    ParserMark start = markParser(parser);

    if (!parseAddExp(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "Exp");
}

//RelExp → AddExp
//       | RelExp ('<' | '>' | '<=' | '>=') AddExp
int parseRelExp(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseAddExp(parser) ||
        !finishNode(parser, start, "RelExp"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    // 每次包装保留左子树，恢复原文法的左结合结构。
    while (checkToken(parser, "LSS") ||
           checkToken(parser, "GRE") ||
           checkToken(parser, "LEQ") ||
           checkToken(parser, "GEQ"))
    {
        if (!advanceToken(parser) ||
            !parseAddExp(parser) ||
            !finishNode(parser, start, "RelExp"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return 1;
}

//EqExp → RelExp
//      | EqExp ('==' | '!=') RelExp
int parseEqExp(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseRelExp(parser) ||
        !finishNode(parser, start, "EqExp"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    // 每次包装保留左子树，恢复原文法的左结合结构。
    while (checkToken(parser, "EQL") ||
           checkToken(parser, "NEQ"))
    {
        if (!advanceToken(parser) ||
            !parseRelExp(parser) ||
            !finishNode(parser, start, "EqExp"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return 1;
}

// LAndExp → EqExp | LAndExp '&&' EqExp
int parseLAndExp(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseEqExp(parser) ||
        !finishNode(parser, start, "LAndExp"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    // 每次包装保留左子树，恢复原文法的左结合结构。
    while (checkToken(parser, "AND"))
    {
        if (!advanceToken(parser) ||
            !parseEqExp(parser) ||
            !finishNode(parser, start, "LAndExp"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return 1;
}

// LOrExp → LAndExp | LOrExp '||' LAndExp
int parseLOrExp(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseLAndExp(parser) ||
        !finishNode(parser, start, "LOrExp"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    // 每次包装保留左子树，恢复原文法的左结合结构。
    while (checkToken(parser, "OR"))
    {
        if (!advanceToken(parser) ||
            !parseLAndExp(parser) ||
            !finishNode(parser, start, "LOrExp"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return 1;
}

// Cond   → LOrExp
int parseCond(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseLOrExp(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "Cond");
}

// ConstExp → AddExp
int parseConstExp(Parser *parser){
    ParserMark start = markParser(parser);

    if (!parseAddExp(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "ConstExp");
}

//ConstInitVal → ConstExp
//             | '{' [ ConstExp { ',' ConstExp } ] '}'
//             | StringConst
int parseConstInitVal(Parser *parser)
{
    ParserMark start = markParser(parser);

    // 字符串初值
    if (matchToken(parser, "STRCON"))
    {
        return finishNode(parser, start, "ConstInitVal");
    }

    // 花括号初值
    if (matchToken(parser, "LBRACE"))
    {
        // 允许空花括号 {}
        if (!checkToken(parser, "RBRACE"))
        {
            if (!parseConstExp(parser))
            {
                rollbackParser(parser, start);
                return 0;
            }

            while (matchToken(parser, "COMMA"))
            {
                if (!parseConstExp(parser))
                {
                    rollbackParser(parser, start);
                    return 0;
                }
            }
        }

        if (!matchToken(parser, "RBRACE"))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "ConstInitVal");
    }

    // 单个常量表达式
    if (!parseConstExp(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "ConstInitVal");
}

// ConstDef → Ident ['[' ConstExp ']'] '=' ConstInitVal
int parseConstDef(Parser *parser)
{
    ParserMark start = markParser(parser);

    // 必须以标识符开头
    if (!matchToken(parser, "IDENFR"))
    {
        return 0;
    }

    // 可选的一维数组长度
    if (matchToken(parser, "LBRACK"))
    {
        if (!parseConstExp(parser) ||
            !expectToken(parser, "RBRACK", 'k'))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    // 常量必须有等号和初值
    if (!matchToken(parser, "ASSIGN") ||
        !parseConstInitVal(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "ConstDef");
}

// ConstDecl → 'const' BType ConstDef { ',' ConstDef } ';'
int parseConstDecl(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!matchToken(parser, "CONSTTK"))
    {
        return 0;
    }

    if (!parseBType(parser) ||
        !parseConstDef(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    while (matchToken(parser, "COMMA"))
    {
        if (!parseConstDef(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    if (!expectToken(parser, "SEMICN", 'i'))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "ConstDecl");
}

// InitVal → Exp | '{' [ Exp { ',' Exp } ] '}' | StringConst
int parseInitVal(Parser *parser)
{
    ParserMark start = markParser(parser);

    // 字符串初值
    if (matchToken(parser, "STRCON"))
    {
        return finishNode(parser, start, "InitVal");
    }

    // 花括号初值
    if (matchToken(parser, "LBRACE"))
    {
        // 允许空花括号 {}
        if (!checkToken(parser, "RBRACE"))
        {
            if (!parseExp(parser))
            {
                rollbackParser(parser, start);
                return 0;
            }

            while (matchToken(parser, "COMMA"))
            {
                if (!parseExp(parser))
                {
                    rollbackParser(parser, start);
                    return 0;
                }
            }
        }

        if (!matchToken(parser, "RBRACE"))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "InitVal");
    }

    // 单个普通表达式
    if (!parseExp(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "InitVal");
}

// VarDef → Ident ['[' ConstExp ']'] ['=' InitVal]
int parseVarDef(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!matchToken(parser, "IDENFR"))
    {
        return 0;
    }

    // 数组长度仍然必须使用常量表达式
    if (matchToken(parser, "LBRACK"))
    {
        if (!parseConstExp(parser) ||
            !expectToken(parser, "RBRACK", 'k'))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    // 初值可以省略，但出现等号后必须有初值
    if (matchToken(parser, "ASSIGN"))
    {
        if (!parseInitVal(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return finishNode(parser, start, "VarDef");
}

// VarDecl → ['static'] BType VarDef { ',' VarDef } ';'
int parseVarDecl(Parser *parser)
{
    ParserMark start = markParser(parser);

    // static 可选：匹配成功就消费，否则保持位置
    matchToken(parser, "STATICTK");

    if (!parseBType(parser) ||
        !parseVarDef(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    while (matchToken(parser, "COMMA"))
    {
        if (!parseVarDef(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    if (!expectToken(parser, "SEMICN", 'i'))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "VarDecl");
}

// Decl → ConstDecl | VarDecl
int parseDecl(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (checkToken(parser, "CONSTTK"))
    {
        if (!parseConstDecl(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "Decl");
    }

    if (!parseVarDecl(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "Decl");
}

// FuncType    → 'void' | 'int' | 'char'
int parseFuncType(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (matchToken(parser, "VOIDTK") ||
        matchToken(parser, "INTTK") ||
        matchToken(parser, "CHARTK"))
    {
        return finishNode(parser, start, "FuncType");
    }

    rollbackParser(parser, start);
    return 0;
}

// FuncFParam  → BType Ident ['[' ']']
int parseFuncFParam(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseBType(parser) ||
        !matchToken(parser, "IDENFR"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    if (matchToken(parser, "LBRACK"))
    {
        if (!expectToken(parser, "RBRACK", 'k'))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return finishNode(parser, start, "FuncFParam");
}

// FuncFParams → FuncFParam { ',' FuncFParam }
int parseFuncFParams(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseFuncFParam(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    while (matchToken(parser, "COMMA"))
    {
        if (!parseFuncFParam(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return finishNode(parser, start, "FuncFParams");
}

//Stmt → 'if' '(' Cond ')' Stmt ['else' Stmt]
//     | 'while' '(' Cond ')' Stmt
//     | 'break' ';'
//     | 'continue' ';'
int parseStmt(Parser *parser)
{
    ParserMark start = markParser(parser);

    // 嵌套语句块
    if (checkToken(parser, "LBRACE"))
    {
        if (!parseBlock(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "Stmt");
    }

    // if '(' Cond ')' Stmt ['else' Stmt]
    if (matchToken(parser, "IFTK"))
    {
        if (!matchToken(parser, "LPARENT") ||
            !parseCond(parser) ||
            !expectToken(parser, "RPARENT", 'j') ||
            !parseStmt(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        if (matchToken(parser, "ELSETK"))
        {
            if (!parseStmt(parser))
            {
                rollbackParser(parser, start);
                return 0;
            }
        }

        return finishNode(parser, start, "Stmt");
    }

    // while '(' Cond ')' Stmt
    if (matchToken(parser, "WHILETK"))
    {
        if (!matchToken(parser, "LPARENT") ||
            !parseCond(parser) ||
            !expectToken(parser, "RPARENT", 'j') ||
            !parseStmt(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "Stmt");
    }

    // break ';' 或 continue ';'
    if (matchToken(parser, "BREAKTK") ||
        matchToken(parser, "CONTINUETK"))
    {
        if (!expectToken(parser, "SEMICN", 'i'))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "Stmt");
    }

    // switch '(' Exp ')' '{' {CaseStmt} '}'
    if (matchToken(parser, "SWITCHTK"))
    {
        if (!matchToken(parser, "LPARENT") ||
            !parseExp(parser) ||
            !expectToken(parser, "RPARENT", 'j') ||
            !matchToken(parser, "LBRACE"))
        {
            rollbackParser(parser, start);
            return 0;
        }

        while (!checkToken(parser, "RBRACE"))
        {
            if (currentToken(parser) == NULL ||
                !parseCaseStmt(parser))
            {
                rollbackParser(parser, start);
                return 0;
            }
        }

        // 消费右花括号
        if (!advanceToken(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        return finishNode(parser, start, "Stmt");
    }

    // printf '(' StringConst { ',' Exp } ')' ';'
    if (matchToken(parser, "PRINTFTK"))
    {
        if (!matchToken(parser, "LPARENT") ||
            !matchToken(parser, "STRCON"))
        {
            rollbackParser(parser, start);
            return 0;
        }

        while (matchToken(parser, "COMMA"))
        {
            if (!parseExp(parser))
            {
                rollbackParser(parser, start);
                return 0;
            }
        }

        if (!expectToken(parser, "RPARENT", 'j') ||
            !expectToken(parser, "SEMICN", 'i'))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "Stmt");
    }

    // return [Exp] ';'
    if (matchToken(parser, "RETURNTK"))
    {
        if (isExpStart(parser) &&
            !parseExp(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        if (!expectToken(parser, "SEMICN", 'i'))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "Stmt");
    }

    // 空语句
    if (matchToken(parser, "SEMICN"))
    {
        return finishNode(parser, start, "Stmt");
    }

    // 尝试赋值语句
    if (parseLVal(parser))
    {
        if (matchToken(parser, "ASSIGN"))
        {
            if (!parseExp(parser) ||
                !expectToken(parser, "SEMICN", 'i'))
            {
                rollbackParser(parser, start);
                return 0;
            }

            return finishNode(parser, start, "Stmt");
        }
    }

    // 没有识别为赋值，重新按表达式语句读取
    rollbackParser(parser, start);

    if (!parseExp(parser) ||
        !expectToken(parser, "SEMICN", 'i'))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "Stmt");
}

//CaseStmt → 'case' Number ':' { Stmt }
//         | 'default' ':' { Stmt }
int parseCaseStmt(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (matchToken(parser, "CASETK"))
    {
        if (!parseNumber(parser) ||
            !matchToken(parser, "COLON"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }
    else if (matchToken(parser, "DEFAULTTK"))
    {
        if (!matchToken(parser, "COLON"))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }
    else
    {
        return 0;
    }

    while (!checkToken(parser, "CASETK") &&
           !checkToken(parser, "DEFAULTTK") &&
           !checkToken(parser, "RBRACE"))
    {
        if (currentToken(parser) == NULL ||
            !parseStmt(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    return finishNode(parser, start, "CaseStmt");
}

// BlockItem → Decl | Stmt
int parseBlockItem(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (checkToken(parser, "CONSTTK") ||
        checkToken(parser, "INTTK") ||
        checkToken(parser, "CHARTK") ||
        checkToken(parser, "STATICTK"))
    {
        if (!parseDecl(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }

        return finishNode(parser, start, "BlockItem");
    }

    if (!parseStmt(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "BlockItem");
}

// Block     → '{' { BlockItem } '}'
int parseBlock(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!matchToken(parser, "LBRACE"))
    {
        return 0;
    }

    while (!checkToken(parser, "RBRACE"))
    {
        if (currentToken(parser) == NULL ||
            !parseBlockItem(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    // 消费右花括号
    if (!advanceToken(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }
    return finishNode(parser, start, "Block");
}

// FuncDef → FuncType Ident '(' [FuncFParams] ')' Block
int parseFuncDef(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!parseFuncType(parser) ||
        !matchToken(parser, "IDENFR") ||
        !matchToken(parser, "LPARENT"))
    {
        rollbackParser(parser, start);
        return 0;
    }

    // 形参以基本类型开头；遇到 '{' 时先恢复缺少的右括号。
    if ((checkToken(parser, "INTTK") || checkToken(parser, "CHARTK")) &&
        !parseFuncFParams(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    if (!expectToken(parser, "RPARENT", 'j') ||
        !parseBlock(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "FuncDef");
}

// MainFuncDef → 'int' 'main' '(' ')' Block
int parseMainFuncDef(Parser *parser)
{
    ParserMark start = markParser(parser);

    if (!matchToken(parser, "INTTK") ||
        !matchToken(parser, "MAINTK") ||
        !matchToken(parser, "LPARENT") ||
        !expectToken(parser, "RPARENT", 'j') ||
        !parseBlock(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "MainFuncDef");
}

// 判断是否是全局声明的开头
int isGlobalDeclStart(const Parser *parser)
{
    if (checkToken(parser, "CONSTTK") ||
        checkToken(parser, "STATICTK"))
    {
        return 1;
    }

    if (checkToken(parser, "INTTK") ||
        checkToken(parser, "CHARTK"))
    {
        // int/char 标识符 后面不是 '('，按变量声明处理
        return checkTokenAt(parser, 1, "IDENFR") &&
               !checkTokenAt(parser, 2, "LPARENT");
    }

    return 0;
}

// 判断是否是普通函数定义的开头
int isFuncDefStart(const Parser *parser)
{
    int isType =
        checkToken(parser, "VOIDTK") ||
        checkToken(parser, "INTTK") ||
        checkToken(parser, "CHARTK");

    return isType &&
           checkTokenAt(parser, 1, "IDENFR") &&
           checkTokenAt(parser, 2, "LPARENT");
}

// CompUnit → {Decl} {FuncDef} MainFuncDef
int parseCompUnit(Parser *parser)
{
    ParserMark start = markParser(parser);

    // {Decl}
    while (isGlobalDeclStart(parser))
    {
        if (!parseDecl(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    // {FuncDef}
    while (isFuncDefStart(parser))
    {
        if (!parseFuncDef(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
    }

    // 必须有主函数，且后面不能再有 token
    if (!parseMainFuncDef(parser) ||
        currentToken(parser) != NULL)
    {
        rollbackParser(parser, start);
        return 0;
    }

    return finishNode(parser, start, "CompUnit");
}

const char *getWordType(const char *word){
    const struct {
        const char *text;
        const char *type;
    } keywords[] = {
        {"const", "CONSTTK"},
        {"int", "INTTK"},
        {"char", "CHARTK"},
        {"static", "STATICTK"},
        {"main", "MAINTK"},
        {"void", "VOIDTK"},
        {"if", "IFTK"},
        {"else", "ELSETK"},
        {"while", "WHILETK"},
        {"switch", "SWITCHTK"},
        {"case", "CASETK"},
        {"default", "DEFAULTTK"},
        {"break", "BREAKTK"},
        {"continue", "CONTINUETK"},
        {"return", "RETURNTK"},
        {"printf", "PRINTFTK"}
    };

    size_t count = sizeof(keywords) / sizeof(keywords[0]);

    for (size_t i = 0; i < count;i++){
        if(strcmp(word,keywords[i].text) == 0){
            return keywords[i].type;
        }
    }
    return "IDENFR";
}

const char *getSingleCharType(int ch)
{
    switch (ch)
    {
    case '+':
        return "PLUS";
    case '-':
        return "MINU";
    case '*':
        return "MULT";
    case '/':
        return "DIV";
    case '%':
        return "MOD";
    case '!':
        return "NOT";
    case '=':
        return "ASSIGN";
    case '<':
        return "LSS";
    case '>':
        return "GRE";
    case ';':
        return "SEMICN";
    case ',':
        return "COMMA";
    case ':':
        return "COLON";
    case '(':
        return "LPARENT";
    case ')':
        return "RPARENT";
    case '[':
        return "LBRACK";
    case ']':
        return "RBRACK";
    case '{':
        return "LBRACE";
    case '}':
        return "RBRACE";
    default:
        return NULL;
    }
}

//处理双字符，如：==
const char *getDoubleCharType(int first, int second)
{
    if (first == '+' && second == '+')
    {
        return "INC";
    }

    if (second == '=')
    {
        switch (first)
        {
        case '=':
            return "EQL";
        case '!':
            return "NEQ";
        case '<':
            return "LEQ";
        case '>':
            return "GEQ";
        }
    }

    if (first == '&' && second == '&')
    {
        return "AND";
    }

    if (first == '|' && second == '|')
    {
        return "OR";
    }

    return NULL;
}

int main(){
    FILE *source = fopen("testfile.txt", "r");
    if(source == NULL){
        perror("无法打开 testfile.txt");
        return 1;
    }

    FILE *lexer = fopen("lexer.txt", "w");

    if (lexer == NULL)
    {
        perror("无法打开 lexer.txt");
        fclose(source);
        return 1;
    }

    ErrorList errors;
    initErrorList(&errors);

    TokenList tokens;
    initTokenList(&tokens);
    int lexicalErrorCount = 0;
    int ch;
    int line = 1;
    while((ch = fgetc(source)) != EOF){
        //putchar(ch);
        if(ch == '\n'){
            line++;
            continue;
        }

        if(isspace(ch)){
            continue;
        }

        //转义字符
        if (ch == '\'')
        {
            char word[8];
            size_t length = 0;

            word[length++] = (char)ch;

            int next = fgetc(source);

            if (next == '\\')
            {
                word[length++] = (char)next;
                next = fgetc(source);
            }

            if (next == EOF)
            {
                fprintf(stderr, "Incomplete character constant\n");
                freeErrorList(&errors);
                freeTokenList(&tokens);
                fclose(lexer);
                fclose(source);
                return 1;
            }

            word[length++] = (char)next;

            int closing = fgetc(source);

            if (closing != '\'')
            {
                fprintf(stderr, "Missing closing single quote\n");
                freeErrorList(&errors);
                freeTokenList(&tokens);
                fclose(lexer);
                fclose(source);
                return 1;
            }

            word[length++] = (char)closing;
            word[length] = '\0';

            if (!addToken(&tokens, "CHARCON", word, line))
            {
                fprintf(stderr, "Failed to add token\n");
                freeTokenList(&tokens);
                freeErrorList(&errors);
                fclose(lexer);
                fclose(source);
                return 1;
            }
            fprintf(lexer, "CHARCON %s\n", word);
            continue;
        }

        //字符串常量处理
        if (ch == '"')
        {
            char text[4096];
            size_t length = 0;

            text[length++] = (char)ch;

            int next;

            while ((next = fgetc(source)) != EOF)
            {
                if (length >= sizeof(text) - 1)
                {
                    fprintf(stderr, "String constant is too long\n");
                    freeErrorList(&errors);
                    freeTokenList(&tokens);
                    fclose(lexer);
                    fclose(source);
                    return 1;
                }

                text[length++] = (char)next;

                if (next == '"')
                {
                    break;
                }
            }

            if (next == EOF)
            {
                fprintf(stderr, "Missing closing double quote\n");
                freeErrorList(&errors);
                freeTokenList(&tokens);
                fclose(lexer);
                fclose(source);
                return 1;
            }

            text[length] = '\0';

            if (!addToken(&tokens, "STRCON", text, line))
            {
                fprintf(stderr, "Failed to add token\n");
                freeTokenList(&tokens);
                freeErrorList(&errors);
                fclose(lexer);
                fclose(source);
                return 1;
            }
            fprintf(lexer, "STRCON %s\n", text);
            continue;
        }

        //标识符识别
        if(isalpha(ch) || ch == '_'){
            char word[1024];
            size_t length = 0;
            do{
                if(length >= sizeof(word) - 1){
                    fprintf(stderr, "Identifier is too long\n");
                    freeErrorList(&errors);
                    freeTokenList(&tokens);
                    fclose(lexer);
                    fclose(source);
                    return 1;
                }

                word[length++] = (char)ch;
                ch = fgetc(source);
            } while (isalnum(ch) || ch == '_');

            word[length] = '\0';

            if(ch != EOF){
                ungetc(ch, source);
            }

            // fprintf(lexer, "%s %s\n", getWordType(word), word);
            const char *type = getWordType(word);

            if(!addToken(&tokens,type,word,line)){
                fprintf(stderr, "Failed to add token\n");
                freeTokenList(&tokens);
                freeErrorList(&errors);
                fclose(lexer);
                fclose(source);
                return 1;
            }

            fprintf(lexer, "%s %s\n", type, word);
            continue;
        }

        if(isdigit(ch)){
            char number[1024];
            size_t length = 0;

            do{
                if(length>= sizeof(number) - 1){
                    fprintf(stderr, "Integer constant is too long\n");
                    freeErrorList(&errors);
                    freeTokenList(&tokens);
                    fclose(lexer);
                    fclose(source);
                    return 1;
                }

                number[length++] = (char)ch;
                ch = fgetc(source);
            } while (isdigit(ch));

            number[length] = '\0';

            if(ch != EOF){
                ungetc(ch, source);
            }

            if (!addToken(&tokens, "INTCON", number, line))
            {
                fprintf(stderr, "Failed to add token\n");
                freeTokenList(&tokens);
                freeErrorList(&errors);
                fclose(lexer);
                fclose(source);
                return 1;
            }
            fprintf(lexer, "INTCON %s\n", number);
            continue;
        }

        //处理注释
        if (ch == '/')
        {
            int next = fgetc(source);

            if (next == '/')
            {
                while ((next = fgetc(source)) != EOF && next != '\n')
                {
                    /* 跳过注释内容 */
                }

                if (next == '\n')
                {
                    line++;
                }

                continue;
            }

            if (next == '*')
            {
                int previous = 0;

                while ((next = fgetc(source)) != EOF)
                {
                    if (next == '\n')
                    {
                        line++;
                    }

                    if (previous == '*' && next == '/')
                    {
                        break;
                    }

                    previous = next;
                }

                continue;
            }

            if (next != EOF)
            {
                ungetc(next, source);
            }
        }

        if (ch == '=' || ch == '!' || ch == '<' ||
            ch == '>' || ch == '&' || ch == '|' || ch == '+')
        {

            int next = fgetc(source);
            const char *doubleType = getDoubleCharType(ch, next);

            if (doubleType != NULL)
            {
                char value[3] = {(char)ch, (char)next, '\0'};

                if (!addToken(&tokens, doubleType, value, line))
                {
                    fprintf(stderr, "Failed to add token\n");
                    freeTokenList(&tokens);
                    freeErrorList(&errors);
                    fclose(lexer);
                    fclose(source);
                    return 1;
                }

                fprintf(lexer, "%s %c%c\n", doubleType, ch, next);
                continue;
            }

            if (next != EOF)
            {
                ungetc(next, source);
            }
        }

        //处理单独的&,|
        if (ch == '&' || ch == '|')
        {
            if (!addError(&errors, line, 'a'))
            {
                fprintf(stderr, "Failed to add error\n");
                freeErrorList(&errors);
                freeTokenList(&tokens);
                fclose(lexer);
                fclose(source);
                return 1;
            }

            lexicalErrorCount++;

            const char *type = ch == '&' ? "AND" : "OR";
            char value[2] = {(char)ch, '\0'};

            if (!addToken(&tokens, type, value, line))
            {
                fprintf(stderr, "Failed to add token\n");
                freeTokenList(&tokens);
                freeErrorList(&errors);
                fclose(lexer);
                fclose(source);
                return 1;
            }

            fprintf(lexer, "%s %s\n", type, value);
            continue;
        }

        const char *type = getSingleCharType(ch);

        if(type != NULL){
            char value[2] = {(char)ch, '\0'};

            if (!addToken(&tokens, type, value, line))
            {
                fprintf(stderr, "Failed to add token\n");
                freeTokenList(&tokens);
                freeErrorList(&errors);
                fclose(lexer);
                fclose(source);
                return 1;
            }
            fprintf(lexer,"%s %c\n", type, ch);
        }else{
            fprintf(stderr, "Unexpected character at line %d: %c\n", line, ch);
            lexicalErrorCount++;
        }
    }

#if PARSER_DEBUG_ENABLED
    printf("Stored tokens: %lu\n", (unsigned long)tokens.size);
    for (size_t i = 0; i < tokens.size;i++){
        printf("%s %s %d\n", tokens.data[i].type, tokens.data[i].value, tokens.data[i].line);
    }
#endif

    Parser parser = {0};
    parser.tokens = &tokens;
    parser.errors = &errors;

    int result = parseCompUnit(&parser);

    if (parser.memoryError)
    {
        fprintf(stderr, "Out of memory while building syntax tree\n");
        result = 0;
    }

    int errorOutputOk = writeErrorOutput("error.txt", &errors);

    if (!errorOutputOk)
    {
        fprintf(stderr, "Failed to write error.txt\n");
    }
    int outputOk = 1;

    if (result && lexicalErrorCount == 0 && errors.size == 0)
    {
        outputOk = writeParserOutput(
            "parser.txt", parser.nodes[0], PARSER_OUTPUT_ENABLED);
    }
    else if (PARSER_OUTPUT_ENABLED)
    {
        // 清空上一次成功运行的输出，避免留下过期结果。
        FILE *emptyOutput = fopen("parser.txt", "w");
        if (emptyOutput == NULL)
        {
            outputOk = 0;
        }
        else if (fclose(emptyOutput) != 0)
        {
            outputOk = 0;
        }
    }

    if (!outputOk)
    {
        fprintf(stderr, "Failed to write parser.txt\n");
    }

#if PARSER_DEBUG_ENABLED
    printf("CompUnit: %d, position: %lu\n",
           result, (unsigned long)parser.pos);
    const Token *token = currentToken(&parser);

    if (token == NULL)
    {
        printf("All tokens consumed\n");
    }
    else
    {
        printf("Remaining: %s %s\n", token->type, token->value);
    }
#endif
    freeParserTrees(&parser);
    freeTokenList(&tokens);
    freeErrorList(&errors);
    fclose(lexer);
    fclose(source);
    return parser.memoryError || !outputOk || !errorOutputOk;
}
