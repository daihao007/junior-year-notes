#include "symbol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
void initSymbolTable(SymbolTable *table){
    table->symbols = NULL;
    table->symbolCount = 0;
    table->symbolCapacity = 0;

    table->scopes = NULL;
    table->scopeCount = 0;
    table->scopeCapacity = 0;

    table->currentScopeId = 0;
}

int enterScope(SymbolTable *table){
    //数组空间不足时扩容
    if(table->scopeCount == table->scopeCapacity){
        size_t newCapacity = table->scopeCapacity == 0 ? 8 : table->scopeCapacity * 2;
        Scope *newScopes = realloc(table->scopes, newCapacity * sizeof(Scope));

        if(newScopes == NULL){
            return 0;
        }

        table->scopes = newScopes;
        table->scopeCapacity = newCapacity;
    }

    //编号从1开始，数组下标从0开始
    int newId = (int)table->scopeCount + 1;

    Scope *scope = &table->scopes[table->scopeCount];
    scope->id = newId;
    scope->parentId = table->currentScopeId;

    table->scopeCount++;
    table->currentScopeId = newId;

    return newId;
}

void leaveScope(SymbolTable *table){
    if(table->currentScopeId == 0){
        return;
    }

    Scope *scope = &table->scopes[table->currentScopeId - 1];
    table->currentScopeId = scope->parentId;
}

const Symbol *findSymbolInCurrentScope(const SymbolTable *table,const char *name){
    for (size_t i = 0; i < table->symbolCount;i++){
        const Symbol *symbol = &table->symbols[i];

        if(symbol->scopeId == table->currentScopeId && strcmp(symbol->name,name)==0){
            return symbol;
        }
    }

    return NULL;
}

const Symbol *findSymbol(const SymbolTable *table,const char *name){
    int scopeId = table->currentScopeId;

    while(scopeId !=0){
        //先查当前这一层
        for (size_t i = 0; i < table->symbolCount;i++){
            const Symbol *symbol = &table->symbols[i];

            if(symbol->scopeId == scopeId && strcmp(symbol->name,name)==0){
                return symbol;
            }
        }

        //这一层没有，找父作用域
        scopeId = table->scopes[scopeId - 1].parentId;
    }

    return NULL;
}

int addSymbol(SymbolTable *table,const Symbol *source){
    if(table->currentScopeId == 0 ){
        return -1;
    }

    //重定义只检查当前作用域
    if(findSymbolInCurrentScope(table,source->name)!=NULL){
        return 0;
    }

    Symbol newSymbol = *source;
    newSymbol.scopeId = table->currentScopeId;

    //为名字分配独立的内存，并复制字符串
    size_t nameSize = strlen(source->name) + 1;
    newSymbol.name = malloc(nameSize);

    if(newSymbol.name == NULL){
        return -1;
    }

    memcpy(newSymbol.name, source->name, nameSize);

    //为函数形参类型分配独立的内存
    newSymbol.paramTypes = NULL;

    if(source->paramCount > 0){
        size_t paramSize = source->paramCount * sizeof(DataType);
        newSymbol.paramTypes = malloc(paramSize);

        if(newSymbol.paramTypes == NULL){
            free(newSymbol.name);
            return -1;
        }

        memcpy(newSymbol.paramTypes, source->paramTypes, paramSize);
    }

    // 符号数组空间不足时扩容。
    if (table->symbolCount == table->symbolCapacity)
    {
        size_t newCapacity = table->symbolCapacity == 0
                                 ? 16
                                 : table->symbolCapacity * 2;

        Symbol *newSymbols = realloc(
            table->symbols,
            newCapacity * sizeof(Symbol));

        if (newSymbols == NULL)
        {
            free(newSymbol.name);
            free(newSymbol.paramTypes);
            return -1;
        }

        table->symbols = newSymbols;
        table->symbolCapacity = newCapacity;
    }

    table->symbols[table->symbolCount] = newSymbol;
    table->symbolCount++;

    return 1;
}

void freeSymbolTable(SymbolTable *table)
{
    // 先释放每条符号记录独立拥有的内存。
    for (size_t i = 0; i < table->symbolCount; i++)
    {
        free(table->symbols[i].name);
        free(table->symbols[i].paramTypes);
    }

    // 再释放保存记录的数组。
    free(table->symbols);
    free(table->scopes);

    // 将指针和计数恢复为空表状态。
    initSymbolTable(table);
}

const char *getSymbolTypeName(const Symbol *symbol)
{
    switch (symbol->kind)
    {
    case SYMBOL_VAR:
        if (symbol->type == TYPE_INT)
            return "Int";
        if (symbol->type == TYPE_CHAR)
            return "Char";
        if (symbol->type == TYPE_INT_ARRAY)
            return "IntArray";
        if (symbol->type == TYPE_CHAR_ARRAY)
            return "CharArray";
        break;

    case SYMBOL_CONST:
        if (symbol->type == TYPE_INT)
            return "ConstInt";
        if (symbol->type == TYPE_CHAR)
            return "ConstChar";
        if (symbol->type == TYPE_INT_ARRAY)
            return "ConstIntArray";
        if (symbol->type == TYPE_CHAR_ARRAY)
            return "ConstCharArray";
        break;

    case SYMBOL_STATIC_VAR:
        if (symbol->type == TYPE_INT)
            return "StaticInt";
        if (symbol->type == TYPE_CHAR)
            return "StaticChar";
        if (symbol->type == TYPE_INT_ARRAY)
            return "StaticIntArray";
        if (symbol->type == TYPE_CHAR_ARRAY)
            return "StaticCharArray";
        break;

    case SYMBOL_FUNC:
        if (symbol->type == TYPE_VOID)
            return "VoidFunc";
        if (symbol->type == TYPE_INT)
            return "IntFunc";
        if (symbol->type == TYPE_CHAR)
            return "CharFunc";
        break;
    }

    return NULL;
}

int writeSymbolOutput(
    const char *filename,
    const SymbolTable *table,
    int enabled)
{
    if (!enabled)
    {
        return 1;
    }

    FILE *output = fopen(filename, "w");
    if (output == NULL)
    {
        return 0;
    }

    // 作用域按创建顺序保存，编号依次递增。
    for (size_t i = 0; i < table->scopeCount; i++)
    {
        int scopeId = table->scopes[i].id;

        // 符号按插入顺序保存，即声明顺序。
        for (size_t j = 0; j < table->symbolCount; j++)
        {
            const Symbol *symbol = &table->symbols[j];

            if (symbol->scopeId != scopeId || symbol->isBuiltin)
            {
                continue;
            }

            const char *typeName = getSymbolTypeName(symbol);

            if (typeName == NULL)
            {
                fclose(output);
                return 0;
            }

            if (fprintf(output, "%d %s %s\n",
                        symbol->scopeId,
                        symbol->name,
                        typeName) < 0)
            {
                fclose(output);
                return 0;
            }
        }
    }

    return fclose(output) == 0;
}