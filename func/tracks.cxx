#include <gtk/gtk.h>
#include <vector>
#include <stdint.h>
#include "geo.h"
#include "event.h"
#include "drawing.h"

extern std::vector<noeevent> theevents;
extern std::vector< std::vector< std::pair<int, int> > > screentracks[kXorY];
extern int gevi;
extern int pixx, pixy;
extern int active_track;

static std::pair<int, int> trackpoint_to_screen(const trackpoint & tp)
{
  return std::pair<int, int>(
    det_to_screen_x(tp.plane)          + (0.5 + tp.fplane)*pixx/2,
    det_to_screen_y(tp.plane, tp.cell) + (0.5 - tp.fcell )*pixy
  );
}

// Draws one track in the view that 'cr' is attached to (so must
// be passed the correct 'traj') and returns all of the computed screen
// track point positions.  If 'active', draw it highlighted as the active
// track.
static std::vector< std::pair<int, int> >
draw_track_in_one_view(cairo_t * cr,
                       const std::vector<trackpoint> & traj,
                       const bool active)
{
  std::vector< std::pair<int, int> > screenpoints;
  if(traj.size() < 2) return screenpoints;

  for(unsigned int h = 0; h < traj.size(); h++)
    screenpoints.push_back(trackpoint_to_screen(traj[h]));

  if(active) cairo_set_source_rgb(cr, 1, 0, 0);
  else       cairo_set_source_rgb(cr, 0, 0.9, 0.9);

  cairo_set_line_width(cr, active?2.5:1.5);

  /* Do not try to optimize by not drawing track segments that are entirely out
     of the view, because I don't want to do the work, and I suspect the
     performance advantage is small in most cases (but haven't checked). */
  for(unsigned int h = 1; h < screenpoints.size(); h++){
    cairo_move_to(cr, screenpoints[h-1].first, screenpoints[h-1].second);
    cairo_line_to(cr, screenpoints[h].first, screenpoints[h].second);
    cairo_stroke(cr);
  }

  return screenpoints;
}

void draw_tracks(cairo_t ** cr,
__attribute__((unused)) const DRAWPARS * const drawpars)
{
  for(int V = 0; V < kXorY; V++){
    screentracks[V].clear();
    for(unsigned int i = 0; i < theevents[gevi].tracks.size(); i++)
      screentracks[V].push_back(
        draw_track_in_one_view(cr[V], theevents[gevi].tracks[i].traj[V],
                               (int)i == active_track));
  }
}
