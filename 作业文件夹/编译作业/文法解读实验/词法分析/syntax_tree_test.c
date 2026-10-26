#include <stdio.h>
#include "syntax_tree.h"

int main(void)
{
    Token token = {"INTCON", "2", 1};

    SyntaxNode *root = createNonterminalNode("Number");
    SyntaxNode *child = createTokenNode(&token);

    if (root == NULL || child == NULL ||
        !addChild(root, child))
    {
        freeSyntaxTree(child);
        freeSyntaxTree(root);
        fprintf(stderr, "Failed to build tree\n");
        return 1;
    }

    printf("Parent: %s\n", root->name);
    printf("Children: %lu\n", (unsigned long)root->childCount);

    const Token *saved = root->children[0]->token;
    printf("Child: %s %s %d\n",
           saved->type, saved->value, saved->line);

    if (!writeParserOutput("tree_output_test.txt", root, 1) ||
        !writeParserOutput("tree_output_disabled_test.txt", root, 0))
    {
        fprintf(stderr, "Failed to write tree output\n");
        freeSyntaxTree(root);
        return 1;
    }
    freeSyntaxTree(root);
    root = NULL;
    printf("Tree freed\n");

    return 0;
}