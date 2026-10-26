#include "semantic.h"
#include <string.h>
#include<stdlib.h>
static void analyzeNode(SemanticContext *context, const SyntaxNode *node);
static DataType analyzeExpression(
    SemanticContext *context,
    const SyntaxNode *node);

void initSemanticContext(
    SemanticContext *context,
    SymbolTable *symbols,
    ErrorList *errors)
{
    context->symbols = symbols;
    context->errors = errors;

    context->currentReturnType = TYPE_VOID;

    context->loopDepth = 0;
    context->switchDepth = 0;

    context->memoryError = 0;
}

static int isNode(const SyntaxNode *node, const char *name)
{
    return node != NULL && strcmp(node->name, name) == 0;
}

static const SyntaxNode *findChild(
    const SyntaxNode *node,
    const char *name)
{
    if (node == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < node->childCount; i++)
    {
        const SyntaxNode *child = node->children[i];

        if (isNode(child, name))
        {
            return child;
        }
    }

    return NULL;
}

static void reportSemanticError(
    SemanticContext *context,
    int line,
    char code)
{
    if (context->memoryError)
    {
        return;
    }

    if (!addError(context->errors, line, code))
    {
        context->memoryError = 1;
    }
}

static const Symbol *resolveIdentifier(
    SemanticContext *context,
    const SyntaxNode *identifier)
{
    if (context->memoryError ||
        identifier == NULL ||
        identifier->token == NULL)
    {
        return NULL;
    }

    const Token *token = identifier->token;
    const Symbol *symbol = findSymbol(context->symbols, token->value);

    if (symbol == NULL)
    {
        reportSemanticError(context, token->line, 'c');
    }

    return symbol;
}

static DataType getBaseType(const SyntaxNode *node)
{
    // 只接受基本类型节点或函数返回类型节点。
    if (!isNode(node, "BType") && !isNode(node, "FuncType"))
    {
        return TYPE_ERROR;
    }

    if (findChild(node, "INTTK") != NULL)
    {
        return TYPE_INT;
    }

    if (findChild(node, "CHARTK") != NULL)
    {
        return TYPE_CHAR;
    }

    if (isNode(node, "FuncType") &&
        findChild(node, "VOIDTK") != NULL)
    {
        return TYPE_VOID;
    }

    return TYPE_ERROR;
}

static DataType getDefinitionType(
    const SyntaxNode *definition,
    DataType baseType)
{
    if (definition == NULL ||
        (baseType != TYPE_INT && baseType != TYPE_CHAR))
    {
        return TYPE_ERROR;
    }

    // 定义节点直接包含左中括号，说明声明的是数组。
    if (findChild(definition, "LBRACK") != NULL)
    {
        if (baseType == TYPE_INT)
        {
            return TYPE_INT_ARRAY;
        }

        return TYPE_CHAR_ARRAY;
    }

    return baseType;
}

static DataType getNumberType(const SyntaxNode *node)
{
    if (!isNode(node, "Number"))
    {
        return TYPE_ERROR;
    }

    if (findChild(node, "INTCON") != NULL)
    {
        return TYPE_INT;
    }

    if (findChild(node, "CHARCON") != NULL)
    {
        return TYPE_CHAR;
    }

    return TYPE_ERROR;
}

static int isScalarType(DataType type)
{
    return type == TYPE_INT || type == TYPE_CHAR;
}

static DataType getBinaryResultType(
    SemanticContext *context,
    DataType leftType,
    DataType rightType,
    const SyntaxNode *operatorNode)
{
    if (context->memoryError ||
        operatorNode == NULL ||
        operatorNode->token == NULL)
    {
        return TYPE_ERROR;
    }

    // 子表达式已经出错，避免继续产生连带的类型错误。
    if (leftType == TYPE_ERROR || rightType == TYPE_ERROR)
    {
        return TYPE_ERROR;
    }

    if (!isScalarType(leftType) ||
        !isScalarType(rightType) ||
        leftType != rightType)
    {
        reportSemanticError(
            context,
            operatorNode->token->line,
            'e');

        return TYPE_ERROR;
    }

    // 算术运算保留操作数类型。
    if (isNode(operatorNode, "PLUS") ||
        isNode(operatorNode, "MINU") ||
        isNode(operatorNode, "MULT") ||
        isNode(operatorNode, "DIV") ||
        isNode(operatorNode, "MOD"))
    {
        return leftType;
    }

    // 关系、相等性和逻辑运算产生 int 类型的真假值。
    if (isNode(operatorNode, "LSS") ||
        isNode(operatorNode, "LEQ") ||
        isNode(operatorNode, "GRE") ||
        isNode(operatorNode, "GEQ") ||
        isNode(operatorNode, "EQL") ||
        isNode(operatorNode, "NEQ") ||
        isNode(operatorNode, "AND") ||
        isNode(operatorNode, "OR"))
    {
        return TYPE_INT;
    }

    return TYPE_ERROR;
}

