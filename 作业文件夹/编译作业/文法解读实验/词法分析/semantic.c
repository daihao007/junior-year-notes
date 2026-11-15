// 基于 parser.c 生成的语法树进行语义分析；符号与作用域管理实现在 symbol.c。
// TYPE_ERROR 表示类型推导失败，memoryError 表示分析器内部无法继续，两者不能混用。
#include "semantic.h"
#include <string.h>
#include<stdlib.h>
static void analyzeNode(SemanticContext *context, const SyntaxNode *node);
static DataType analyzeExpression(
    SemanticContext *context,
    const SyntaxNode *node);

// 绑定已有符号表、错误表，初始化当前函数返回类型和两种嵌套深度；不创建作用域。
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

// 比较节点 name（如 Stmt、IDENFR），不是比较变量的原始名字；NULL 返回不匹配。
static int isNode(const SyntaxNode *node, const char *name)
{
    return node != NULL && strcmp(node->name, name) == 0;
}

// 只找第一个同名的直接孩子，不递归找后代；多条 Stmt/Exp 必须另行遍历。
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

// 将给定行号和类别加入统一 ErrorList；仅记录分配失败才置 memoryError。
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

// 取标识符 token 的 value，按当前作用域及父链查符号；未定义在名字处报 c。
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

// 从声明的 BType 或函数的 FuncType 读取 int/char/void，不根据初值猜测类型。
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

//区分数组和标量
// 结合基本类型和定义节点是否直接含 [，得到标量或一维数组类型。
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

// Number 的直接 INTCON/CHARCON 决定 int/char；本函数不读取数值。
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

// 判断是否为 int 或 char；数组整体、void、TYPE_ERROR 都不是标量。
static int isScalarType(DataType type)
{
    return type == TYPE_INT || type == TYPE_CHAR;
}

// 检查两侧是否为同类型标量；算术保留类型，比较和逻辑运算返回 int。
// 子树已为 TYPE_ERROR 时传播错误类型，避免再次报由同一原因引起的 e。
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

// 按孩子顺序递归取得子树第一个真实 Token，为非终结符提供报错行号。
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

//根据节点定位行号记录错误
// 以传入节点的首 token 行号报告错误；调用者决定传表达式、关键字或右花括号。
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

//检查变量或数组访问，并给出左值表达式类型
// 检查变量/数组引用：查到名字后，无下标返回符号类型，有下标则检查数组与 int 下标。
// indexType 是调用者分析方括号内表达式所得类型，不是下标的整数值。
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
    // 没有下标时，a 可为标量，arr 可为整个数组；直接返回各自在符号表中的类型。
    // 调用者无下标时传来的 indexType=TYPE_ERROR 只是占位值，本分支不会使用它。
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

    // 作业下标要求 int，不隐式把 char 提升为 int；这里只检查类型，不检查下标范围。
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

//将实参类型列表与查到的函数签名匹配
// 根据函数名查签名：先确认是函数，再比较实参数量 d 和按位置的类型 e。
// 签名匹配成功后返回函数声明的返回类型，不在本函数内重新遍历实参。
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
    //找到函数名
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

// 统计并分析 FuncRParams 的直接 Exp，收集有序实参类型，再交给 getCallType。
// 临时类型数组由本函数分配并释放；实参内部错误在签名匹配之前就能被发现。
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

//递归计算表达式的类型
// 递归推导表达式 DataType；这是类型分析，不是执行或通用常量求值。
// Number/LVal/调用由专门函数处理，二元节点先分析两侧，包装层透传孩子类型。
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
        // LVal 的直接 Exp 才是方括号内下标，例如 arr[i+1] 中的 i+1。
        const SyntaxNode *index = findChild(node, "Exp");
        DataType indexType = TYPE_ERROR;

        if (index != NULL)
        {
            indexType = analyzeExpression(context, index);
        }

        // 先递归推导下标类型，再作为第三个参数传入；是否数组由符号表确认。
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

        // 二元树的三个直接孩子为：左子表达式、运算符、右子表达式；不是三个值。
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

