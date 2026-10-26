#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>

typedef struct {
    const char *type;  //单词类别码
    char *value;       //单词实际内容
    int line;          //所在行号
} Token;

typedef struct {
    Token *data;        //Token数组
    size_t size;       //当前Token数量
    size_t capacity;  //数组容量
} TokenList;

void initTokenList(TokenList *list);

int addToken(TokenList *list,
             const char *type,
             const char *value,
             int line);

void freeTokenList(TokenList *list);

#endif
