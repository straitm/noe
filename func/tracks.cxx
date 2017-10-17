#include <gtk/gtk.h>
#include <vector>
#include <stdint.h>
#include "event.h"
#include "drawing.h"
#include "geo.h"

extern std::vector<noeevent> theevents;
extern int gevi;
extern int pixx, pixy;

static void draw_trackseg(cairo_t * cr, const trackpoint & point1,
                                        const trackpoint & point2)
{
  if(point1.plane%2 ^ point2.plane%2) return; // shouldn't happen

  const int screenx1 = det_to_screen_x(point1.plane)
                     + (0.5 + point1.fplane)*pixx/2;
  const int screenx2 = det_to_screen_x(point2.plane)
                     + (0.5 + point2.fplane)*pixx/2;

  const int screeny1 = det_to_screen_y(point1.plane, point1.cell)
                     + (0.5 - point1.fcell)*pixy;
  const int screeny2 = det_to_screen_y(point2.plane, point2.cell)
                     + (0.5 - point2.fcell)*pixy;

  /* Do not try to optimize by not drawing track segments that are entirely out
     of the view, because I don't want to do the work, and I suspect the
     performance advantage is small in most cases (but haven't checked). */

  cairo_set_source_rgb(cr, 0, 1, 1);

  cairo_move_to(cr, screenx1, screeny1);
  cairo_line_to(cr, screenx2, screeny2);
  cairo_stroke(cr);
}

static void draw_tracks_in_one_view(cairo_t * cr,
                                   const std::vector<trackpoint> & traj)
{
  if(traj.size() < 2) return;
  for(unsigned int h = 0; h < traj.size()-1; h++)
    draw_trackseg(cr, traj[h], traj[h+1]);
}

void draw_tracks(cairo_t ** cr,
__attribute__((unused)) const DRAWPARS * const drawpars)
{
  for(unsigned int i = 0; i < theevents[gevi].tracks.size(); i++){
    draw_tracks_in_one_view(cr[kX], theevents[gevi].tracks[i].trajx);
    draw_tracks_in_one_view(cr[kY], theevents[gevi].tracks[i].trajy);
  }
}