static const Token *getFirstToken(const SyntaxNode *node)
{
    if (node == NULL)
    {
        return NULL;
    }

    if (node->kind == NODE_TERMINAL)
    {
        return node->token;
    }

    for (size_t i = 0; i < node->childCount; i++)
    {
        const Token *token = getFirstToken(node->children[i]);

        if (token != NULL)
        {
            return token;
        }
    }

    return NULL;
}

static void reportNodeError(
    SemanticContext *context,
    const SyntaxNode *node,
    char code)
{
    const Token *token = getFirstToken(node);

    if (token != NULL)
    {
        reportSemanticError(context, token->line, code);
    }
}

static DataType getLValType(
    SemanticContext *context,
    const SyntaxNode *node,
    DataType indexType)
{
    if (context->memoryError || !isNode(node, "LVal"))
    {
        return TYPE_ERROR;
    }

    const SyntaxNode *identifier = findChild(node, "IDENFR");
    const Symbol *symbol = resolveIdentifier(context, identifier);

    if (symbol == NULL)
    {
        return TYPE_ERROR;
    }

    int line = identifier->token->line;

    // 函数名不能作为变量或数组引用。
    if (symbol->kind == SYMBOL_FUNC)
    {
        reportSemanticError(context, line, 'e');
        return TYPE_ERROR;
    }

    // 没有下标，类型直接来自符号表。
    if (findChild(node, "LBRACK") == NULL)
    {
        return symbol->type;
    }

    // 有下标时，被引用的符号必须是数组。
    if (symbol->type != TYPE_INT_ARRAY &&
        symbol->type != TYPE_CHAR_ARRAY)
    {
        reportSemanticError(context, line, 'e');
        return TYPE_ERROR;
    }

    if (indexType == TYPE_ERROR)
    {
        return TYPE_ERROR;
    }

    if (indexType != TYPE_INT)
    {
        const SyntaxNode *index = findChild(node, "Exp");
        const Token *token = getFirstToken(index);

        reportSemanticError(
            context,
            token != NULL ? token->line : line,
            'e');

        return TYPE_ERROR;
    }

    return symbol->type == TYPE_INT_ARRAY
               ? TYPE_INT
               : TYPE_CHAR;
}

static DataType getCallType(
    SemanticContext *context,
    const SyntaxNode *node,
    const DataType *argumentTypes,
    size_t argumentCount)
{
    if (context->memoryError || !isNode(node, "UnaryExp"))
    {
        return TYPE_ERROR;
    }

    const SyntaxNode *identifier = findChild(node, "IDENFR");
    const Symbol *function = resolveIdentifier(context, identifier);

    if (function == NULL)
    {
        return TYPE_ERROR;
    }

    int line = identifier->token->line;

    // 找到了名字，但它不是函数。
    if (function->kind != SYMBOL_FUNC)
    {
        reportSemanticError(context, line, 'e');
        return TYPE_ERROR;
    }

    // 先检查参数个数。
    if (argumentCount != function->paramCount)
    {
        reportSemanticError(context, line, 'd');
        return TYPE_ERROR;
    }

    int hasArgumentError = 0;

    // 个数相同，再按顺序比较参数类型。
    for (size_t i = 0; i < argumentCount; i++)
    {
        if (argumentTypes[i] == TYPE_ERROR)
        {
            hasArgumentError = 1;
            continue;
        }

        if (argumentTypes[i] != function->paramTypes[i])
        {
            reportSemanticError(context, line, 'e');
            return TYPE_ERROR;
        }
    }

    if (hasArgumentError)
    {
        return TYPE_ERROR;
    }

    return function->type;
}

