#include <gtk/gtk.h>
#include <stdint.h>
#include <vector>
#include "event.h"
#include "geo.h"
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

// XXX handle what happens to the other view better. I think it zooms
// centered on the view in x, but centered on the top of the detector in y?
gboolean dozooming(__attribute__((unused)) GtkWidget * widg,
                   GdkEventScroll * gevent, gpointer data)
{
  const bool up = gevent->direction == GDK_SCROLL_UP;

  const noe_view_t V = (*(bool *)data)?kY:kX;
  int * yoffset = V == kX?&screenyoffset_xview:&screenyoffset_yview;
  const int plane = screen_to_plane_unbounded(V, (int)gevent->x);
  const int cell  = screen_to_cell_unbounded (V, (int)gevent->x, (int)gevent->y);

  const int old_pixy = pixy;

  if(up) pixy++;
  else   pixy = std::max(isfd?FDpixy:NDpixy, pixy-1);

  if(old_pixy == pixy) return TRUE;

  pixx = pixx_from_pixy(pixy);

  // Pick offsets that keep the center of the cell the pointer is over
  // under the pointer.  There may be a small shift since we don't check the
  // offset within the cell.
  const int newtoleft = det_to_screen_x(plane) + pixx/2;
  screenxoffset += newtoleft - (int)gevent->x;

  const int newtotop = det_to_screen_y(plane, cell) + pixy/2;
  *yoffset += newtotop - (int)gevent->y;

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