//将一项变量、常量或形参定义登记入符号表
// 登记单个变量、常量或形参；addSymbol 自动填写当前 scopeId 并进行同层查重。
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

//检查数组长度和初始化表达式类型
// 检查数组长度和初值的类型；变量找 InitVal，常量找 ConstInitVal。
// 只检查当前实现覆盖的类型规则，不计算数组容量或初始化元素数量上限。
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

    // 变量 int a=1 的初值节点名为 InitVal；常量 const int a=1 则为 ConstInitVal。
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
    // 例如 int a[2]={1,2}，每项期望 int（baseType），不是 int[]。
    // 无花括号时比较整体 definitionType，故 int a[2]=1 能发现 int[] 与 int 不符。
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

//分析整个ConstDecl或VarDecl
// 从 BType 取得基本类型，区分 const/static/普通变量，再按源顺序处理各项定义。
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

    // 声明的 BType 决定 int/char；VarDef 内的方括号再决定是否为数组，与初值无关。
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
            // 每项先登记再分析初值；前面的声明项可供后项引用，当前不做确定赋值分析。
            registerDefinition(context, child, baseType, kind);
            analyzeDefinitionValues(context, child, baseType);
        }
    }
}

//按顺序分析Block，并根据参数决定是否创建作用域
// createScope=1 时进入新作用域并在结束时退出；0 表示使用调用者已经创建的作用域。
// 函数最外层用 0，保证形参与最外层局部变量同层；普通嵌套 Block 用 1。
static void analyzeBlock(SemanticContext *context,const SyntaxNode *block,int createScope){
    if(context->memoryError || !isNode(block,"Block")){
        return;
    }

    // 进入时新建 Scope 并切换 currentScopeId，后续 addSymbol 会自动记录这个编号。
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

//把每个形参作为函数内部的变量登记
// 将形参名字登记到当前函数作用域，供函数体查找及与局部变量进行同层查重。
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

//提取有序形参类型列表，建立函数签名
// 按 FuncFParam 顺序建立函数签名的类型数组；这里不登记形参名字。
// 数组暂由 function 持有，addSymbol 深复制后由 analyzeFunction 释放临时数组。
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

//检查赋值两侧类型与常量可修改性
// 先分析左右表达式，再检查常量修改 h 与赋值类型 e；数组元素类型由 getLValType 得到。
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

    // 前面已依次分析左侧和右侧，再检查左侧是否为常量。
    // 因此即使下面报告 h 并返回，右侧内部的独立错误也已有机会被记录。
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

//检查单条return语句与当前函数返回类型是否一致
// 检查单条 return：void 函数带表达式报 f，非 void 返回类型不符报 e。
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

//将printf的格式字符映射成期望实参类型
// 把百分号后的 d/c/s 映射为 int/char/char[]，其他字符返回 TYPE_ERROR。
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

//检查printf的占位符数量和对应实参类型
// 先检查占位符与直接 Exp 数量 l，再按格式匹配类型 e；仍分析所有实参内部错误。
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

//检查非void函数是否以直接的顶层return结束
// 非 void 函数的最后一个顶层 BlockItem 必须是直接 return，否则在 } 处报 g。
// 按作业的末条规则检查，不做 if 分支等控制流全路径返回证明。
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

    // 这里只看最后一个顶层成分：if 两支都 return 仍不是直接的顶层 return。
    const SyntaxNode *lastStatement = findChild(lastItem, "Stmt");

    if (findChild(lastStatement, "RETURNTK") == NULL)
    {
        reportNodeError(context, findChild(body, "RBRACE"), 'g');
    }
}

//检查条件表达式是否为标量类型
// 递归取得条件类型，要求是 int/char 标量；不计算真假，也不据此跳过分支。
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

//分析while条件，并维护循环嵌套深度
// 先检查 Cond，再在循环体遍历期间将 loopDepth 加一，离开后减一。
static void analyzeWhile(
    SemanticContext *context,
    const SyntaxNode *statement)
{
    analyzeCondition(context, findChild(statement, "Cond"));

    context->loopDepth++;
    analyzeNode(context, findChild(statement, "Stmt"));
    context->loopDepth--;
}

