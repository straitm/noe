#include <gtk/gtk.h>
#include <vector>
#include <stdint.h>
#include "event.h"
#include "geo.h"
#include "drawing.h"
#include "vertices.h"

extern std::vector<noeevent> theevents;
extern std::vector<screenvertex_t> screenvertices[kXorY];
extern int gevi;
extern int pixx, pixy;
extern int active_vertex;

static std::pair<int, int> draw_vertex_in_one_view(cairo_t * cr,
                                                   const cppoint & pos,
                                                   const bool active)
{
  std::pair<int, int> screenpoint = cppoint_to_screen(pos);

  if(active) cairo_set_source_rgb(cr, 1.0, 0.5, 0.5);
  else       cairo_set_source_rgb(cr, 0.8, 0.0, 0.8);
  cairo_set_line_width(cr, 1);

  // As with tracks, don't bother checking if we're in view since drawing is
  // fairly cheap.

  const int starsize = 6;

  cairo_move_to(cr, screenpoint.first-starsize+0.5, screenpoint.second-starsize+0.5);
  cairo_line_to(cr, screenpoint.first+starsize+0.5, screenpoint.second+starsize+0.5);
  cairo_stroke(cr);

  cairo_move_to(cr, screenpoint.first+starsize+0.5, screenpoint.second-starsize+0.5);
  cairo_line_to(cr, screenpoint.first-starsize+0.5, screenpoint.second+starsize+0.5);
  cairo_stroke(cr);

  cairo_move_to(cr, screenpoint.first-starsize+0.5, screenpoint.second+0.5);
  cairo_line_to(cr, screenpoint.first+starsize+0.5, screenpoint.second+0.5);
  cairo_stroke(cr);

  cairo_move_to(cr, screenpoint.first+0.5, screenpoint.second-starsize+0.5);
  cairo_line_to(cr, screenpoint.first+0.5, screenpoint.second+starsize+0.5);
  cairo_stroke(cr);

  return screenpoint;
}

void draw_vertices(cairo_t ** cr, const DRAWPARS * const drawpars)
{
  for(int V = 0; V < kXorY; V++){
    if(drawpars->clear) screenvertices[V].clear();
    for(unsigned int i = 0; i < theevents[gevi].vertices.size(); i++){
      vertex & vert = theevents[gevi].vertices[i];
      if((int)i != active_vertex &&
         vert.time >= drawpars->firsttick && vert.time <= drawpars->lasttick){
        screenvertex_t sv;
        sv.pos = draw_vertex_in_one_view(cr[V], vert.pos[V], false);
        sv.i = i;
        screenvertices[V].push_back(sv);
      }
    }

    // Draw the active vertex last so it is on top
    if(active_vertex >= 0){
      vertex & vert = theevents[gevi].vertices[active_vertex];
      if(vert.time >= drawpars->firsttick && vert.time <= drawpars->lasttick){
        screenvertex_t sv;
        sv.pos = draw_vertex_in_one_view(cr[V], vert.pos[V], true);
        sv.i = active_vertex;
        screenvertices[V].push_back(sv);
      }
    }
  }
}
