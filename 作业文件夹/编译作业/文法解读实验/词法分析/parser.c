// 本文件同时包含词法扫描、递归下降语法分析以及整个编译器的 main 入口。
// parse 函数通常返回 1 表示识别成功，0 表示失败；缺符号恢复成功也可能返回 1。
#include<stdio.h>
#include<ctype.h>
#include<string.h>
#include <stdlib.h>
#include "syntax_tree.h"
#include "error_list.h"
#include "semantic.h"

#ifndef SYMBOL_OUTPUT_ENABLED
#define SYMBOL_OUTPUT_ENABLED 1
#endif

#ifndef PARSER_OUTPUT_ENABLED
#define PARSER_OUTPUT_ENABLED 1
#endif

#ifndef PARSER_DEBUG_ENABLED
#define PARSER_DEBUG_ENABLED 0
#endif

typedef struct
{
    const TokenList *tokens;
    // 下一个待解析 token 的下标；不是源文件的字符位置。
    size_t pos;

    ErrorList *errors;

    // 暂存尚未归入父节点的子树根，finishNode 会转移这些子树的所有权。
    SyntaxNode **nodes;
    size_t nodeCount;
    size_t nodeCapacity;
    int memoryError;
} Parser;

// 试探解析失败时，三个状态必须一起恢复，不能只恢复 token 位置。
typedef struct
{
    size_t pos;
    size_t nodeCount;
    size_t errorCount;
} ParserMark;

int parseExp(Parser *parser);
int parseBlock(Parser *parser);
int parseCaseStmt(Parser *parser);

// 取得下一个待解析的 Token；到达末尾返回 NULL，不移动 pos。
const Token *currentToken(const Parser *parser)
{
    if (parser->pos >= parser->tokens->size)
    {
        return NULL;
    }

    return &parser->tokens->data[parser->pos];
}

// 语法树节点栈、检查点与回退
// 记录 token 位置、节点栈长度和错误数量，作为一次文法试探的回退点。
ParserMark markParser(const Parser *parser)
{
    ParserMark mark = {
        parser->pos,
        parser->nodeCount,
        parser->errors->size};

    return mark;
}

// 把一棵子树压入临时节点栈；容量不足则扩容，失败置 memoryError。
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

// 撤销试探：释放检查点之后的子树，同时恢复 pos 和错误表长度。
void rollbackParser(Parser *parser, ParserMark mark)
{
    while (parser->nodeCount > mark.nodeCount)
    {
        parser->nodeCount--;
        freeSyntaxTree(parser->nodes[parser->nodeCount]);
        parser->nodes[parser->nodeCount] = NULL;
    }

    parser->pos = mark.pos;
    // 回退也撤销这次试探记录的错误，防止换一条分支后重复或误报。
    parser->errors->size = mark.errorCount;
}

// 将检查点后产生的子树收为孩子，包装成 name 指定的非终结符并重新入栈。
// 返回 1 表示构树成功；节点栈保留的是当前尚未被父节点接收的各棵子树。
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

    // 例如 Stmt 的 mark 之后留下 LVal、等号、Exp、分号，都会成为新 Stmt 的直接孩子。
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

// 释放解析器持有的所有树和节点栈；应在释放 TokenList 之前调用。
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

// 消费一个 token，同时创建终结符节点；不要仅用 pos++ 替代，否则树会漏节点。
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

// 只判断当前 token 类别，不消费、不建树；type 是类别码，不是源文本。
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
// 向前查看 offset 个 token，先做边界检查；offset=0 是当前项。
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

// 查看下一个 token 的类别，用于区分函数调用、类型转换等共同前缀。
int checkNextToken(const Parser *parser, const char *type)
{
    return checkTokenAt(parser, 1, type);
}

// 匹配成功才调用 advanceToken；不匹配返回 0，由调用者决定换分支还是失败。
int matchToken(Parser *parser, const char *type)
{
    if (!checkToken(parser, type))
    {
        return 0;
    }

    return advanceToken(parser);
}

