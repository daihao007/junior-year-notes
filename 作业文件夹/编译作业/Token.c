#include <stdio.h>
#include <ctype.h>
#include <string.h>

// Token类型
typedef enum
{
    IDSY,     // 标识符
    INTSY,    // 整数
    PLUSSY,   // +
    STARSY,   // *
    COMMASY,  // ,
    LPARSY,   // (
    RPARSY,   // )
    COLONSY,  // :
    ASSIGNSY, // :=
    ERROR
} TokenType;

typedef struct
{
    TokenType type;
    char value[100];
} Token;

char input[1000];
int pos = 0;

// 当前字符
char ch;

void GETCHAR()
{
    ch = input[pos++];
}

// 跳过空白字符
void GETNBC()
{
    while (ch == ' ' || ch == '\n' || ch == '\t')
    {
        GETCHAR();
    }
}

// 回退一个字符
void UNGETCH()
{
    pos--;
}

// 添加字符到TOKEN
void CAT(char *token)
{
    int len = strlen(token);

    token[len] = ch;
    token[len + 1] = '\0';
}

// 判断是否关键字
TokenType RESERVE(char *token)
{
    if (strcmp(token, "if") == 0)
        return IDSY;

    if (strcmp(token, "while") == 0)
        return IDSY;

    if (strcmp(token, "int") == 0)
        return IDSY;

    return ERROR;
}

// 返回Token
Token makeToken(TokenType type, char *value)
{
    Token t;

    t.type = type;

    if (value)
        strcpy(t.value, value);
    else
        t.value[0] = '\0';

    return t;
}

// 词法分析核心函数
Token scan()
{
    char token[100] = "";

    do{
        GETCHAR();
    } while (ch == ' ' || ch == '\n' || ch == '\t');

    switch (ch)
    {

    // 字母开头
    case 'A' ... 'Z':
    case 'a' ... 'z':

        while (isalpha(ch) || isdigit(ch))
        {
            CAT(token);
            GETCHAR();
        }

        UNGETCH();

        if (RESERVE(token) != ERROR)
        {
            return makeToken(IDSY, token);
        }
        else
        {
            return makeToken(IDSY, token);
        }

    // 数字
    case '0' ... '9':

        while (isdigit(ch))
        {
            CAT(token);
            GETCHAR();
        }

        UNGETCH();

        return makeToken(INTSY, token);

    // 单字符符号
    case '+':

        return makeToken(PLUSSY, "+");

    case '*':

        return makeToken(STARSY, "*");

    case ',':

        return makeToken(COMMASY, ",");

    case '(':

        return makeToken(LPARSY, "(");

    case ')':

        return makeToken(RPARSY, ")");

    // 冒号
    case ':':

        GETCHAR();

        if (ch == '=')
        {
            return makeToken(ASSIGNSY, ":=");
        }

        UNGETCH();

        return makeToken(COLONSY, ":");

    default:

        return makeToken(ERROR, "error");
    }
}

// 输出Token
void printToken(Token t)
{

    printf("<");

    switch (t.type)
    {

    case IDSY:
        printf("ID");
        break;

    case INTSY:
        printf("INT");
        break;

    case PLUSSY:
        printf("PLUS");
        break;

    case STARSY:
        printf("STAR");
        break;

    case ASSIGNSY:
        printf("ASSIGN");
        break;

    case ERROR:
        printf("ERROR");
        break;

    default:
        printf("OTHER");
    }

    printf(", %s>\n", t.value);
}

int main()
{

    strcpy(input, "int a:=10+20");

    while (pos < strlen(input))
    {
        Token t = scan();

        printToken(t);
    }

    return 0;
}