/* query.c - name-based lookup over parsed profile data
 *
 * These helpers search the decoded tables by name. Colorant names are held in
 * the colorant table's name storage; the lookup walks the name pointers and
 * compares each against the requested string.
 */
#include "internal.h"
#include <string.h>

int gmt_colorant_index(const gmt_profile *p, const char *name)
{
    unsigned i;

    if (!p || !name || !p->colorants.names)
        return -1;

    for (i = 0; i < p->colorants.count; i++) {
        const char *c = p->colorants.names[i];
        if (c && strcmp(c, name) == 0)
            return (int)i;
    }
    return -1;
}
