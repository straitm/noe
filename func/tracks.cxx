#include <gtk/gtk.h>
#include <vector>
#include <stdint.h>
#include "event.h"
#include "geo.h"
#include "drawing.h"
#include "tracks.h"

extern std::vector<noeevent> theevents;
extern std::vector<screentrack_t> screentracks[kXorY];
extern int gevi;
extern int pixx, pixy;
extern int active_track;

// Draws one track in the view that 'cr' is attached to (so must
// be passed the correct 'traj') and returns all of the computed screen
// track point positions.  If 'active', draw it highlighted as the active
// track.
static std::vector< std::pair<int, int> >
draw_track_in_one_view(cairo_t * cr,
                       const std::vector<cppoint> & traj,
                       const bool active)
{
  std::vector< std::pair<int, int> > screenpoints;
  if(traj.size() < 2) return screenpoints;

  for(unsigned int h = 0; h < traj.size(); h++)
    screenpoints.push_back(cppoint_to_screen(traj[h]));

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

void draw_tracks(cairo_t ** cr, const DRAWPARS * const drawpars)
{
  for(int V = 0; V < kXorY; V++){
    if(drawpars->clear) screentracks[V].clear();

    for(unsigned int i = 0; i < theevents[gevi].tracks.size(); i++){
      track & tr = theevents[gevi].tracks[i];
      if((int)i != active_track &&
         tr.time >= drawpars->firsttick && tr.time <= drawpars->lasttick){
        screentrack_t st;
        st.traj = draw_track_in_one_view(cr[V], tr.traj[V], false);
        st.i = i;
        screentracks[V].push_back(st);
      }
    }

    // Draw the active track last so it is on top
    if(active_track >= 0){
      track & tr = theevents[gevi].tracks[active_track];
      if(tr.time >= drawpars->firsttick && tr.time <= drawpars->lasttick){
        screentrack_t st;
        st.traj = draw_track_in_one_view(cr[V], tr.traj[V], true);
        st.i = active_track;
        screentracks[V].push_back(st);
      }
    }
  }
}
