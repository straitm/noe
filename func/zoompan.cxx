#include <gtk/gtk.h>
#include <stdint.h>
#include <vector>
#include "geo.h"
#include "event.h"
#include "drawing.h"
#include "zoompan.h"

extern int pixx, pixy;
extern int FDpixy, FDpixx;
extern int NDpixy, NDpixx;
extern bool isfd;

extern std::vector<noeevent> theevents;
extern int gevi;

extern int screenxoffset, screenyoffset_xview, screenyoffset_yview;

static int xonbutton1 = 0, yonbutton1 = 0;
static int newbuttonpush = false;
gboolean mousebuttonpress(__attribute__((unused)) GtkWidget * widg,
                                 GdkEventMotion * gevent,
                                 __attribute__((unused)) gpointer data)
{
  xonbutton1 = gevent->x;
  yonbutton1 = gevent->y;
  newbuttonpush = true;
  return TRUE;
}

void dopanning(const noe_view_t V, GdkEventMotion * gevent)
{
  int * yoffset = V == kX?&screenyoffset_xview:&screenyoffset_yview;

  static int oldx = xonbutton1, oldy = yonbutton1;
  if(newbuttonpush){
    oldx = xonbutton1, oldy = yonbutton1;
    newbuttonpush = false;
  }

  screenxoffset += oldx - gevent->x;
  *yoffset      += oldy - gevent->y;

  oldx = gevent->x, oldy = gevent->y;

  redraw_event(NULL, NULL, NULL);
}

// True if we are zoomed, i.e. not at the full detector view.
static bool zoomed()
{
  return (isfd && pixx != FDpixx) || (!isfd && pixx != NDpixx);
}

gboolean dozooming(GtkWidget * widg, GdkEventScroll * gevent, gpointer data)
{
  const bool up = gevent->direction == GDK_SCROLL_UP;

  const noe_view_t V = (*(bool *)data)?kY:kX;
  int * yoffset       = V == kX?&screenyoffset_xview:&screenyoffset_yview;
  int * other_yoffset = V == kY?&screenyoffset_xview:&screenyoffset_yview;

  const int plane = screen_to_plane_unbounded(V, (int)gevent->x);
  const int cell  = screen_to_cell_unbounded (V, (int)gevent->x, (int)gevent->y);

  // Pixels away from the plane left edge and cell top edge
  const int planepix = (int)gevent->x - det_to_screen_x(plane);
  const int cellpix  = (int)gevent->y - det_to_screen_y(plane, cell);

  // In the view *not* being moused-over, zoom in y around the center of the
  // view.  This assumes the two views have the same size on the screen.
  const int other_cell = screen_to_cell_unbounded(V==kX?kY:kX,
                                     (int)gevent->x, widg->allocation.height/2);

  const int old_pixy = pixy, old_pixx = pixx;

  if(up){
    if(pixy > 10) pixy *= 1.1;
    else pixy++;
  }
  else{
    if(pixy > 10) pixy = std::max(isfd?FDpixy:NDpixy, int(pixy*0.9));
    else          pixy = std::max(isfd?FDpixy:NDpixy, pixy-1);
  }

  if(old_pixy == pixy) return TRUE;

  pixx = pixx_from_pixy(pixy);

  // Pick offsets that keep the center of the cell the pointer is over
  // under the pointer.  Try to keep the same part of the cell under
  // the mouse so that zooming is roughly reversable.
  const int newtoleft = det_to_screen_x(plane)
                        + pixx*(float(planepix)/old_pixx);
  screenxoffset += newtoleft - (int)gevent->x;

  const int newtotop = det_to_screen_y(plane, cell)
                       + pixy*(float(cellpix)/old_pixy);
  *yoffset += newtotop - (int)gevent->y;

  // This is hacky.  There is no plane with the right number in the other
  // view, and the cell stagger confuses the coordinates.
  const int other_newtotop = det_to_screen_y(plane+1, other_cell) + pixy/2;
  *other_yoffset += other_newtotop - widg->allocation.height/2;

  // If we're back at the unzoomed view, clear offsets, even though this
  // violates the "don't move the hit under the mouse pointer" rule.
  // This lets the user find things again if they got lost panning the
  // detector way off the screen and it is just generally satisfying to
  // have the detector snap into place in an orderly way.
  if(!zoomed())
    screenxoffset = screenyoffset_yview = screenyoffset_xview = 0;

  redraw_event(NULL, NULL, NULL);

  return TRUE;
}
