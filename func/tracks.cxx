#include <gtk/gtk.h>
#include <vector>
#include <stdint.h>
#include "event.h"
#include "drawing.h"
#include "geo.h"

extern std::vector<noeevent> theevents;
extern int gevi;
extern int pixx, pixy;

static std::pair<int, int> trackpoint_to_screen(const trackpoint & tp)
{
  return std::pair<int, int>(
    det_to_screen_x(tp.plane)          + (0.5 + tp.fplane)*pixx/2,
    det_to_screen_y(tp.plane, tp.cell) + (0.5 - tp.fcell )*pixy
  );
}

static void draw_trackseg(cairo_t * cr, const std::pair<int, int> & point1,
                                        const std::pair<int, int> & point2)
{
  /* Do not try to optimize by not drawing track segments that are entirely out
     of the view, because I don't want to do the work, and I suspect the
     performance advantage is small in most cases (but haven't checked). */
  cairo_set_source_rgb(cr, 0, 1, 1);
  cairo_move_to(cr, point1.first, point1.second);
  cairo_line_to(cr, point2.first, point2.second);
  cairo_stroke(cr);
}

static void draw_tracks_in_one_view(cairo_t * cr,
                                   const std::vector<trackpoint> & traj)
{
  if(traj.size() < 2) return;
  std::vector< std::pair<int, int> > screenpoints;
  for(unsigned int h = 0; h < traj.size(); h++)
    screenpoints.push_back(trackpoint_to_screen(traj[h]));

  for(unsigned int h = 0; h < traj.size()-1; h++)
    draw_trackseg(cr, screenpoints[h], screenpoints[h+1]);
}

void draw_tracks(cairo_t ** cr,
__attribute__((unused)) const DRAWPARS * const drawpars)
{
  for(unsigned int i = 0; i < theevents[gevi].tracks.size(); i++){
    draw_tracks_in_one_view(cr[kX], theevents[gevi].tracks[i].trajx);
    draw_tracks_in_one_view(cr[kY], theevents[gevi].tracks[i].trajy);
  }
}
