#include<stdio.h>
#include "token.h"
int main(void)
{
    TokenList tokens = {0};

    addToken(&tokens, "INTTK", "int", 1);
    addToken(&tokens, "IDENFR", "a", 1);
    addToken(&tokens, "ASSIGN", "=", 1);
    addToken(&tokens, "INTCON", "10", 1);

    for (size_t i = 0; i < tokens.size; i++)
    {
        printf("%s %s %d\n",
               tokens.data[i].type,
               tokens.data[i].value,
               tokens.data[i].line);
    }

    freeTokenList(&tokens);
    return 0;
}