static DataType analyzeCall(
    SemanticContext *context,
    const SyntaxNode *node)
{
    if (context->memoryError)
    {
        return TYPE_ERROR;
    }

    const SyntaxNode *parameters = findChild(node, "FuncRParams");
    size_t count = 0;

    if (parameters != NULL)
    {
        for (size_t i = 0; i < parameters->childCount; i++)
        {
            if (isNode(parameters->children[i], "Exp"))
            {
                count++;
            }
        }
    }

    if (count == 0)
    {
        return getCallType(context, node, NULL, 0);
    }

    DataType *types = malloc(count * sizeof(DataType));

    if (types == NULL)
    {
        context->memoryError = 1;
        return TYPE_ERROR;
    }

    size_t index = 0;

    for (size_t i = 0; i < parameters->childCount; i++)
    {
        const SyntaxNode *argument = parameters->children[i];

        if (!isNode(argument, "Exp"))
        {
            continue;
        }

        types[index] = analyzeExpression(context, argument);
        index++;

        if (context->memoryError)
        {
            free(types);
            return TYPE_ERROR;
        }
    }

    DataType result = getCallType(context, node, types, count);
    free(types);

    return result;
}

static DataType analyzeExpression(
    SemanticContext *context,
    const SyntaxNode *node)
{
    if (context->memoryError || node == NULL)
    {
        return TYPE_ERROR;
    }

    if (isNode(node, "Number"))
    {
        return getNumberType(node);
    }

    if (isNode(node, "LVal"))
    {
        const SyntaxNode *index = findChild(node, "Exp");
        DataType indexType = TYPE_ERROR;

        if (index != NULL)
        {
            indexType = analyzeExpression(context, index);
        }

        return getLValType(context, node, indexType);
    }

    if (isNode(node, "UnaryExp"))
    {
        // 函数调用。
        if (findChild(node, "IDENFR") != NULL)
        {
            return analyzeCall(context, node);
        }

        const SyntaxNode *castType = findChild(node, "BType");
        const SyntaxNode *unaryOp = findChild(node, "UnaryOp");

        // 显式类型转换，或者单目运算。
        if (castType != NULL || unaryOp != NULL)
        {
            const SyntaxNode *operand = findChild(node, "UnaryExp");
            DataType operandType = analyzeExpression(context, operand);

            if (operandType == TYPE_ERROR)
            {
                return TYPE_ERROR;
            }

            if (!isScalarType(operandType))
            {
                const Token *token = getFirstToken(node);

                if (token != NULL)
                {
                    reportSemanticError(context, token->line, 'e');
                }

                return TYPE_ERROR;
            }

            if (castType != NULL)
            {
                return getBaseType(castType);
            }

            if (findChild(unaryOp, "NOT") != NULL)
            {
                return TYPE_INT;
            }

            // 单目 +、- 保留操作数类型。
            return operandType;
        }

        return analyzeExpression(
            context,
            findChild(node, "PrimaryExp"));
    }

    // 各级二元运算节点。
    if (isNode(node, "MulExp") ||
        isNode(node, "AddExp") ||
        isNode(node, "RelExp") ||
        isNode(node, "EqExp") ||
        isNode(node, "LAndExp") ||
        isNode(node, "LOrExp"))
    {
        if (node->childCount == 1)
        {
            return analyzeExpression(context, node->children[0]);
        }

        if (node->childCount == 3)
        {
            DataType leftType =
                analyzeExpression(context, node->children[0]);

            DataType rightType =
                analyzeExpression(context, node->children[2]);

            return getBinaryResultType(
                context,
                leftType,
                rightType,
                node->children[1]);
        }

        return TYPE_ERROR;
    }

    // 这些节点只有一个表示表达式内容的非终结符子节点。
    if (isNode(node, "PrimaryExp") ||
        isNode(node, "Exp") ||
        isNode(node, "ConstExp") ||
        isNode(node, "Cond"))
    {
        for (size_t i = 0; i < node->childCount; i++)
        {
            const SyntaxNode *child = node->children[i];

            if (child->kind == NODE_NONTERMINAL)
            {
                return analyzeExpression(context, child);
            }
        }
    }

    return TYPE_ERROR;
}

