#include <stdlib.h>
#include "syntax_tree.h"
#include <stdio.h>
#include <string.h>

SyntaxNode *createTokenNode(const Token *token)
{
    if (token == NULL)
    {
        return NULL;
    }

    SyntaxNode *node = malloc(sizeof(SyntaxNode));
    if (node == NULL)
    {
        return NULL;
    }

    node->kind = NODE_TERMINAL;
    node->name = token->type;
    node->token = token;
    node->children = NULL;
    node->childCount = 0;
    node->childCapacity = 0;

    return node;
}

SyntaxNode *createNonterminalNode(const char *name)
{
    if (name == NULL)
    {
        return NULL;
    }

    SyntaxNode *node = malloc(sizeof(SyntaxNode));
    if (node == NULL)
    {
        return NULL;
    }

    node->kind = NODE_NONTERMINAL;
    node->name = name;
    node->token = NULL;
    node->children = NULL;
    node->childCount = 0;
    node->childCapacity = 0;

    return node;
}

int addChild(SyntaxNode *parent, SyntaxNode *child)
{
    if (parent == NULL || child == NULL ||
        parent == child ||
        parent->kind != NODE_NONTERMINAL)
    {
        return 0;
    }

    if (parent->childCount == parent->childCapacity)
    {
        size_t newCapacity = parent->childCapacity == 0
                                 ? 4
                                 : parent->childCapacity * 2;

        SyntaxNode **newChildren = realloc(
            parent->children,
            newCapacity * sizeof(SyntaxNode *));

        if (newChildren == NULL)
        {
            return 0;
        }

        parent->children = newChildren;
        parent->childCapacity = newCapacity;
    }

    parent->children[parent->childCount] = child;
    parent->childCount++;

    return 1;
}

void freeSyntaxTree(SyntaxNode *node)
{
    if (node == NULL)
    {
        return;
    }

    for (size_t i = 0; i < node->childCount; i++)
    {
        freeSyntaxTree(node->children[i]);
    }

    free(node->children);
    free(node);
}

static int shouldOutputNonterminal(const char *name)
{
    return strcmp(name, "Decl") != 0 &&
           strcmp(name, "BType") != 0 &&
           strcmp(name, "BlockItem") != 0;
}

static int writeNode(FILE *out, const SyntaxNode *node)
{
    if (node == NULL)
    {
        return 0;
    }

    // 终结符输出类别码和原始内容
    if (node->kind == NODE_TERMINAL)
    {
        if (node->token == NULL)
        {
            return 0;
        }

        return fprintf(out, "%s %s\n",
                       node->token->type,
                       node->token->value) >= 0;
    }

    // 非终结符先输出所有子节点
    for (size_t i = 0; i < node->childCount; i++)
    {
        if (!writeNode(out, node->children[i]))
        {
            return 0;
        }
    }

    // 最后输出当前成分名
    if (shouldOutputNonterminal(node->name))
    {
        return fprintf(out, "<%s>\n", node->name) >= 0;
    }

    return 1;
}

int writeParserOutput(const char *filename,
                      const SyntaxNode *root,
                      int enabled)
{
    if (!enabled)
    {
        return 1;
    }

    if (filename == NULL || root == NULL)
    {
        return 0;
    }

    FILE *out = fopen(filename, "w");
    if (out == NULL)
    {
        return 0;
    }

    int success = writeNode(out, root);

    if (fclose(out) != 0)
    {
        success = 0;
    }

    return success;
}