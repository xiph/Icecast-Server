/* format_mp3.h
**
** mp3 format plugin
**
*/
#ifndef __FORMAT_MP3_H__
#define __FORMAT_MP3_H__

typedef struct {
    char *metadata;
    int metadata_age;
    mutex_t lock;
} mp3_state;

format_plugin_t *format_mp3_get_plugin(void);

#endif  /* __FORMAT_MP3_H__ */
