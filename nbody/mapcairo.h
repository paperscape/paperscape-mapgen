#ifndef _INCLUDED_MAPDRAW_H
#define _INCLUDED_MAPDRAW_H

//typedef struct _cairo cairo_t;

#include <cairo.h>
#include "util/xiwilib.h"
#include "map.h"

void map_env_draw(map_env_t *map_env, cairo_t *cr, int width, int height, vstr_t *info_out);

#endif // _INCLUDED_MAPDRAW_H 
