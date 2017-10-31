#include <gtk/gtk.h>
#include <vector>
#include <algorithm>
#include <stdint.h>
#include "event.h"
#include "drawing.h"
#include "geo.h"
#include "status.h"

extern std::vector<noeevent> theevents;
extern int gevi;
extern int pixx, pixy;
extern int active_plane, active_cell;

static bool by_charge(const hit & a, const hit & b)
{
  return a.adc < b.adc;
}

__attribute__((unused)) static bool by_time(const hit & a, const hit & b)
{
  return a.tdc < b.tdc;
}

// Given a hit energy, set red, green and blue to the color we want to display
// for the hit.  If "active" is true, set a brighter color.  This is intended
// for when the user has moused over the cell.
static void colorhit(const int32_t adc, float & red, float & green, float & blue,
                     const bool active)
{
  // Oh so hacky!
  const float graycut = 60;
  const float nextcut = 120;
  if(adc < graycut) red = green = blue = 0.2 + 0.3*adc/graycut;
  else if(adc < nextcut)
    blue  = 0.5 + 0.5*(adc-graycut)/(nextcut-graycut),
    red   = 0.5 - 0.5*(adc-graycut)/(nextcut-graycut),
    green = 0.5 - 0.5*(adc-graycut)/(nextcut-graycut);
  else if(adc < 600) blue  = 1, red = green = 0;
  else if(adc < 800) blue = 1-(adc-600)/200.0,
                     green  = (adc-600)/200.0,
                     red = 0;
  else if(adc < 1200) green = 1, red = blue  = 0;
  else if(adc < 1400) green = 1-(adc-1200)/200.0,
                      red   =   (adc-1200)/200.0,
                      blue = 0;
  else red = 1, green = blue  = 0;

  // Brighten this hit while trying to retain some of its original color, but
  // just make it white if that is what it takes to make a difference.
  if(active){
    const float goal = 1.3;
    const float left = 3 - red - blue - green;
    if(left < goal){
      red = blue = green = 1;
    }
    else{
      red += goal*(1 - red)/left;
      blue += goal*(1 - blue)/left;
      green += goal*(1 - green)/left;
    }
  }
}

// Draw a single hit to the screen, taking into account whether it is the
// "active" hit (i.e. being moused over right now).
void draw_hit(cairo_t * cr, const hit & thishit, GtkWidget ** edarea)
{
  const noe_view_t V = thishit.plane%2 == 1?kX:kY;

  // Get position of upper left corner.  If the zoom carries this hit entirely
  // out of the view in screen y, don't waste cycles displaying it.
  const int screenx = det_to_screen_x(thishit.plane);
  if(screenx+pixx < 0) return;
  if(screenx      > edarea[V]->allocation.width) return;

  const int screeny = det_to_screen_y(thishit.plane, thishit.cell);
  if(screeny+pixy < 0) return;
  if(screeny      > edarea[V]->allocation.height) return;

  float red, green, blue;

  colorhit(thishit.adc, red, green, blue,
           thishit.plane == active_plane && thishit.cell == active_cell);

  cairo_set_source_rgb(cr, red, green, blue);

  // If we're representing cells with a very small number of pixels,
  // draw all the way across to the next plane in the view to be easier
  // to look at.  If cells are visually large, make them closer to the
  // actual size of the scintillator.
  // XXX how about a yexpand to show the scintillator size in y?
  // XXX are tracks correctly aligned with hits in both expanded and unexpanded
  // styles?  (Probably not!)
  const bool xexpand = pixx <= 3;
  const int epixx = xexpand?pixx:scintpix_from_pixx(pixx);

  // This is the only part of drawing an event that takes any time
  // I have measured drawing a line to be twice as fast as drawing a
  // rectangle of width 1, so it is totally worth it to have a special
  // case.  This really helps with drawing big events.
  if(pixy == 1){
    cairo_move_to(cr, screenx,       screeny+0.5);
    cairo_line_to(cr, screenx+epixx, screeny+0.5);
  }
  // This is a smaller gain, but it is definitely faster by about 10%.
  else if(pixy == 2){
    cairo_move_to(cr, screenx,       screeny+0.5);
    cairo_line_to(cr, screenx+epixx, screeny+0.5);
    cairo_move_to(cr, screenx,       screeny+1.5);
    cairo_line_to(cr, screenx+epixx, screeny+1.5);
  }
  else{
    cairo_rectangle(cr, screenx+0.5, screeny+0.5,
                        epixx-1,     pixy-1);
  }
  cairo_stroke(cr);
}


// Draw all the hits in the event that we need to draw, depending on
// whether we are animating or have been exposed, etc.
void draw_hits(cairo_t ** cr, const DRAWPARS * const drawpars, GtkWidget ** edarea)
{
  for(int i = 0; i < kXorY; i++) cairo_set_line_width(cr[i], 1.0);

  std::vector<hit> & THEhits = theevents[gevi].hits;

  std::sort(THEhits.begin(), THEhits.end(), by_charge);

  const int big = 100000;
  const bool bigevent = THEhits.size() > big;

  int ndrawn = 0;
  for(unsigned int i = 0; i < THEhits.size(); i++){
    const hit & thishit = THEhits[i];

    if(thishit.tdc < drawpars->firsttick ||
       thishit.tdc > drawpars->lasttick) continue;

    if(bigevent && (++ndrawn)%big == 0)
      set_eventn_status2progress(ndrawn, THEhits.size());

    draw_hit(cr[thishit.plane%2 == 1?kX:kY], thishit, edarea);
  }
}

