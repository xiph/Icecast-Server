/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2020,      Marvin Scholz <epirat07@gmail.com>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "ctest_lib.h"

#include "../errors.h"

int main (void) {
    int ret = 1;

    ctest_init();

    /* Verify all error IDs have a table entry */
    {
        for (int i = 0; i < ICECAST_ERROR_END_SENTINEL; ++i)
        {
            const icecast_error_t *error = error_get_by_id(i);

            if (error == NULL || error->uuid == NULL) {
                ret = 0;
                break;
            }

            error = error_get_by_uuid(error->uuid);
            if (error == NULL) {
                ret = 0;
                break;
            }
        }
        ctest_test("error table completeness", ret);
    }

    /* Verify invalid ID lookup fails */
    {
        const icecast_error_t *error = error_get_by_id(ICECAST_ERROR_END_SENTINEL + 1);

        ctest_test("invalid id lookup should fail", (error == NULL));
    }

    /* Verify invalid UUID lookup fails */
    {
        const icecast_error_t *error = error_get_by_uuid("c0225973-cd02-4faa-9720-01665ea400a7");

        ctest_test("invalid uuid lookup should fail", (error == NULL));
    }

    ctest_fin();
    return 0;
}
