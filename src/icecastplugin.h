/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2012,      Philipp "ph3-der-loewe" Schafft <lion@lion.leolix.org>,
 */

#ifndef __ICECASTPLUGINS_H__
#define __ICECASTPLUGINS_H__

#define ICECASTPH_APPNAME    "Icecast <Xiph.Org Foundation>"
#define ICECASTPH_ABIVERSION "2.3.99.0-ph3-api0"

#define ICECASTPH_CHECK_VERSIONS() ROAR_DL_PLUGIN_CHECK_VERSIONS(ICECASTPH_APPNAME, ICECASTPH_ABIVERSION)

typedef int (*icecastph_func_t)();
typedef icecastph_func_t(*icecastph_getter_t)(const char * func);

#define __icecastph_export_func(func)     (((icecastph_getter_t)para->binargv)(#func))
#define __icecastph_export0(func)         __icecastph_export_func(func)()
#define __icecastph_export1(func,arg)     __icecastph_export_func(func)(arg)
#define __icecastph_exportn(func,arg,...) __icecastph_export_func(func)(arg, __VA_ARGS__)

#define icecastph_exit(arg) __icecastph_export1(exit,arg)
#define icecastph_config_queue_reload() __icecastph_export0(config_queue_reload)

#endif  /* __ICECASTPLUGIN_H__ */
