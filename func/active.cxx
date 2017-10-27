#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <stdint.h>
#include <vector>
#include "event.h"
#include "absgeo.h"
#include "geo.h"
#include "drawing.h"
#include "tracks.h"
#include "vertices.h"
#include "status.h"

extern std::vector<noeevent> theevents;
extern int gevi;

extern int first_mucatcher, ncells_perplane;

// The positions of all the track points on the screen.  We save this
// separately from the physical tracks so that we can quickly calculate
// which track the user is mousing over.  Same idea for vertices.
std::vector<screentrack_t> screentracks[kXorY];
std::vector<screenvertex_t> screenvertices[kXorY];

extern int active_plane, active_cell, active_track, active_vertex;

// How close the mouse pointer should be to a reconstructed object for
// it to make that object active.
static const int min_pix_to_be_close = 20;

// Given a screen position, return the closest vertex. If the position
// is nowhere near a vertex, can return -1. If there are no vertices,
// returns -1.
static int screen_to_activevertex(const noe_view_t view,
                                  const int x, const int y)
{
  if(theevents[gevi].vertices.empty()) return -1;

  int closesti = -1;
  float mindist = FLT_MAX;
  for(unsigned int i = 0; i < screenvertices[view].size(); i++){
    const float dist = hypot(x-screenvertices[view][i].pos.first,
                             y-screenvertices[view][i].pos.second);
    if(dist < mindist){
      mindist = dist;
      closesti = screenvertices[view][i].i; // index into the full vertex array
    }
  }

  if(mindist < min_pix_to_be_close) return closesti;
  return -1;
}


// Given a screen position, return the closest track.  Preferably the
// definition of "closest" matches what a user would expect.  If the
// position is nowhere near a track, can return -1.  If there are no
// tracks, returns -1.
static int screen_to_activetrack(const noe_view_t view,
                                 const int x, const int y)
{
  if(theevents[gevi].tracks.empty()) return -1;

  int closesti = -1;
  float mindist = FLT_MAX;
  for(unsigned int i = 0; i < screentracks[view].size(); i++){
    const float dist = screen_dist_to_track(x, y, screentracks[view][i].traj);
    if(dist < mindist){
      mindist = dist;
      closesti = screentracks[view][i].i; // index into the full track array
    }
  }

  if(mindist < min_pix_to_be_close) return closesti;
  return -1;
}

static bool visible_hit(const int32_t tdc, const int TDCSTEP)
{
  return tdc <= theevents[gevi].current_maxtick &&
         tdc >= theevents[gevi].current_mintick - (TDCSTEP-1);
}

// Given a screen position, returns the cell number.  If no hit cell is in this
// position, return the number of the closest cell in screen y coordinates,
// i.e. the closest hit cell that is directly above or below the screen position,
// even if the closest one is in another direction.  But if this position is
// outside the detector boxes on the right or left, return -1.
static int screen_to_activecell(noe_view_t view, const int x,
                                const int y, const int TDCSTEP)
{
  const int c = screen_to_cell(view, x, y);
  const int plane = screen_to_plane(view, x);

  if(c < 0) return -1;
  if(c >= ncells_perplane) return -1;
  if(plane >= first_mucatcher && view == kY && c >= 2*ncells_perplane/3) return -1;

  std::vector<hit> & THEhits = theevents[gevi].hits;
  int mindist = 9999, closestcell = -1;
  for(unsigned int i = 0; i < THEhits.size(); i++){
    if(THEhits[i].plane != plane) continue;
    if(!visible_hit(THEhits[i].tdc, TDCSTEP)) continue;
    const int dist = abs(THEhits[i].cell - c);
    if(dist < mindist){
      mindist = dist;
      closestcell = THEhits[i].cell;
    }
  }

  return closestcell;
}

void update_active_indices(const noe_view_t V, const int x, const int y,
                           const int TDCSTEP)
{
  active_plane  = screen_to_plane       (V, x);
  active_cell   = screen_to_activecell  (V, x, y, TDCSTEP);
  active_track  = screen_to_activetrack (V, x, y);
  active_vertex = screen_to_activevertex(V,x, y);
}
