#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "symbol.h"
#include "syntax_tree.h"
#include "error_list.h"

// 语义分析过程中的状态。
typedef struct
{
    SymbolTable *symbols;
    ErrorList *errors;

    DataType currentReturnType;//当前函数声明的返回类型

    int loopDepth;//当前嵌套多少层while中
    int switchDepth;//当前嵌套多少层switch中

    int memoryError;
} SemanticContext;

void initSemanticContext(
    SemanticContext *context,
    SymbolTable *symbols,
    ErrorList *errors);

int analyzeSemantics(
    SemanticContext *context,
    const SyntaxNode *root);
#endif