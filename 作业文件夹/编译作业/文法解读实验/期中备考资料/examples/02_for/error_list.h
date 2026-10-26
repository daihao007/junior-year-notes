#ifndef ERROR_LIST_H
#define ERROR_LIST_H

#include <stddef.h>

// 一条错误记录
typedef struct
{
    int line;  // 错误行号
    char code; // 错误类别，例如 'a'、'i'、'j'、'k'
} CompileError;

// 动态错误列表
typedef struct
{
    CompileError *data;
    size_t size;
    size_t capacity;
} ErrorList;

void initErrorList(ErrorList *list);
int addError(ErrorList *list, int line, char code);
void freeErrorList(ErrorList *list);
void sortErrors(ErrorList *list);
int writeErrorOutput(const char *filename, ErrorList *list);

#endif