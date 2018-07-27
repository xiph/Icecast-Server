/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2014,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __CTEST_LIB_H__
#define __CTEST_LIB_H__

void ctest_init(void);
void ctest_fin(void);

void ctest_test(const char *desc, int res);
void ctest_diagnostic(const char *line);
void ctest_bail_out(const char *reason);
int  ctest_bailed_out(void);

#endif