static void registerDefinition(
    SemanticContext *context,
    const SyntaxNode *definition,
    DataType baseType,
    SymbolKind kind)
{
    if (context->memoryError)
    {
        return;
    }

    const SyntaxNode *identifier = findChild(definition, "IDENFR");

    if (identifier == NULL || identifier->token == NULL)
    {
        return;
    }

    DataType type = getDefinitionType(definition, baseType);

    if (type == TYPE_ERROR)
    {
        return;
    }

    Symbol symbol = {0};
    symbol.name = identifier->token->value;
    symbol.type = type;
    symbol.kind = kind;
    symbol.line = identifier->token->line;

    int result = addSymbol(context->symbols, &symbol);

    if (result == 0)
    {
        // 当前作用域中已有同名符号。
        if (!addError(context->errors, symbol.line, 'b'))
        {
            context->memoryError = 1;
        }
    }
    else if (result < 0)
    {
        context->memoryError = 1;
    }
}

static void analyzeDefinitionValues(
    SemanticContext *context,
    const SyntaxNode *definition,
    DataType baseType)
{
    if (context->memoryError)
    {
        return;
    }

    const SyntaxNode *length = findChild(definition, "ConstExp");

    if (length != NULL)
    {
        DataType lengthType = analyzeExpression(context, length);

        if (lengthType != TYPE_ERROR && lengthType != TYPE_INT)
        {
            reportNodeError(context, length, 'e');
        }
    }

    const SyntaxNode *initializer = findChild(definition, "InitVal");

    if (initializer == NULL)
    {
        initializer = findChild(definition, "ConstInitVal");
    }

    if (initializer == NULL || context->memoryError)
    {
        return;
    }

    DataType definitionType = getDefinitionType(definition, baseType);
    const SyntaxNode *string = findChild(initializer, "STRCON");

    if (string != NULL)
    {
        if (definitionType != TYPE_CHAR_ARRAY)
        {
            reportNodeError(context, string, 'e');
        }

        return;
    }

    // 花括号中的每项与元素类型比较，单个表达式与定义类型比较。
    DataType expectedType = findChild(initializer, "LBRACE") != NULL
                                ? baseType
                                : definitionType;
    int reportedMismatch = 0;

    for (size_t i = 0; i < initializer->childCount; i++)
    {
        if (context->memoryError)
        {
            return;
        }

        const SyntaxNode *value = initializer->children[i];

        if (!isNode(value, "Exp") && !isNode(value, "ConstExp"))
        {
            continue;
        }

        DataType actualType = analyzeExpression(context, value);

        if (actualType != TYPE_ERROR && expectedType != TYPE_ERROR &&
            actualType != expectedType && !reportedMismatch)
        {
            reportNodeError(context, value, 'e');
            reportedMismatch = 1;
        }
    }
}

static void analyzeDeclaration(
    SemanticContext *context,
    const SyntaxNode *declaration)
{
    if (context->memoryError || declaration == NULL)
    {
        return;
    }

    SymbolKind kind;
    const char *definitionName;

    if (isNode(declaration, "ConstDecl"))
    {
        kind = SYMBOL_CONST;
        definitionName = "ConstDef";
    }
    else if (isNode(declaration, "VarDecl"))
    {
        kind = findChild(declaration, "STATICTK") != NULL
                   ? SYMBOL_STATIC_VAR
                   : SYMBOL_VAR;

        definitionName = "VarDef";
    }
    else
    {
        return;
    }

    const SyntaxNode *typeNode = findChild(declaration, "BType");
    DataType baseType = getBaseType(typeNode);

    if (baseType == TYPE_ERROR)
    {
        return;
    }

    for (size_t i = 0; i < declaration->childCount; i++)
    {
        if (context->memoryError)
        {
            return;
        }

        const SyntaxNode *child = declaration->children[i];

        if (isNode(child, definitionName))
        {
            registerDefinition(context, child, baseType, kind);
            analyzeDefinitionValues(context, child, baseType);
        }
    }
}

static void analyzeBlock(SemanticContext *context,const SyntaxNode *block,int createScope){
    if(context->memoryError || !isNode(block,"Block")){
        return;
    }

    if(createScope){
        if(enterScope(context->symbols)==0){
            context->memoryError = 1;
            return;
        }
    }

    for (size_t i = 0; i < block->childCount;i++){
        if(context->memoryError){
            break;
        }

        analyzeNode(context, block->children[i]);
    }

    if(createScope){
        leaveScope(context->symbols);
    }
}

