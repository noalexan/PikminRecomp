/* Links every translated function; prints 1 if main() resolves through the function table. */
#include <stdio.h>

#include "gen.h"

int main(void) {
    printf("%d\n", ppc_lookup(0x800055c0u) != NULL);
    return 0;
}
