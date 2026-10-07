#include<stdio.h>
#include<ctype.h>
#include<string.h>

const char *getWordType(const char *word){
    const struct {
        const char *text;
        const char *type;
    } keywords[] = {
        {"const", "CONSTTK"},
        {"int", "INTTK"},
        {"char", "CHARTK"},
        {"static", "STATICTK"},
        {"main", "MAINTK"},
        {"void", "VOIDTK"},
        {"if", "IFTK"},
        {"else", "ELSETK"},
        {"while", "WHILETK"},
        {"switch", "SWITCHTK"},
        {"case", "CASETK"},
        {"default", "DEFAULTTK"},
        {"break", "BREAKTK"},
        {"continue", "CONTINUETK"},
        {"return", "RETURNTK"},
        {"printf", "PRINTFTK"}
    };

    size_t count = sizeof(keywords) / sizeof(keywords[0]);

    for (size_t i = 0; i < count;i++){
        if(strcmp(word,keywords[i].text) == 0){
            return keywords[i].type;
        }
    }
    return "IDENFR";
}

const char *getSingleCharType(int ch)
{
    switch (ch)
    {
    case '+':
        return "PLUS";
    case '-':
        return "MINU";
    case '*':
        return "MULT";
    case '/':
        return "DIV";
    case '%':
        return "MOD";
    case '!':
        return "NOT";
    case '=':
        return "ASSIGN";
    case '<':
        return "LSS";
    case '>':
        return "GRE";
    case ';':
        return "SEMICN";
    case ',':
        return "COMMA";
    case ':':
        return "COLON";
    case '(':
        return "LPARENT";
    case ')':
        return "RPARENT";
    case '[':
        return "LBRACK";
    case ']':
        return "RBRACK";
    case '{':
        return "LBRACE";
    case '}':
        return "RBRACE";
    default:
        return NULL;
    }
}

//处理双字符，如：==
const char *getDoubleCharType(int first, int second)
{
    if (second == '=')
    {
        switch (first)
        {
        case '=':
            return "EQL";
        case '!':
            return "NEQ";
        case '<':
            return "LEQ";
        case '>':
            return "GEQ";
        }
    }

    if (first == '&' && second == '&')
    {
        return "AND";
    }

    if (first == '|' && second == '|')
    {
        return "OR";
    }

    return NULL;
}

int main(){
    FILE *source = fopen("testfile.txt", "r");
    if(source == NULL){
        perror("无法打开 testfile.txt");
        return 1;
    }

    FILE *lexer = fopen("lexer.txt", "w");

    if (lexer == NULL)
    {
        perror("无法打开 lexer.txt");
        fclose(source);
        return 1;
    }

    FILE *error = fopen("error.txt", "w");

    if (error == NULL)
    {
        perror("无法打开 error.txt");
        fclose(lexer);
        fclose(source);
        return 1;
    }

    int ch;
    int line = 1;
    while((ch = fgetc(source)) != EOF){
        //putchar(ch);
        if(ch == '\n'){
            line++;
            continue;
        }

        if(isspace(ch)){
            continue;
        }

        //转义字符
        if (ch == '\'')
        {
            char word[8];
            size_t length = 0;

            word[length++] = (char)ch;

            int next = fgetc(source);

            if (next == '\\')
            {
                word[length++] = (char)next;
                next = fgetc(source);
            }

            if (next == EOF)
            {
                fprintf(stderr, "Incomplete character constant\n");
                fclose(source);
                return 1;
            }

            word[length++] = (char)next;

            int closing = fgetc(source);

            if (closing != '\'')
            {
                fprintf(stderr, "Missing closing single quote\n");
                fclose(source);
                return 1;
            }

            word[length++] = (char)closing;
            word[length] = '\0';

            fprintf(lexer, "CHARCON %s\n", word);
            continue;
        }

        //字符串常量处理
        if (ch == '"')
        {
            char text[4096];
            size_t length = 0;

            text[length++] = (char)ch;

            int next;

            while ((next = fgetc(source)) != EOF)
            {
                if (length >= sizeof(text) - 1)
                {
                    fprintf(stderr, "String constant is too long\n");
                    fclose(source);
                    return 1;
                }

                text[length++] = (char)next;

                if (next == '"')
                {
                    break;
                }
            }

            if (next == EOF)
            {
                fprintf(stderr, "Missing closing double quote\n");
                fclose(source);
                return 1;
            }

            text[length] = '\0';

            fprintf(lexer, "STRCON %s\n", text);
            continue;
        }

        //标识符识别
        if(isalpha(ch) || ch == '_'){
            char word[1024];
            size_t length = 0;
            do{
                if(length >= sizeof(word) - 1){
                    fprintf(stderr, "Identifier is too long\n");
                    return 1;
                }

                word[length++] = (char)ch;
                ch = fgetc(source);
            } while (isalnum(ch) || ch == '_');

            word[length] = '\0';

            if(ch != EOF){
                ungetc(ch, source);
            }

            fprintf(lexer, "%s %s\n", getWordType(word), word);
            continue;
        }

        if(isdigit(ch)){
            char number[1024];
            size_t length = 0;

            do{
                if(length>= sizeof(number) - 1){
                    fprintf(stderr, "Integer constant is too long\n");
                    fclose(source);
                    return 1;
                }

                number[length++] = (char)ch;
                ch = fgetc(source);
            } while (isdigit(ch));

            number[length] = '\0';

            if(ch != EOF){
                ungetc(ch, source);
            }

            fprintf(lexer, "INTCON %s\n", number);
            continue;
        }

        //处理注释
        if (ch == '/')
        {
            int next = fgetc(source);

            if (next == '/')
            {
                while ((next = fgetc(source)) != EOF && next != '\n')
                {
                    /* 跳过注释内容 */
                }

                if (next == '\n')
                {
                    line++;
                }

                continue;
            }

            if (next == '*')
            {
                int previous = 0;

                while ((next = fgetc(source)) != EOF)
                {
                    if (next == '\n')
                    {
                        line++;
                    }

                    if (previous == '*' && next == '/')
                    {
                        break;
                    }

                    previous = next;
                }

                continue;
            }

            if (next != EOF)
            {
                ungetc(next, source);
            }
        }

        if (ch == '=' || ch == '!' || ch == '<' ||
            ch == '>' || ch == '&' || ch == '|')
        {

            int next = fgetc(source);
            const char *doubleType = getDoubleCharType(ch, next);

            if (doubleType != NULL)
            {
                fprintf(lexer, "%s %c%c\n", doubleType, ch, next);
                continue;
            }

            if (next != EOF)
            {
                ungetc(next, source);
            }
        }

        //处理单独的&,|
        if (ch == '&' || ch == '|')
        {
            fprintf(error, "%d a\n", line);

            if (ch == '&')
            {
                fprintf(lexer, "AND &\n");
            }
            else
            {
                fprintf(lexer, "OR |\n");
            }

            continue;
        }

        const char *type = getSingleCharType(ch);

        if(type != NULL){
            fprintf(lexer,"%s %c\n", type, ch);
        }else{
            fprintf(stderr, "Unexpected character at line %d: %c\n", line, ch);
        }
    }

    fclose(error);
    fclose(lexer);
    fclose(source);
    return 0;
}