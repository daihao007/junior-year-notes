#include <stdio.h>
#include "error_list.h"

int main(void)
{
    ErrorList errors;
    initErrorList(&errors);

    if (!addError(&errors, 4, 'a') ||
        !addError(&errors, 3, 'i') ||
        !addError(&errors, 3, 'k') ||
        !addError(&errors, 1, 'j'))
    {
        fprintf(stderr, "Failed to add error\n");
        freeErrorList(&errors);
        return 1;
    }

    if (!writeErrorOutput("error_sort_test.txt", &errors))
    {
        fprintf(stderr, "Failed to write errors\n");
        freeErrorList(&errors);
        return 1;
    }

    freeErrorList(&errors);
    printf("Error output written successfully\n");

    return 0;
}