// 检查 Cond，并遍历全部直接 Stmt，覆盖 then 与可选 else 两个分支。
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

//检查break、continue是否处在允许的上下文
// break 允许在 while 或 switch 中；continue 只允许在 while 中，否则报 m。
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

//把case的Number字面量转换成整数值
// 将 case 的整数字符串按十进制转换，或将字符/转义字符转成数值以供查重。
// 只处理 Number 字面量，不对任意 ConstExp 做常量折叠。
static long long getNumberValue(const SyntaxNode *number)
{
    const SyntaxNode *integer = findChild(number, "INTCON");

    if (integer != NULL)
    {
        // 将 "123" 转成 long long 值 123；NULL 表示不取停止位置，10 表示十进制。
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

// 本次调用独立保存 case 的类型/值及 default 标志，检查 e/n 并继续分析分支语句。
// switch 的花括号不是当前文法的 Block，不自动建作用域；内部真正的 Block 会另行处理。
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

    // 每次调用有自己的标签数组；嵌套 switch 不能与外层共用已出现标签。
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

        // branch 是 CaseStmt，直接孩子除标签/冒号外还有多条 Stmt。
        // 标签重复报 n 后仍遍历 Stmt，例如继续发现分支中的未定义名字 c。
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

//将函数登记、作用域、形参、函数体和返回检查组合起来
// 先全局登记普通函数签名，再建立函数作用域、登记形参、分析函数体和返回规则。
// 先登记可支持自递归；没有预扫描登记全部后定义函数，不能等同于支持任意前向调用。
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

    // 保存共享 context 的旧状态；本函数分析临时改写，退出后恢复调用者环境。
    DataType previousReturnType = context->currentReturnType;
    int previousLoopDepth = context->loopDepth;
    int previousSwitchDepth = context->switchDepth;

    // return 按当前函数声明类型检查；深度归零表示本函数刚开始尚未进入 while/switch。
    context->currentReturnType = returnType;
    context->loopDepth = 0;
    context->switchDepth = 0;

    registerParameters(context, parameters);
    // 前面已 enterScope；传 0 避免重复建层，否则会漏掉形参与外层局部变量同名的 b。
    analyzeBlock(context, body, 0);
    checkFunctionReturn(context, body);

    leaveScope(context->symbols);

    // leaveScope 只恢复符号表作用域，不会恢复返回类型和控制深度，须在这里分别还原。
    context->currentReturnType = previousReturnType;
    context->loopDepth = previousLoopDepth;
    context->switchDepth = previousSwitchDepth;
}

//根据节点类别分派任务，语义树遍历的总调度函数
// 语义遍历的总分派：命中具体声明/函数/语句后由专门函数处理并立即返回。
// Decl、BlockItem 等包装层用末尾通用递归展开，避免专门处理后又重复分析子树。
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

    // 例如 Decl → VarDecl，BlockItem → Stmt：当前只是包装层，就递归进入直接孩子。
    // 专门分支已在上方 return，避免对同一声明重复登记或重复报告错误。
    //展开Decl、BlockItem等节点，继续寻找需要处理的节点
    for (size_t i = 0; i < node->childCount;i++){
        if(context->memoryError){
            return;
        }

        analyzeNode(context, node->children[i]);
    }
}

//登记一个运行库函数签名
// 将一个库函数作为 isBuiltin=1 的函数符号登记；可查找但最终符号输出会过滤。
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

//一次注册当前程序允许调用的运行库函数
// 在遍历用户程序前预置库函数签名；printf 是专用语句，不作为普通函数在此登记。
static void registerBuiltins(SemanticContext *context)
{
    // 这些局部数组只用于构造签名；addSymbol 深复制后，离开此函数不会悬空。
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

// 语义入口：全局作用域 1 → 注册库函数 → 遍历 CompUnit → 退出全局。
// 返回 1 只表示过程完成；源程序是否无错，还要由 main 检查 errors.size。
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

    // 即使 errors 中记录了 b/c/e 等普通错误，遍历仍可完成并返回 1。
    return !context->memoryError;
}