static void registerParameters(SemanticContext *context,const SyntaxNode *parameters){
    if(context->memoryError || !isNode(parameters,"FuncFParams")){
        return;
    }

    for (size_t i = 0; i < parameters->childCount;i++){
        if(context->memoryError){
            return;
        }

        const SyntaxNode *parameter = parameters->children[i];

        if(!isNode(parameter,"FuncFParam")){
            continue;
        }

        const SyntaxNode *typeNode = findChild(parameter, "BType");
        DataType baseType = getBaseType(typeNode);

        registerDefinition(context, parameter, baseType, SYMBOL_VAR);

    }
}

static int collectParameterTypes(
    SemanticContext *context,
    const SyntaxNode *parameters,
    Symbol *function)
{
    function->paramTypes = NULL;
    function->paramCount = 0;

    if (context->memoryError)
    {
        return 0;
    }

    // 无参函数没有 FuncFParams 节点。
    if (parameters == NULL)
    {
        return 1;
    }

    // 第一遍：统计形参数量。
    size_t count = 0;

    for (size_t i = 0; i < parameters->childCount; i++)
    {
        if (isNode(parameters->children[i], "FuncFParam"))
        {
            count++;
        }
    }

    if (count == 0)
    {
        return 1;
    }

    DataType *types = malloc(count * sizeof(DataType));

    if (types == NULL)
    {
        context->memoryError = 1;
        return 0;
    }

    // 第二遍：按形参顺序保存类型。
    size_t index = 0;

    for (size_t i = 0; i < parameters->childCount; i++)
    {
        const SyntaxNode *parameter = parameters->children[i];

        if (!isNode(parameter, "FuncFParam"))
        {
            continue;
        }

        const SyntaxNode *typeNode = findChild(parameter, "BType");
        DataType baseType = getBaseType(typeNode);

        types[index] = getDefinitionType(parameter, baseType);
        index++;
    }

    function->paramTypes = types;
    function->paramCount = count;

    return 1;
}

static void analyzeAssignment(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    const SyntaxNode *left = findChild(statement, "LVal");
    const SyntaxNode *right = findChild(statement, "Exp");
    DataType leftType = analyzeExpression(context, left);
    DataType rightType = analyzeExpression(context, right);

    const SyntaxNode *identifier = findChild(left, "IDENFR");
    const Symbol *symbol = identifier != NULL && identifier->token != NULL
                               ? findSymbol(context->symbols, identifier->token->value)
                               : NULL;

    if (leftType != TYPE_ERROR && symbol != NULL &&
        symbol->kind == SYMBOL_CONST)
    {
        reportNodeError(context, identifier, 'h');
        return;
    }

    if (leftType != TYPE_ERROR && rightType != TYPE_ERROR &&
        (!isScalarType(leftType) || leftType != rightType))
    {
        reportNodeError(context, right, 'e');
    }
}

static void analyzeReturn(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    const SyntaxNode *keyword = findChild(statement, "RETURNTK");
    const SyntaxNode *value = findChild(statement, "Exp");
    DataType actualType = value != NULL
                              ? analyzeExpression(context, value)
                              : TYPE_VOID;

    if (context->currentReturnType == TYPE_VOID)
    {
        // void 函数只允许不带表达式的 return。
        if (value != NULL)
        {
            reportNodeError(context, keyword, 'f');
        }

        return;
    }

    if (actualType != TYPE_ERROR &&
        actualType != context->currentReturnType)
    {
        reportNodeError(context, keyword, 'e');
    }
}

static DataType getFormatType(char specifier)
{
    switch (specifier)
    {
    case 'd':
        return TYPE_INT;
    case 'c':
        return TYPE_CHAR;
    case 's':
        return TYPE_CHAR_ARRAY;
    default:
        return TYPE_ERROR;
    }
}

