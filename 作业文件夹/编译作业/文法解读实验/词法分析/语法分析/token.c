#include<stdio.h>
#include "token.h"
#include<string.h>
#include<stdlib.h>

void initTokenList(TokenList *list){
    list->data = NULL;
    list->size = 0;
    list->capacity = 0;
}

int addToken(TokenList *list,const char *type,const char *value,int line){
    //第一步：检查并扩容
    if(list->size == list->capacity){
        size_t newCapacity = list->capacity == 0 ? 16 : list->capacity * 2;

        Token *newData = realloc(list->data, newCapacity * sizeof(Token));

        if(newData == NULL){
            return 0;
        }

        list->data = newData;
        list->capacity = newCapacity;
    }

    //第二步：为字符串申请独立内存
    size_t len = strlen(value) + 1;

    char *copy = malloc(len);

    if(copy == NULL){
        return 0;
    }

    //第三步：复制字符串
    memcpy(copy, value, len);

    //第四步：保存Token信息
    list->data[list->size].type = type;
    list->data[list->size].value = copy;
    list->data[list->size].line = line;

    //第五步：更新Token数量
    list->size++;

    return 1;
}

void freeTokenList(TokenList *list){
    //第一步：释放每个Token的value
    for (size_t i = 0; i < list->size;i++){
        free(list->data[i].value);
    }

    //第二步：释放整个Token数组
    free(list->data);
    
    //恢复到空列表状态
    list->data = NULL;
    list->size = 0;
    list->capacity = 0;
}