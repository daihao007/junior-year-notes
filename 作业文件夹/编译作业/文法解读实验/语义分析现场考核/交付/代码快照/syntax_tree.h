#ifndef SYNTAX_TREE_H
#define SYNTAX_TREE_H

#include <stddef.h>
#include "token.h"

typedef enum
{
    NODE_TERMINAL,
    NODE_NONTERMINAL
} NodeKind;

typedef struct SyntaxNode
{
    NodeKind kind;
    const char *name;
    const Token *token;

    struct SyntaxNode **children;
    size_t childCount;
    size_t childCapacity;
} SyntaxNode;

SyntaxNode *createTokenNode(const Token *token);

SyntaxNode *createNonterminalNode(const char *name);

int addChild(SyntaxNode *parent, SyntaxNode *child);

void freeSyntaxTree(SyntaxNode *node);

int writeParserOutput(const char *filename,
                      const SyntaxNode *root,
                      int enabled);
#endif