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
  return std::pair<int, int>(0, 0); // TODO
}

void draw_vertices(cairo_t ** cr, const DRAWPARS * const drawpars)
{
  for(int V = 0; V < kXorY; V++){
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

    // Draw the active track last so it is on top
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
