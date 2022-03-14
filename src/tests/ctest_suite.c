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
#include <stdlib.h> /* for EXIT_FAILURE */

#include <igloo/tap.h>

int main (void) {
    igloo_tap_init();
    igloo_tap_exit_on(igloo_TAP_EXIT_ON_FIN, NULL);
    igloo_tap_test("suite working", true);
    igloo_tap_fin();

    return EXIT_FAILURE; // return failure as we should never reach this point!
}
