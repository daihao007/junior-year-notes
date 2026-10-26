#include <assert.h>
#include <stdio.h>
#include "symbol.h"
#include <string.h>

int main(void)
{
    SymbolTable table;
    initSymbolTable(&table);

    // 进入全局作用域，声明 int x。
    int scopeId = enterScope(&table);
    assert(scopeId == 1);

    Symbol global = {
        .name = "x",
        .type = TYPE_INT,
        .kind = SYMBOL_VAR,
        .line = 1};

    int result = addSymbol(&table, &global);
    assert(result == 1);

    // 同一作用域再次声明 x，应被拒绝。
    result = addSymbol(&table, &global);
    assert(result == 0);
    assert(table.symbolCount == 1);

    // 进入内层，允许声明 char x。
    scopeId = enterScope(&table);
    assert(scopeId == 2);

    Symbol local = {
        .name = "x",
        .type = TYPE_CHAR,
        .kind = SYMBOL_VAR,
        .line = 2};

    result = addSymbol(&table, &local);
    assert(result == 1);

    const Symbol *found = findSymbol(&table, "x");
    assert(found != NULL);
    assert(found->type == TYPE_CHAR);

    // 再添加一个只属于当前内层的名字。
    local.name = "inside";
    result = addSymbol(&table, &local);
    assert(result == 1);

    // 退出后，x 恢复为全局的 int。
    leaveScope(&table);
    found = findSymbol(&table, "x");
    assert(found != NULL);
    assert(found->type == TYPE_INT);

    // 进入兄弟作用域，编号继续递增。
    scopeId = enterScope(&table);
    assert(scopeId == 3);

    assert(findSymbolInCurrentScope(&table, "x") == NULL);
    assert(findSymbol(&table, "inside") == NULL);

    found = findSymbol(&table, "x");
    assert(found != NULL);
    assert(found->scopeId == 1);

    // 在作用域 3 中添加静态字符数组。
    Symbol letters = {
        .name = "letters",
        .type = TYPE_CHAR_ARRAY,
        .kind = SYMBOL_STATIC_VAR,
        .line = 3};

    result = addSymbol(&table, &letters);
    assert(result == 1);

    // 回到全局作用域，再添加函数。
    leaveScope(&table);
    assert(table.currentScopeId == 1);

    Symbol function = {
        .name = "work",
        .type = TYPE_VOID,
        .kind = SYMBOL_FUNC,
        .line = 4};

    result = addSymbol(&table, &function);
    assert(result == 1);

    // 库函数参与符号表管理，但不应出现在输出中。
    Symbol builtin = {
        .name = "get_int",
        .type = TYPE_INT,
        .kind = SYMBOL_FUNC,
        .isBuiltin = 1};

    result = addSymbol(&table, &builtin);
    assert(result == 1);
    assert(findSymbol(&table, "get_int") != NULL);

    result = writeSymbolOutput("symbol_test_output.txt", &table, 1);
    assert(result == 1);

    // 按题目要求列出预期输出。
    const char *expected[] = {
        "1 x Int\n",
        "1 work VoidFunc\n",
        "2 x Char\n",
        "2 inside Char\n",
        "3 letters StaticCharArray\n"};

    FILE *input = fopen("symbol_test_output.txt", "r");
    assert(input != NULL);

    char buffer[256];
    size_t lineCount = sizeof(expected) / sizeof(expected[0]);

    for (size_t i = 0; i < lineCount; i++)
    {
        char *readResult = fgets(buffer, sizeof(buffer), input);
        assert(readResult != NULL);
        assert(strcmp(buffer, expected[i]) == 0);
    }

    // 预期内容读完后，不应再有多余的输出。
    char *extraLine = fgets(buffer, sizeof(buffer), input);
    assert(extraLine == NULL);
    assert(ferror(input) == 0);

    int closeResult = fclose(input);
    assert(closeResult == 0);

    freeSymbolTable(&table);
    assert(table.symbols == NULL);
    assert(table.scopes == NULL);
    assert(table.symbolCount == 0);
    assert(table.scopeCount == 0);
    assert(table.currentScopeId == 0);

    puts("Symbol table checks passed.");
    return 0;
}