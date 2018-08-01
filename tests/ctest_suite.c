/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2018,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ctest_lib.h"

int main (void) {
    ctest_init();
    ctest_test("suite working", 1);
    ctest_fin();
    return 0;
}
