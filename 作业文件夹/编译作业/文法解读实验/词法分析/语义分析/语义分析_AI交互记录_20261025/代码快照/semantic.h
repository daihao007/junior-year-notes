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

    DataType currentReturnType;

    int loopDepth;
    int switchDepth;

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