static void analyzePrintf(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    const SyntaxNode *keyword = findChild(statement, "PRINTFTK");
    const SyntaxNode *string = findChild(statement, "STRCON");

    if (context->memoryError || string == NULL || string->token == NULL)
    {
        return;
    }

    const char *format = string->token->value;
    size_t formatCount = 0;
    size_t argumentCount = 0;

    for (size_t i = 0; format[i] != '\0'; i++)
    {
        if (format[i] == '%' && getFormatType(format[i + 1]) != TYPE_ERROR)
        {
            formatCount++;
            i++;
        }
    }

    for (size_t i = 0; i < statement->childCount; i++)
    {
        if (isNode(statement->children[i], "Exp"))
        {
            argumentCount++;
        }
    }

    int countMismatch = formatCount != argumentCount;

    if (countMismatch)
    {
        reportNodeError(context, keyword, 'l');
    }

    size_t position = 0;
    int reportedMismatch = 0;

    for (size_t i = 0; i < statement->childCount; i++)
    {
        if (context->memoryError)
        {
            return;
        }

        const SyntaxNode *argument = statement->children[i];

        if (!isNode(argument, "Exp"))
        {
            continue;
        }

        // 即使格式数量不匹配，仍然检查实参内部的表达式。
        DataType actualType = analyzeExpression(context, argument);

        if (countMismatch)
        {
            continue;
        }

        DataType expectedType = TYPE_ERROR;

        while (format[position] != '\0')
        {
            if (format[position] == '%')
            {
                expectedType = getFormatType(format[position + 1]);

                if (expectedType != TYPE_ERROR)
                {
                    position += 2;
                    break;
                }
            }

            position++;
        }

        if (actualType != TYPE_ERROR && actualType != expectedType &&
            !reportedMismatch)
        {
            reportNodeError(context, argument, 'e');
            reportedMismatch = 1;
        }
    }
}

static void checkFunctionReturn(
    SemanticContext *context,
    const SyntaxNode *body)
{
    if (context->memoryError || context->currentReturnType == TYPE_VOID)
    {
        return;
    }

    const SyntaxNode *lastItem = NULL;

    for (size_t i = 0; i < body->childCount; i++)
    {
        if (isNode(body->children[i], "BlockItem"))
        {
            lastItem = body->children[i];
        }
    }

    const SyntaxNode *lastStatement = findChild(lastItem, "Stmt");

    if (findChild(lastStatement, "RETURNTK") == NULL)
    {
        reportNodeError(context, findChild(body, "RBRACE"), 'g');
    }
}

static void analyzeCondition(
    SemanticContext *context,
    const SyntaxNode *condition)
{
    DataType type = analyzeExpression(context, condition);

    if (type != TYPE_ERROR && !isScalarType(type))
    {
        reportNodeError(context, condition, 'e');
    }
}

static void analyzeWhile(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    analyzeCondition(context, findChild(statement, "Cond"));

    context->loopDepth++;
    analyzeNode(context, findChild(statement, "Stmt"));
    context->loopDepth--;
}

static void analyzeIf(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    analyzeCondition(context, findChild(statement, "Cond"));

    for (size_t i = 0; i < statement->childCount; i++)
    {
        if (isNode(statement->children[i], "Stmt"))
        {
            analyzeNode(context, statement->children[i]);
        }
    }
}

static void analyzeJump(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    const SyntaxNode *keyword = findChild(statement, "BREAKTK");

    if (keyword != NULL)
    {
        if (context->loopDepth == 0 && context->switchDepth == 0)
        {
            reportNodeError(context, keyword, 'm');
        }
    }
    else if (context->loopDepth == 0)
    {
        reportNodeError(context, findChild(statement, "CONTINUETK"), 'm');
    }
}

static long long getNumberValue(const SyntaxNode *number)
{
    const SyntaxNode *integer = findChild(number, "INTCON");

    if (integer != NULL)
    {
        return strtoll(integer->token->value, NULL, 10);
    }

    const SyntaxNode *character = findChild(number, "CHARCON");
    const char *text = character->token->value;

    if (text[1] != '\\')
    {
        return (unsigned char)text[1];
    }

    switch (text[2])
    {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case '0':
        return 0;
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    default:
        return (unsigned char)text[2];
    }
}

typedef struct
{
    DataType type;
    long long value;
} CaseValue;

