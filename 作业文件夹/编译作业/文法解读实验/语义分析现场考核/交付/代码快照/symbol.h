#ifndef SYMBOL_H
#define SYMBOL_H
#include<stddef.h>

//数据类型：用于表达式、赋值、传参和返回值的类型检查
typedef enum
{
    TYPE_INT,
    TYPE_CHAR,
    TYPE_INT_ARRAY,
    TYPE_CHAR_ARRAY,
    TYPE_VOID,
    TYPE_ERROR
} DataType;

//符号种类：用于区分变量、常量、静态变量和函数
typedef enum
{
    SYMBOL_VAR,
    SYMBOL_CONST,
    SYMBOL_STATIC_VAR,
    SYMBOL_FUNC
} SymbolKind;

//符号表中的一条记录
typedef struct{
    char *name; //标识符名字
    DataType type; //变量的数据类型，或函数的返回类型
    SymbolKind kind; //普通变量、常量、静态变量或函数
    int scopeId; //所属作用域的编号
    int line; //声明时标识符所在的行号

    DataType *paramTypes; //函数各个形参的数据类型
    size_t paramCount; //函数形参数量

    int isBuiltin; //是否为运行时库函数
} Symbol;

//一个作用域的信息
typedef struct{
    int id;        //当前作用域编号
    int parentId; //外层作用域编号，0表示没有外层
} Scope;

//整个程序的符号表
typedef struct{
    Symbol *symbols;
    size_t symbolCount;
    size_t symbolCapacity;

    Scope *scopes;
    size_t scopeCount;
    size_t scopeCapacity;

    int currentScopeId;//当前正在分析的作用域，0表示尚未进入

} SymbolTable;

void initSymbolTable(SymbolTable *table);

//成功时返回新作用域编号，内存分配失败时返回0
int enterScope(SymbolTable *table);

void leaveScope(SymbolTable *table);

const Symbol *findSymbolInCurrentScope(const SymbolTable *table, const char *name);

const Symbol *findSymbol(const SymbolTable *table, const char *name);

int addSymbol(SymbolTable *table, const Symbol *source);

void freeSymbolTable(SymbolTable *table);

const char *getSymbolTypeName(const Symbol *symbol);

int writeSymbolOutput(const char *filename,const SymbolTable *table,int enabled);
#endif