// 仅用于允许按“缺少分隔符”恢复的位置
// 用于题目允许恢复的缺失分隔符：匹配则消费，缺失则记录 i/j/k 并返回成功。
// 缺失时不前进，也不伪造终结符，保留当前 token 给后续成分处理。
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
// 判断可选表达式能否开始；新增表达式前缀时，需要同步检查这个入口集合。
int isExpStart(const Parser *parser)
{
    return checkToken(parser, "IDENFR") ||
           checkToken(parser, "INTCON") ||
           checkToken(parser, "CHARCON") ||
           checkToken(parser, "LPARENT") ||
           checkToken(parser, "PLUS") ||
           checkToken(parser, "MINU") ||
           checkToken(parser, "NOT");
}

// BType → 'int' | 'char'
// 识别 int/char，构造 BType；该节点虽可能不输出，但仍保留在树中。
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
// 识别整数或字符常量，包装为 Number；这里只认结构，不求数值。
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
// 识别前缀 +、-、!，包装为 UnaryOp，后续操作数由 UnaryExp 识别。
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
// 识别变量名或一维数组元素：Ident [下标]；缺右方括号按 k 恢复。
// LVal 也可用于表达式读取，是否为常量、是否能赋值由语义分析判断。
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
// 识别括号表达式、左值或 Number；括号中使用 Exp，缺右括号按 j 恢复。
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
// 识别逗号分隔的实参 Exp；不消费调用外层的左右圆括号。
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
// 区分一元运算、函数调用、显式类型转换和 PrimaryExp。
// 先看关键 token 处理共同前缀；调用中的实参和括号表达式的树结构不同。
int parseUnaryExp(Parser *parser){
    ParserMark start = markParser(parser);

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

    // 前面都不属于时，最后按 PrimaryExp 尝试；函数调用的名字与普通 LVal 由前看区分。
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
// 解析乘除模层，先识别 UnaryExp，再循环接运算符与右操作数。
// 通过反复 finishNode 保留左结合树，避免直接展开左递归产生无限递归。
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
// 解析加减层；每轮把已有左子树、运算符和右 MulExp 包成新的 AddExp。
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
// 普通表达式入口为 AddExp；不要与允许比较、逻辑运算的 Cond 混淆。
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
// 解析 <、>、<=、>=，操作数由更高优先级的 AddExp 识别。
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
// 解析 ==、!=；以 RelExp 为操作数，并保留原文法的左结合结构。
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
// 解析逻辑与；循环接 AND 和 EqExp，词法阶段可将单独 & 记录 a 后恢复为 AND。
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
// 解析逻辑或；以 LAndExp 为操作数，优先级低于逻辑与。
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
// 条件表达式入口为 LOrExp，可覆盖逻辑、比较及内部算术层。
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
// 按 AddExp 识别常量表达式的结构；语法识别本身不证明表达式一定是常量。
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
// 识别常量初值：ConstExp、花括号列表或字符串；得到 ConstInitVal。
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
// 识别单个常量名、可选数组长度与必需的初值；不消费声明结尾分号。
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
// 识别整条 const 声明，允许多个逗号分隔的 ConstDef；末尾分号由本层负责。
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
// 识别变量初值：Exp、花括号列表或字符串，和常量初值的节点名称区分。
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
// 识别单个变量定义；数组长度和初始化均按文法可选，语义稍后登记名字。
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
// 识别可选 static、基本类型和多个 VarDef；最终消费或恢复缺失的分号。
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
// 声明的统一入口：区分 ConstDecl 与 VarDecl，外层包装为 Decl。
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
// 识别普通函数返回类型 void/int/char；不同于只接受 int/char 的 BType。
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
// 识别一个形参：基本类型、名字、可选 []；数组形参不在括号内写长度。
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
// 识别有序形参列表，直接孩子为 FuncFParam 和逗号，供语义阶段提取签名。
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
// 语句总入口：先按关键字分派，再处理赋值与表达式语句的共同前缀。
// 只在有对应错误规则的位置使用 expectToken，其他必需结构失败则回退。
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

    // a=1; 与 a+1; 都以 LVal 开头：先试赋值，未见等号则完整回退再读表达式。
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
// 识别 case Number 或 default 标签及其后的语句，遇到下一标签或 } 时结束。
// 每个分支包装为 CaseStmt；标签值重复检查属于 semantic.c。
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
// 块内成分入口：看声明起始 token 决定走 Decl 还是 Stmt。
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
// 识别花括号内的 BlockItem 序列；显式检查 EOF，避免找不到 } 时循环不结束。
// 这里只建 Block 节点，是否创建作用域由语义阶段的 analyzeBlock 决定。
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
// 识别普通函数的返回类型、名字、可选形参表和 Block，包装为 FuncDef。
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
// 识别 int main() Block，包装为 MainFuncDef；main 是保留关键字。
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
// 用类型后面的标识符与左括号前看，区分全局声明和普通函数定义。
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
// 检查 类型 + 标识符 + 左括号 的函数起始模式，不消费任何 token。
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
// 顶层按全局声明、普通函数、main 的顺序解析，并要求最后无剩余 token。
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

// 完整标识符读入后查询关键字表，未命中返回 IDENFR；当前实现按大小写精确比较。
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

// 将单字符符号映射为 token 类别；没有对应符号时返回 NULL。
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
// 识别双字符运算符；是否尝试读取第二字符，还由 main 中的候选集合决定。
const char *getDoubleCharType(int first, int second)
{
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

// 编译器入口：词法扫描并存 token → 递归下降构树 → 语义分析 → 输出并释放资源。
// 读取当前工作目录的 testfile.txt，分析 SysY 源码，不执行其中的语句。
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
    // fgetc 的返回类型为 int，必须能区分所有字节值与 EOF。
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
            ch == '>' || ch == '&' || ch == '|')
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

        // 合法双字符优先匹配；单独 &/| 记 a，但仍产生 AND/OR token 让语法分析继续。
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

    // 词法结束后 TokenList 固定，递归下降读取它并构造 CompUnit 树。
    int result = parseCompUnit(&parser);

    if (parser.memoryError)
    {
        fprintf(stderr, "Out of memory while building syntax tree\n");
        result = 0;
    }

    SymbolTable symbols;
    initSymbolTable(&symbols);

    SemanticContext semantic;
    initSemanticContext(&semantic, &symbols, &errors);

    int semanticOk = 0;

    if (result)
    {
        // parseCompUnit 成功时根位于 nodes[0]；语义阶段复用现有树，不重新读取源码。
        semanticOk = analyzeSemantics(&semantic, parser.nodes[0]);

        if (!semanticOk)
        {
            fprintf(stderr, "Semantic analysis could not complete\n");
        }
    }

    // semanticOk 是过程完成状态，不是无语义错误；还必须检查错误表及词法错误计数。
    int canOutputSymbols =
        result &&
        semanticOk &&
        lexicalErrorCount == 0 &&
        errors.size == 0;

    // 出错时输出空表，清除上一次运行留下的符号输出。
    SymbolTable emptySymbols = {0};

    int symbolOutputOk = writeSymbolOutput(
        "symbol.txt",
        canOutputSymbols ? &symbols : &emptySymbols,
        SYMBOL_OUTPUT_ENABLED);

    if (!symbolOutputOk)
    {
        fprintf(stderr, "Failed to write symbol.txt\n");
    }

    // 语义分析结束后统一输出，才能同时包含词法、语法和语义阶段记录的错误。
    int errorOutputOk = writeErrorOutput("error.txt", &errors);

    if (!errorOutputOk)
    {
        fprintf(stderr, "Failed to write error.txt\n");
    }
    int outputOk = 1;

    if (result && semanticOk && lexicalErrorCount == 0 && errors.size == 0)
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
    freeSymbolTable(&symbols);
    // 树中的终结符借用 TokenList 的 token 指针，因此先释放树，再释放 token。
    freeParserTrees(&parser);
    freeTokenList(&tokens);
    freeErrorList(&errors);
    fclose(lexer);
    fclose(source);
    return !result || parser.memoryError || !semanticOk || !symbolOutputOk || !outputOk || !errorOutputOk;
}