static void analyzeSwitch(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    const SyntaxNode *selector = findChild(statement, "Exp");
    DataType selectorType = analyzeExpression(context, selector);

    if (selectorType != TYPE_ERROR && !isScalarType(selectorType))
    {
        reportNodeError(context, selector, 'e');
        selectorType = TYPE_ERROR;
    }

    if (context->memoryError)
    {
        return;
    }

    size_t caseCount = 0;

    for (size_t i = 0; i < statement->childCount; i++)
    {
        if (isNode(statement->children[i], "CaseStmt"))
        {
            caseCount++;
        }
    }

    CaseValue *seen = caseCount != 0
                          ? malloc(caseCount * sizeof(CaseValue))
                          : NULL;

    if (caseCount != 0 && seen == NULL)
    {
        context->memoryError = 1;
        return;
    }

    size_t seenCount = 0;
    int seenDefault = 0;
    context->switchDepth++;

    for (size_t i = 0; i < statement->childCount; i++)
    {
        if (context->memoryError)
        {
            break;
        }

        const SyntaxNode *branch = statement->children[i];

        if (!isNode(branch, "CaseStmt"))
        {
            continue;
        }

        const SyntaxNode *keyword = findChild(branch, "DEFAULTTK");

        if (keyword != NULL)
        {
            if (seenDefault)
            {
                reportNodeError(context, keyword, 'n');
            }

            seenDefault = 1;
        }
        else
        {
            const SyntaxNode *number = findChild(branch, "Number");
            DataType labelType = getNumberType(number);

            if (selectorType != TYPE_ERROR && labelType != selectorType)
            {
                reportNodeError(context, number, 'e');
            }
            else if (labelType != TYPE_ERROR)
            {
                long long value = getNumberValue(number);
                int duplicate = 0;

                for (size_t j = 0; j < seenCount; j++)
                {
                    if (seen[j].type == labelType && seen[j].value == value)
                    {
                        duplicate = 1;
                        break;
                    }
                }

                if (duplicate)
                {
                    reportNodeError(context, findChild(branch, "CASETK"), 'n');
                }
                else
                {
                    seen[seenCount].type = labelType;
                    seen[seenCount].value = value;
                    seenCount++;
                }
            }
        }

        // 每个 switch 独立保存标签；标签出错后仍分析分支中的语句。
        for (size_t j = 0; j < branch->childCount; j++)
        {
            if (isNode(branch->children[j], "Stmt"))
            {
                analyzeNode(context, branch->children[j]);
            }
        }
    }

    context->switchDepth--;
    free(seen);
}

static void analyzeFunction(
    SemanticContext *context,
    const SyntaxNode *node)
{
    if (context->memoryError || node == NULL)
    {
        return;
    }

    int isMain = isNode(node, "MainFuncDef");

    if (!isMain && !isNode(node, "FuncDef"))
    {
        return;
    }

    const SyntaxNode *body = findChild(node, "Block");
    const SyntaxNode *parameters = findChild(node, "FuncFParams");

    if (body == NULL)
    {
        return;
    }

    DataType returnType = isMain
                              ? TYPE_INT
                              : getBaseType(findChild(node, "FuncType"));

    if (returnType == TYPE_ERROR)
    {
        return;
    }

    // main 是关键字，不加入符号表。
    if (!isMain)
    {
        const SyntaxNode *identifier = findChild(node, "IDENFR");

        if (identifier == NULL || identifier->token == NULL)
        {
            return;
        }

        Symbol function = {0};
        function.name = identifier->token->value;
        function.type = returnType;
        function.kind = SYMBOL_FUNC;
        function.line = identifier->token->line;

        if (!collectParameterTypes(context, parameters, &function))
        {
            return;
        }

        // 此时仍在全局作用域，先登记函数名。
        int result = addSymbol(context->symbols, &function);

        // addSymbol 已复制参数类型，释放临时数组。
        free(function.paramTypes);

        if (result == 0)
        {
            if (!addError(context->errors, function.line, 'b'))
            {
                context->memoryError = 1;
            }
        }
        else if (result < 0)
        {
            context->memoryError = 1;
        }

        if (context->memoryError)
        {
            return;
        }
    }

    // 为函数形参和函数体最外层的局部变量创建同一个作用域。
    if (enterScope(context->symbols) == 0)
    {
        context->memoryError = 1;
        return;
    }

    DataType previousReturnType = context->currentReturnType;
    int previousLoopDepth = context->loopDepth;
    int previousSwitchDepth = context->switchDepth;

    context->currentReturnType = returnType;
    context->loopDepth = 0;
    context->switchDepth = 0;

    registerParameters(context, parameters);
    analyzeBlock(context, body, 0);
    checkFunctionReturn(context, body);

    leaveScope(context->symbols);

    context->currentReturnType = previousReturnType;
    context->loopDepth = previousLoopDepth;
    context->switchDepth = previousSwitchDepth;
}

