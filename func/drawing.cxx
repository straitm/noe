#include <gtk/gtk.h>
#include <vector>
#include <stdint.h>
#include "event.h"
#include "drawing.h"
#include "status.h"
#include "geo.h"
#include "hits.h"
#include "tracks.h"

extern std::vector<noeevent> theevents;
extern int gevi;
extern bool isfd;
extern rect screenview[kXorY], screenmu;
extern int first_mucatcher;
extern int nplanes;

GtkWidget * edarea[2] = { NULL }; // X and Y views

// Blank the drawing area and draw the detector bounding boxes
static void draw_background(cairo_t ** cr)
{
  setboxes();
  for(int i = 0; i < kXorY; i++){
    cairo_set_source_rgb(cr[i], 0, 0, 0);
    cairo_paint(cr[i]);

    cairo_set_source_rgb(cr[i], 1, 0, 1);
    cairo_set_line_width(cr[i], 1.0);

    // detector box
    cairo_rectangle(cr[i], 0.5+screenview[i].xmin, 0.5+screenview[i].ymin,
                               screenview[i].xsize,    screenview[i].ysize);
    cairo_stroke(cr[i]);
  }

  // Y-view muon catcher empty box
  if(first_mucatcher < nplanes){
    cairo_rectangle(cr[kY], 0.5+screenmu.xmin, 0.5+screenmu.ymin,
                            screenmu.xsize, screenmu.ysize);
    cairo_stroke(cr[kY]);
  }
}


// Size the drawing areas to the detector sizes at the starting zoom level.
// This would not make sense if called after the user zooms and pans, so it
// is only called on startup.
void request_edarea_size()
{
  for(int i = 0; i < kXorY; i++)
    gtk_widget_set_size_request(edarea[i],
      std::max(screenview[kX].xmax(), screenview[kY].xmax()) + 1,
      std::max(screenview[kX].ymax(), screenview[kY].ymax()) + 1);
}

void draw_event(const DRAWPARS * const drawpars)
{
  set_eventn_status();
  if(theevents.empty()) return;

  if(!isfd && theevents[gevi].fdlike){
    setfd();
    request_edarea_size();
  }

  cairo_t * cr[kXorY];
  for(int i = 0; i < kXorY; i++)
    cairo_push_group(cr[i] = gdk_cairo_create(edarea[i]->window));

  // Do not blank the display in the middle of an animation unless necessary
  if(drawpars->clear) draw_background(cr);

  draw_hits(cr, drawpars, edarea);
  draw_tracks(cr, drawpars);

  set_eventn_status(); // overwrite anything that draw_hits did

  for(int i = 0; i < kXorY; i++){
    cairo_pop_group_to_source(cr[i]);
    cairo_paint(cr[i]);
    cairo_destroy(cr[i]);
  }
}

gboolean redraw_event(__attribute__((unused)) GtkWidget *widg,
                      __attribute__((unused)) GdkEventExpose * ee,
                      __attribute__((unused)) gpointer data)
{
  DRAWPARS drawpars;
  drawpars.firsttick = theevents[gevi].current_mintick;
  drawpars.lasttick  = theevents[gevi].current_maxtick;
  drawpars.clear = true;
  draw_event(&drawpars);

  return FALSE;
}
