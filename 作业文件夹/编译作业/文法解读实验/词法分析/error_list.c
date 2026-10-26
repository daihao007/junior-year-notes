#include "error_list.h"
#include <stdlib.h>
#include <stdio.h>

void initErrorList(ErrorList *list)
{
    list->data = NULL;
    list->size = 0;
    list->capacity = 0;
}

int addError(ErrorList *list, int line, char code)
{
    // 容量不足时扩容
    if (list->size == list->capacity)
    {
        size_t newCapacity =
            list->capacity == 0 ? 16 : list->capacity * 2;

        CompileError *newData = realloc(
            list->data,
            newCapacity * sizeof(CompileError));

        if (newData == NULL)
        {
            return 0;
        }

        list->data = newData;
        list->capacity = newCapacity;
    }

    // 保存错误记录
    list->data[list->size].line = line;
    list->data[list->size].code = code;
    list->size++;

    return 1;
}

void freeErrorList(ErrorList *list)
{
    free(list->data);

    list->data = NULL;
    list->size = 0;
    list->capacity = 0;
}

void sortErrors(ErrorList *list)
{
    for (size_t i = 1; i < list->size; i++)
    {
        CompileError current = list->data[i];
        size_t j = i;

        // 把行号更大的记录向后移动
        while (j > 0 &&
               list->data[j - 1].line > current.line)
        {
            list->data[j] = list->data[j - 1];
            j--;
        }

        // 放入空出来的位置
        list->data[j] = current;
    }
}

int writeErrorOutput(const char *filename, ErrorList *list)
{
    sortErrors(list);

    FILE *out = fopen(filename, "w");

    if (out == NULL)
    {
        return 0;
    }

    for (size_t i = 0; i < list->size; i++)
    {
        if (fprintf(out, "%d %c\n",
                    list->data[i].line,
                    list->data[i].code) < 0)
        {
            fclose(out);
            return 0;
        }
    }

    return fclose(out) == 0;
}