static void analyzeNode(SemanticContext *context,const SyntaxNode *node){
    if(context->memoryError || node == NULL){
        return;
    }

    if(node->kind == NODE_TERMINAL){
        return;
    }

    if(isNode(node,"ConstDecl")||isNode(node,"VarDecl")){
        analyzeDeclaration(context, node);
        return;
    }

    if (isNode(node, "FuncDef") || isNode(node, "MainFuncDef"))
    {
        analyzeFunction(context, node);
        return;
    }

    if(isNode(node,"Block")){
        analyzeBlock(context, node, 1);
        return;
    }

    if (isNode(node, "Stmt") && findChild(node, "WHILETK") != NULL)
    {
        analyzeWhile(context, node);
        return;
    }

    if (isNode(node, "Stmt") && findChild(node, "IFTK") != NULL)
    {
        analyzeIf(context, node);
        return;
    }

    if (isNode(node, "Stmt") && findChild(node, "SWITCHTK") != NULL)
    {
        analyzeSwitch(context, node);
        return;
    }

    if (isNode(node, "Stmt") &&
        (findChild(node, "BREAKTK") != NULL ||
         findChild(node, "CONTINUETK") != NULL))
    {
        analyzeJump(context, node);
        return;
    }

    if (isNode(node, "Stmt") && findChild(node, "PRINTFTK") != NULL)
    {
        analyzePrintf(context, node);
        return;
    }

    if (isNode(node, "Stmt") && findChild(node, "ASSIGN") != NULL)
    {
        analyzeAssignment(context, node);
        return;
    }

    if (isNode(node, "Stmt") && findChild(node, "RETURNTK") != NULL)
    {
        analyzeReturn(context, node);
        return;
    }

    if (isNode(node, "Exp") ||
        isNode(node, "ConstExp") ||
        isNode(node, "Cond") ||
        isNode(node, "LVal") ||
        isNode(node, "Number"))
    {
        analyzeExpression(context, node);
        return;
    }

    //展开Decl、BlockItem等节点，继续寻找需要处理的节点
    for (size_t i = 0; i < node->childCount;i++){
        if(context->memoryError){
            return;
        }

        analyzeNode(context, node->children[i]);
    }
}

static void registerBuiltin(
    SemanticContext *context,
    char *name,
    DataType returnType,
    DataType *paramTypes,
    size_t paramCount)
{
    if (context->memoryError)
    {
        return;
    }

    Symbol function = {0};
    function.name = name;
    function.type = returnType;
    function.kind = SYMBOL_FUNC;
    function.paramTypes = paramTypes;
    function.paramCount = paramCount;
    function.isBuiltin = 1;

    // 此函数只在空的全局作用域初始化阶段调用。
    if (addSymbol(context->symbols, &function) != 1)
    {
        context->memoryError = 1;
    }
}

static void registerBuiltins(SemanticContext *context)
{
    DataType intParam[] = {TYPE_INT};
    DataType charParam[] = {TYPE_CHAR};
    DataType stringParam[] = {TYPE_CHAR_ARRAY};
    DataType getStringParams[] = {TYPE_CHAR_ARRAY, TYPE_INT};

    registerBuiltin(
        context, "get_int", TYPE_INT, NULL, 0);

    registerBuiltin(
        context, "get_char", TYPE_CHAR, NULL, 0);

    registerBuiltin(
        context, "get_string", TYPE_VOID, getStringParams, 2);

    registerBuiltin(
        context, "put_int", TYPE_VOID, intParam, 1);

    registerBuiltin(
        context, "put_char", TYPE_VOID, charParam, 1);

    registerBuiltin(
        context, "put_str", TYPE_VOID, stringParam, 1);

    // 文法的名称栏使用 put_string，函数原型使用 put_str。
    registerBuiltin(
        context, "put_string", TYPE_VOID, stringParam, 1);
}

int analyzeSemantics(
    SemanticContext *context,
    const SyntaxNode *root)
{
    if (context->memoryError || !isNode(root, "CompUnit"))
    {
        return 0;
    }

    // 第一次进入作用域，创建编号为 1 的全局作用域。
    if (enterScope(context->symbols) == 0)
    {
        context->memoryError = 1;
        return 0;
    }

    registerBuiltins(context);
    analyzeNode(context, root);

    leaveScope(context->symbols);

    return !context->memoryError;
}
