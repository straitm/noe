/*
Main file for NOE, the New nOva Event display. This isn't the entry
point (see comments at realmain()), but is where nearly everything
happens.

Author: Matthew Strait
Begun: Sept 2017

======================================================================

Code style:

* This is mostly C with use of STL containers. I'm not a big fan of
  the infinitely complex language that C++ has become.

* There is a fair amount of global state. Mostly it represents what's
  being displayed to the user, which is also global state of a sort.
  You might consider this bad style anyway, but I think it is under
  control and prevents having to build up too much abstraction that
  makes the code difficult to understand.

* Tradition dictates that code be under 80 or perhaps 72 columns. I
  like tradition, plus I like splitting my screen into two equal width
  terminals, which leaves 83 columns after 5 columns are used for line
  numbers. So this code will be mostly within 80 columns and certainly
  within 83.
*/

#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include "event.h"

struct DRAWPARS{
  // The ticks to draw right now, typically just the minimum needed
  // and not the whole range that is visible.
  int32_t firsttick, lasttick;
  bool clear;
};

static const int viewsep = 8; // vertical cell widths between x and y views

// Maximum length of any string being printed to a status bar
static const int MAXSTATUS = 1024;

// Let's see.  I believe both detectors read out in increments of 4 TDC units,
// but the FD is multiplexed whereas the ND isn't, so any given channel at the
// FD can only report every 4 * 2^N TDC units, where I think N = 2.
static int TDCSTEP = 4;

/* The events and the current event index in the vector */
extern std::vector<noeevent> theevents;
static int gevi = 0;

/* GTK objects */
static const int NSTATBOXES = 4;
static GtkWidget * win = NULL;
static GtkWidget * statbox[NSTATBOXES];
static GtkTextBuffer * stattext[NSTATBOXES];
static GtkWidget * edarea = NULL;
static GtkWidget * animate_checkbox = NULL,
                 * cum_ani_checkbox = NULL,
                 * freerun_checkbox = NULL;
static GtkWidget * ueventbut = NULL;
static GtkWidget * ueventbox = NULL;
static GtkWidget * mintickslider = NULL;
static GtkWidget * maxtickslider = NULL;

/* Running flags.  */
static bool ghave_read_all = false;
static bool prefetching = false;
static bool adjusttick_callback_inhibit = false; // XXX ug

static int active_plane = -1, active_cell = -1;

/* Ticky boxes flags */
// Animate must start false, because the first thing that happens is that we
// get two expose events (I don't know why) and I don't want to handle an
// animated expose when we haven't drawn yet at all.
//
// Could eliminate these bools and always consult GTK_TOGGLE_BUTTON::active.
// Would that be better?
static bool animate = false;
static bool cumulative_animation = true;
static bool free_running = false;

static gulong freeruninterval = 0; // ms.  Immediately overwritten.
static gulong animationinterval = 0; // ms.  Immediately overwritten.
static gulong freeruntimeoutid = 0;
static gulong animatetimeoutid = 0;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static const int NDnplanes_perview = 8 * 12 + 11,
                 NDfirst_mucatcher = 8 * 24,
                 NDncells_perplane = 3 * 32;
static const int FDnplanes_perview = 16 * 28,
                 FDfirst_mucatcher = 9999, // i.e. no muon catcher
                 FDncells_perplane = 12 * 32;

// Want the nearest small integer box that has an aspect ratio close to 3.36:1.
// Options: 3:1, 2:7, 3:10, 4:13, 5:17, etc.
static int pixx_from_pixy(const int x)
{
  return int(x*3.36 + 0.5);
}

static const int FDpixy = 1, FDpixx = pixx_from_pixy(FDpixy);
static const int NDpixy = 3, NDpixx = pixx_from_pixy(NDpixy);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static int nplanes_perview = NDnplanes_perview,
           first_mucatcher = NDfirst_mucatcher,
           ncells_perplane = NDncells_perplane;
static int nplanes = 2*nplanes_perview;
static int pixx = NDpixx, pixy = NDpixy;
static int ybox, xboxnomu, yboxnomu, xbox;

// Calculate the size of the bounding boxes for the detector's x and y
// views, plus the muon catcher cutaway.  Resize the window to match.
static void setboxes()
{
  ybox = ncells_perplane*pixy + pixy/2 /* cell stagger */,
  xboxnomu = pixx*(first_mucatcher/2) + pixy/2 /* cell stagger */,

  // muon catcher is 1/3 empty.  Do not include cell stagger here since we want
  // the extra half cells to be inside the active box.
  yboxnomu = (ncells_perplane/3)*pixy,

  xbox = pixx*(nplanes_perview +
               (first_mucatcher < nplanes?
               nplanes_perview - first_mucatcher/2: 0));
  if(edarea != NULL)
    gtk_widget_set_size_request(edarea,
                                xbox + 2 /* border */ + pixx/2 /* plane stagger */,
                                ybox*2 + viewsep*pixy);
}

// We're going to assume the ND until we see a hit that indicates it's FD
static bool isfd = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static void setfd()
{
  isfd = true;

  nplanes_perview = FDnplanes_perview;
  first_mucatcher = FDfirst_mucatcher;
  ncells_perplane = FDncells_perplane;

  nplanes = 2*nplanes_perview;

  pixx = FDpixx;
  pixy = FDpixy;

  setboxes();
}

// This hack allows us to get proper minus signs that look a lot better
// than the hyphens that printf puts out.  I'm sure there's a better
// way to do this.
#define BOTANY_BAY_OH_NO(x) x < 0?"−":"", fabs(x)
#define BOTANY_BAY_OH_INT(x) x < 0?"−":"", abs(x)

// Update the given status bar to the given text
static void set_status(const int boxn, const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  static char buf[MAXSTATUS];
  vsnprintf(buf, MAXSTATUS-1, format, ap);

  gtk_text_buffer_set_text(stattext[boxn], buf, strlen(buf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[boxn]), stattext[boxn]);
  gtk_widget_draw(statbox[boxn], NULL);
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

__attribute__((unused)) static bool by_charge(const hit & a, const hit & b)
{
  return a.adc < b.adc;
}

__attribute__((unused)) static bool by_time(const hit & a, const hit & b)
{
  return a.tdc < b.tdc;
}

// Blank the drawing area and draw the detector bounding boxes
static void draw_background(cairo_t * cr)
{
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  cairo_set_source_rgb(cr, 1, 0, 1);
  cairo_set_line_width(cr, 1.0);

  // X-view box
  cairo_rectangle(cr, 0.5+pixx/2 /* plane stagger */, 0.5, xbox+1, ybox+1);
  cairo_stroke(cr);

  const bool hasmucatch = first_mucatcher < nplanes;

  // In the x view the blank spaces are to the left of the hits, but in
  // the y view, they are to the right, but I don't want the box to include them.
  const int hacky_subtraction_for_y_mucatch = hasmucatch * pixx;

  // Y-view main box
  cairo_rectangle(cr, 0.5, 0.5 + ybox + viewsep*pixy - pixy/2 /* cell stagger */,
                      xbox+1-hacky_subtraction_for_y_mucatch, ybox+1);
  cairo_stroke(cr);

  // Y-view muon catcher empty box
  if(hasmucatch){
    cairo_rectangle(cr, 1.5 + xboxnomu, 0.5 + ybox + viewsep*pixy - pixy/2,
                        xbox-xboxnomu-hacky_subtraction_for_y_mucatch, yboxnomu);
    cairo_stroke(cr);
  }
}

static bool visible_hit(const int32_t tdc)
{
  // XXX the acceptance range of TDCSTEP isn't well integrated
  return tdc <= theevents[gevi].current_maxtick &&
         tdc >= theevents[gevi].current_mintick - (TDCSTEP-1);
}

// Given the plane, returns the left side of the screen position in Cairo
// coordinates.  More precisely, returns half a pixel to the left of the left
// side.
static int det_to_screen_x(const int plane)
{
  const bool xview = plane%2 == 1;
  return 1 + // Don't overdraw the border
    pixx*((plane

         // space out the muon catcher planes
         +(plane > first_mucatcher?plane-first_mucatcher:0))/2)

        // stagger x and y planes
      + xview*pixx/2;
}

// Given the plane and cell, return the top of the screen position in
// Cairo coordinates.  More precisely, returns half a pixel above the top.
static int det_to_screen_y(const int plane, const int cell)
{
  const bool xview = plane%2 == 1;

  // In each view, every other plane is offset by half a cell width
  const bool celldown = !((plane/2)%2 ^ (plane%2));

  // put y view on the bottom
  return pixy*(ncells_perplane*2 + viewsep - cell
          - xview*(ncells_perplane + viewsep)) - (pixy-1)

         // Physical stagger of planes in each view
         + celldown*pixy/2;
}

// Given a screen y position, return true if we are in the x-view, or if we are
// in neither view, return true if we are closer to the x-view.  The empty part
// of the detector above the muon catcher is considered to be part of the
// y-view as though the detector were a rectangle in both views.
static int screen_y_to_xview(const int y)
{
  return y <= 2 /* border */ + ybox + (viewsep*pixy)/2;
}

// Given a screen position, returns the plane number. Any x position within the
// boundaries of hits displayed in a plane, including the right and left
// pixels, will return the same plane number.  In the muon catcher, return the
// nearest plane in the view if the screen position is in dead material.  If
// the screen position is outside the detector boxes to the right or left or
// above or below both views, return -1.  If it is between the two views,
// return the plane for the closer view.
static int screen_to_plane(const int x, const int y)
{
  const bool xview = screen_y_to_xview(y);

  // The number of the first muon catcher plane counting only planes
  // in one view.
  const int halfmucatch = (first_mucatcher)/2 + !xview;

  // Account for the plane stagger and border width.
  int effx;
  if(x-2 >= halfmucatch*pixx) effx = x - 2 - pixx/2;
  else effx = x - 2;

  // Half the plane number, as long as we're not in the muon catcher
  int halfp = xview? (effx-pixx/2)/pixx
                    :(    effx   )/pixx;

  // Fix up the case of being in the muon catcher
  if(halfp > halfmucatch) halfp = halfmucatch +
                                  (halfp - halfmucatch)/2;

  // The plane number, except it might be out of range
  const int p = halfp*2 + xview;

  if(p < xview) return -1;
  if(p >= nplanes) return -1;
  return p;
}

// Given a screen position, returns the cell number.  If no hit cell is in this
// position, return the number of the closest cell in screen y coordinates,
// i.e. the closest hit cell that is directly above or below the screen position,
// even if the closest one is in another direction.  But if this position is
// outside the detector boxes on the right or left, return -1.
static int screen_to_cell(const int x, const int y)
{
  const bool xview = screen_y_to_xview(y);
  const int plane = screen_to_plane(x, y);
  const bool celldown = !((plane/2)%2 ^ (plane%2));
  const int effy = (xview? y
                         : y - ybox - viewsep*pixy + 1)
                   - celldown*(pixy/2) - 2;

  const int c = ncells_perplane - effy/pixy - 1;
  if(c < 0) return -1;
  if(c >= ncells_perplane) return -1;
  if(plane >= first_mucatcher && !xview && c >= 2*ncells_perplane/3) return -1;

  std::vector<hit> & THEhits = theevents[gevi].hits;
  int mindist = 9999, closestcell = -1;
  for(unsigned int i = 0; i < THEhits.size(); i++){
    if(THEhits[i].plane != plane) continue;
    if(!visible_hit(THEhits[i].tdc)) continue;
    const int dist = abs(THEhits[i].cell - c);
    if(dist < mindist){
      mindist = dist;
      closestcell = THEhits[i].cell;
    }
  }

  return closestcell;
}

// Draw a single hit to the screen, taking into account whether it is the
// "active" hit (i.e. being moused over right now).
static void draw_hit(cairo_t * cr, const hit & thishit)
{
  const int screenx = det_to_screen_x(thishit.plane),
            screeny = det_to_screen_y(thishit.plane, thishit.cell);

  float red, green, blue;

  colorhit(thishit.adc, red, green, blue,
           thishit.plane == active_plane && thishit.cell == active_cell);

  cairo_set_source_rgb(cr, red, green, blue);

  // This is the only part of drawing an event that takes any time
  // I have measured drawing a line to be twice as fast as drawing a
  // rectangle of width 1, so it is totally worth it to have a special
  // case.  This really helps with drawing big events.
  if(pixy == 1){
    cairo_move_to(cr, screenx,      screeny+0.5);
    cairo_line_to(cr, screenx+pixx, screeny+0.5);
  }
  // This is a smaller gain, but it is definitely faster by about 10%.
  else if(pixy == 2){
    cairo_move_to(cr, screenx,      screeny+0.5);
    cairo_line_to(cr, screenx+pixx, screeny+0.5);
    cairo_move_to(cr, screenx,      screeny+1.5);
    cairo_line_to(cr, screenx+pixx, screeny+1.5);
  }
  else{
    cairo_rectangle(cr, screenx+0.5, screeny+0.5,
                        pixx-1,      pixy-1);
  }
  cairo_stroke(cr);
}

// Set top status line, which reports on event level information
static void set_eventn_status0()
{
  if(theevents.empty()){
    set_status(0, "No events in file");
    return;
  }

  set_status(0, "Run %'d, subrun %d, event %'d (%'d/%'d%s in the file)",
    theevents[gevi].nrun, theevents[gevi].nsubrun,
    theevents[gevi].nevent, gevi+1,
    (int)theevents.size(), ghave_read_all?"":"+");
}

// Set second status line, which reports on timing information
static void set_eventn_status1()
{
  noeevent & E = theevents[gevi];

  char status1[MAXSTATUS];

  int pos = snprintf(status1, MAXSTATUS, "Ticks %s%'d through %'d.  ",
             BOTANY_BAY_OH_INT(E.mintick), E.maxtick);
  if(E.current_mintick != E.current_maxtick)
    pos += snprintf(status1+pos, MAXSTATUS-pos,
      "Showing ticks %s%d through %s%d (%s%.3f through %s%.3f μs)",
      BOTANY_BAY_OH_INT(E.current_mintick),
      BOTANY_BAY_OH_INT(E.current_maxtick),
      BOTANY_BAY_OH_NO( E.current_mintick/64.),
      BOTANY_BAY_OH_NO( E.current_maxtick/64.));
  else
    pos += snprintf(status1+pos, MAXSTATUS-pos,
      "Showing tick %s%d (%s%.3f μs)",
      BOTANY_BAY_OH_INT(E.current_maxtick),
      BOTANY_BAY_OH_NO( E.current_maxtick/64.));

  set_status(1, status1);
}

// Set third status line, which reports on hit information
static void set_eventn_status2()
{
  if(active_plane < 0 || active_cell < 0){
    set_status(2, "Mouse over a cell for more information");
    return;
  }

  char status2[MAXSTATUS];
  int pos = snprintf(status2, MAXSTATUS, "Plane %d, cell %d: ",
                     active_plane, active_cell);

  // TODO: display calibrated energies when possible
  std::vector<hit> & THEhits = theevents[gevi].hits;
  bool needseparator = false;

  // TODO: make this more flexible.
  const int maxmatches = 2;
  int matches = 0;
  for(unsigned int i = 0; i < THEhits.size(); i++){
    if(THEhits[i].plane == active_plane &&
       THEhits[i].cell  == active_cell){
      matches++;
      if(matches <= maxmatches){
        pos += pos >= MAXSTATUS?0:snprintf(status2+pos, MAXSTATUS-pos,
            "%sTDC = %s%d (%s%.3f μs), TNS = %s%.3f μs%s, ADC = %s%d",
            needseparator?"; ":"",
            BOTANY_BAY_OH_INT(THEhits[i].tdc),
            BOTANY_BAY_OH_NO (THEhits[i].tdc/64.),
            BOTANY_BAY_OH_NO (THEhits[i].tns/1000),
            THEhits[i].good_tns?"":"(bad)",
            BOTANY_BAY_OH_INT(THEhits[i].adc));
        needseparator = true;
      }
      else if(matches == maxmatches+1){
        pos += pos >= MAXSTATUS?0:snprintf(status2+pos, MAXSTATUS-pos,
            "; and more...");
      }
    }
  }
  set_status(2, status2);
}

// Set third status line to a special status when we are reading a big event
static void set_eventn_status2progress(const int nhit, const int tothits)
{
  set_status(2, "Processing big event, %d/%d hits", nhit, tothits);
}

// Set all status lines
static void set_eventn_status()
{
  set_eventn_status0();

  if(theevents.empty()) return;

  set_eventn_status1();
  set_eventn_status2();
}

// Draw all the hits in the event that we need to draw, depending on
// whether we are animating or have been exposed, etc.
static void draw_hits(cairo_t * cr, const DRAWPARS * const drawpars)
{
  cairo_set_line_width(cr, 1.0);

  std::vector<hit> & THEhits = theevents[gevi].hits;

  std::sort(THEhits.begin(), THEhits.end(), by_charge);

  const int big = 100000;
  const bool bigevent = THEhits.size() > big;

  for(unsigned int i = 0; i < THEhits.size(); i++){
    const hit & thishit = THEhits[i];

    if(thishit.tdc < drawpars->firsttick ||
       thishit.tdc > drawpars->lasttick) continue;

    if(bigevent && i%big == 0)
      set_eventn_status2progress(i, THEhits.size());

    draw_hit(cr, thishit);
  }
}

// Unhighlight the cell that is no longer being moused over, indicated by
// oldactive_plane/cell, and highlight the new one.  Do this instead of a full
// redraw of edarea, which is expensive and causes very noticeable lag for the
// FD.
static void change_highlighted_cell(GtkWidget * widg,
                                    const int oldactive_plane,
                                    const int oldactive_cell)
{
  cairo_t * cr = gdk_cairo_create(widg->window);
  cairo_set_line_width(cr, 1.0);
  std::vector<hit> & THEhits = theevents[gevi].hits;

  // We may need to find any number of hits since more than one hit
  // can be in the same cell.  It is not guaranteed that the same hit
  // ends up visible, but I'm just going to live with that.
  for(unsigned int i = 0; i < THEhits.size(); i++){
    hit & thishit = THEhits[i];
    if((thishit.plane == oldactive_plane && thishit.cell == oldactive_cell) ||
       (thishit.plane ==    active_plane && thishit.cell ==    active_cell))
      draw_hit(cr, thishit);
  }
  cairo_destroy(cr);
}

// Handle the mouse arriving at a spot in the drawing area. Find what cell the
// user is pointing at, highlight it, and show information about it.
static gboolean mouseover(__attribute__((unused)) GtkWidget * widg,
                          GdkEventMotion * gevent,
                          __attribute__((unused)) gpointer data)
{
  if(gevent == NULL) return TRUE; // shouldn't happen
  if(theevents.empty()) return TRUE; // No coordinates in this case

  const int oldactive_plane = active_plane;
  const int oldactive_cell  = active_cell;

  active_plane = screen_to_plane((int)gevent->x, (int)gevent->y);
  active_cell  = screen_to_cell ((int)gevent->x, (int)gevent->y);

  change_highlighted_cell(edarea, oldactive_plane, oldactive_cell);
  set_eventn_status2();

  return TRUE;
}

// draw_event_and to_next_free_run circularly refer to each other...
static gboolean to_next_free_run(__attribute__((unused)) gpointer data);

// Draw a whole event, a range, or an animation frame, as dictated by
// the DRAWPARS.
static void draw_event(const DRAWPARS * const drawpars)
{
  set_eventn_status();
  if(theevents.empty()) return;

  if(!isfd && theevents[gevi].fdlike) setfd();

  cairo_t * cr = gdk_cairo_create(edarea->window);
  cairo_push_group(cr);

  // Do not blank the display in the middle of an animation unless necessary
  if(drawpars->clear) draw_background(cr);

  draw_hits(cr, drawpars);
  set_eventn_status(); // overwrite anything that draw_hits did

  cairo_pop_group_to_source(cr);
  cairo_paint(cr);
  cairo_destroy(cr);
}

static gboolean redraw_event(__attribute__((unused)) GtkWidget *widg,
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

static void draw_whole_user_event()
{
  theevents[gevi].current_mintick = theevents[gevi].user_mintick;
  theevents[gevi].current_maxtick = theevents[gevi].user_maxtick;

  DRAWPARS drawpars;
  drawpars.firsttick = theevents[gevi].current_mintick;
  drawpars.lasttick  = theevents[gevi].current_maxtick;
  drawpars.clear = true;
  draw_event(&drawpars);
}

static gboolean animation_step(__attribute__((unused)) gpointer data)
{
  noeevent & E = theevents[gevi];

  DRAWPARS drawpars;
  // Must redraw if we are just starting the animation, or if it is
  // non-cumulative, i.e. the old hits have to be re-hidden
  drawpars.clear = E.current_maxtick == theevents[gevi].user_mintick ||
                   !cumulative_animation;

  E.current_maxtick += TDCSTEP;

  // Necessary to make visible_hit() work.  Otherwise, hits incorrectly
  // become visible when moused over.
  if(!cumulative_animation) E.current_mintick = E.current_maxtick;

  drawpars.firsttick = cumulative_animation?E.current_mintick
                                           :E.current_maxtick-(TDCSTEP-1);
  drawpars.lasttick  = E.current_maxtick;
  draw_event(&drawpars);
  return animate && E.current_maxtick < theevents[gevi].user_maxtick;
}

static gboolean handle_event()
{
  noeevent & E = theevents[gevi];
  adjusttick_callback_inhibit = true;
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(maxtickslider), E.mintick, E.maxtick);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(mintickslider), E.mintick, E.maxtick);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(maxtickslider), E.user_maxtick);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(mintickslider), E.user_mintick);
  gtk_widget_draw(maxtickslider, NULL);
  gtk_widget_draw(mintickslider, NULL);
  adjusttick_callback_inhibit = false;

  if(animate){
    E.current_maxtick = E.current_mintick = E.user_mintick;
    if(animatetimeoutid) g_source_remove(animatetimeoutid);

    // Do one step immediately to be responsive to the user even if the
    // speed is set very slow
    animation_step(NULL);

    animatetimeoutid =
      g_timeout_add(std::max(1, (int)animationinterval),
                    animation_step, NULL);
  }
  else{
    if(animatetimeoutid) g_source_remove(animatetimeoutid);
    animatetimeoutid = 0;
    draw_whole_user_event();
  }

  return FALSE;
}

// Used, e.g., with g_timeout_add() to get an event drawn after re-entering the
// GTK main loop.
static gboolean draw_event_from_timer(__attribute__((unused)) gpointer data)
{
  draw_whole_user_event();
  return FALSE; // don't call me again
}

// Move 'change' events through the event list.  If the requested event is in
// the list, this trivially updates the integer 'gevi', the global event
// number.  Does bounds checking and clamps the result, except in the case the
// upper bound is not known (see the "otherwise" case below).
//
// Returns true if the event is ready to be drawn.  Otherwise, issues a request
// to fetch another event and does *not* change the global event number. Another
// call to get_event() is needed once we have the event.
static bool get_event(const int change)
{
  if(gevi+change >= (int)theevents.size()){
    if(ghave_read_all){
      gevi = theevents.size() - 1;
    }
    else{
      // TODO make this work with abs(change) > 1... actually, do we care?
      // It would make it easier to jump forward many events, I suppose.
      // XXX this is great except that if events happen while not
      // in the GTK loop, we get an assertion failure on the console.
      // Maybe it's harmless, or nearly harmless, in that we just lose
      // the user input.  Not sure.  It more or less seems to work.
      gtk_main_quit();
      return false;
    }
  }
  else if(gevi+change < 0){
    gevi = 0;
  }
  else{
    gevi += change;
  }

  return true;
}

// Called when idle to load events into memory
static gboolean prefetch_an_event(__attribute__((unused)) gpointer data)
{
  if(ghave_read_all) return FALSE; // don't call this again

  if(gtk_events_pending()) return TRUE;

  // exit GTK event loop to get another event from art
  prefetching = true;
  gtk_main_quit();
  return TRUE;
}

// Why are you always preparing?  You're always preparing! Just go!
static void prepare_to_swich_events()
{
  active_cell = active_plane = -1;
}

// Display the next or previous event.
static void to_next(__attribute__((unused)) GtkWidget * widget,
                    gpointer data)
{
  prepare_to_swich_events();
  const bool * const forward = (const bool * const)data;
  if(get_event((*forward)?1:-1))
    handle_event();
}

// Called to move us the next event while free running
static gboolean to_next_free_run(__attribute__((unused)) gpointer data)
{
  bool forward = true;
  to_next(NULL, &forward);

  const bool atend = gevi == (int)theevents.size()-1 && ghave_read_all;

  if(atend){
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(freerun_checkbox),
                                 FALSE);
    free_running = false;
  }
  return free_running && !atend;
}

// Returns true if the list of events we have *now* has an event with the given
// number. No assumptions are made about the ordering of event numbers, although
// I'm not sure if it's possible for them to be non-increasing in the current
// art design.
static bool have_event_by_number(const unsigned int n)
{
  for(unsigned int i = 0; i < theevents.size(); i++)
    if(theevents[i].nevent == n)
      return true;
  return false;
}

// Blank out the fourth status line that sometimes has error messages
static gboolean clear_error_message(__attribute__((unused)) gpointer dt)
{
  set_status(3, "");
  return FALSE;
}

// Get a user entered event number from the text entry widget
static void getuserevent()
{
  errno = 0;
  char * endptr;
  const int userevent =
    strtol(gtk_entry_get_text(GTK_ENTRY(ueventbox)), &endptr, 10);

  clear_error_message(NULL);

  if((errno == ERANGE && (userevent == INT_MAX || userevent == INT_MIN))
     || (errno != 0 && userevent == 0)
     || endptr == optarg || *endptr != '\0'
     || !have_event_by_number(userevent)){
    if(!theevents.empty())
      set_status(3, "Entered event invalid or not available. %sI have "
                 "events %d through %d%s",
                 ghave_read_all?"":"Currently ",
                 theevents[0].nevent,
                 theevents[theevents.size()-1].nevent,
                 theevents[theevents.size()-1].nevent-theevents[0].nevent
                 == theevents.size()-1?"":" (not consecutive)");
    g_timeout_add(8e3, clear_error_message, NULL);
    return;
  }

  if(userevent == (int)theevents[gevi].nevent) return;

  const bool forward = userevent > (int)theevents[gevi].nevent;
  while(userevent != (int)theevents[gevi].nevent)
    // Don't go through get_event because we do *not* want to try
    // getting more events from the file
    gevi += (forward?1:-1);

  prepare_to_swich_events();
  draw_whole_user_event();
}

// Handle the user clicking the "run freely" check box.
static void toggle_freerun(__attribute__((unused)) GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  free_running = GTK_TOGGLE_BUTTON(w)->active;
  if(free_running)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(animate_checkbox), FALSE);

  if(free_running){
    // Do not call g_timeout_add with an interval of zero, since that seems
    // to be a special case that causes the function to be run repeatedly
    // *without* returning to the main loop, or without doing all the things
    // in the main loop that are usually done, or something. In any case,
    // empirically, it causes multiple (infinite?) calls to to_next_free_run
    // after gtk_main_quit() has been called, which locks us up. It is quite
    // possible that by setting the interval to 1, it only makes it *unlikely*
    // that further events are processed between gtk_main_quit() and exiting
    // the main loop, in which case I have a bug that has only been suppressed
    // instead of fixed.
    freeruntimeoutid =
      g_timeout_add(std::max((gulong)1, freeruninterval), to_next_free_run, NULL);
  }
  else{
    if(freeruntimeoutid) g_source_remove(freeruntimeoutid);
    freeruntimeoutid = 0;
  }
}

// Handle the user clicking the "cumulative animation" check box.
static void toggle_cum_ani(GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  cumulative_animation = GTK_TOGGLE_BUTTON(w)->active;

  // If switching to cumulative, need to draw all the previous hits.  If
  // switching away, need to blank them all out.  In either case, don't wait
  // until the next animation step, because that appears laggy for the user.
  // TODO: make that actually work.
  DRAWPARS drawpars;
  if(cumulative_animation){
    drawpars.firsttick = theevents[gevi].user_mintick;
    drawpars.lasttick  = theevents[gevi].current_maxtick;
  }
  else{
    theevents[gevi].current_mintick = theevents[gevi].current_maxtick;
    drawpars.firsttick = theevents[gevi].current_mintick;
    drawpars.lasttick  = theevents[gevi].current_maxtick;
  }
  drawpars.clear = !cumulative_animation;
  draw_event(&drawpars);
}

// Handle the user clicking the "animate" check box.
static void toggle_animate(GtkWidget * w, __attribute__((unused)) gpointer dt)
{
  animate = GTK_TOGGLE_BUTTON(w)->active;
  if(animate)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(freerun_checkbox), FALSE);
  else
    TDCSTEP = 1;
  handle_event();
}

static void restart_animation(__attribute__((unused)) GtkWidget * w,
                              __attribute__((unused)) gpointer d)
{
  // Assume that if the user wants the animation restarted, then the
  // user wants animation.
  animate = true;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(animate_checkbox), TRUE);
  handle_event();
}

// Convert the abstract "speed" number from the user into a delay.
static void set_intervals(const int speednum)
{
  freeruninterval   = (int)pow(10, 6.5 - speednum/2.0);
  animationinterval = (int)pow(10, 4.0 - speednum/2.0);

  switch(speednum < 1?1:speednum > 11?11:speednum){
    case  1: TDCSTEP =   1; break;
    case  2: TDCSTEP =   2; break;
    case  3: TDCSTEP =   4; break;
    case  4: TDCSTEP =   4; break;
    case  5: TDCSTEP =   4; break;
    case  6: TDCSTEP =   8; break;
    case  7: TDCSTEP =   8; break;
    case  8: TDCSTEP =   8; break;
    case  9: TDCSTEP =  16; break;
    case 10: TDCSTEP =  32; break;
    case 11: TDCSTEP = 128; break;
  }
}

static void adjusttick(GtkWidget * wg, const gpointer dt)
{
  if(adjusttick_callback_inhibit) return;

  noeevent & E = theevents[gevi];

  (dt != NULL && *(bool *)dt? E.user_maxtick: E.user_mintick)
    = gtk_adjustment_get_value(GTK_ADJUSTMENT(wg));

  const int32_t oldcurrent_maxtick = E.current_maxtick;
  const int32_t oldcurrent_mintick = E.current_mintick;
  if(animate){
    // TODO: more elegantly handle the case that animation is on, but
    // the animation has completed.
    E.current_maxtick = std::min(E.current_maxtick, E.user_maxtick);
    E.current_mintick = std::max(E.current_mintick, E.user_mintick);
  }
  else{
    E.current_maxtick = E.user_maxtick;
    E.current_mintick = E.user_mintick;
  }

  DRAWPARS drawpars;
  drawpars.clear = E.current_maxtick < oldcurrent_maxtick ||
                   E.current_mintick > oldcurrent_mintick;

  // TODO can optimize this to only draw the new range
  drawpars.firsttick = E.current_mintick;
  drawpars.lasttick  = E.current_maxtick;
  draw_event(&drawpars);
}

// Respond to changes in the spin button for animation/free running speed
static void adjustspeed(GtkWidget * wg,
                        __attribute__((unused)) const gpointer dt)
{
  set_intervals(gtk_adjustment_get_value(GTK_ADJUSTMENT(wg)));

  if(freeruntimeoutid) g_source_remove(freeruntimeoutid);
  if(free_running)
    freeruntimeoutid =
      g_timeout_add(std::max((gulong)1,freeruninterval), to_next_free_run, NULL);

  if(animatetimeoutid) g_source_remove(animatetimeoutid);
  if(animate)
    animatetimeoutid =
      g_timeout_add(std::max(1, (int)animationinterval),
                    animation_step, NULL);
  else
    animatetimeoutid = 0;
}

// Called when the window is resized or moved. The buttons don't redraw
// themselves when the window is resized, so we have to get it done. I
// don't really want to call this 100 times when a user slowly resizes a window
// that takes a long time to draw, but nor do I want to write a complex system
// for dealing with that case...
static gboolean redraw_window(GtkWidget * win,
                              GdkEventConfigure * event,
                              __attribute__((unused)) gpointer d)
{
  static int oldwidth = event->width, oldheight = event->height;
  static bool first = true;
  const bool need_redraw =
    first || event->width > oldwidth || event->height > oldheight;
  first = false;
  oldwidth = event->width, oldheight = event->height;

  if(need_redraw) gtk_widget_queue_draw(win);

  return !need_redraw; // FALSE means *do* propagate this to children
}

static void close_window()
{
  // We could quit gently:
  // gtk_main_quit(); exit(0);
  // But there is nothing to save, so just drop everything quickly.
  _exit(0);
}

/**********************************************************************/
/*                          Widget setup                              */
/**********************************************************************/

// tick select spin button
static GtkWidget * make_tickslider(const bool ismax)
{
  const int initialticknum = 0;
  GtkObject * const tickadj = gtk_adjustment_new
    (initialticknum, 0, 1000, 1, 10, 0);
  GtkWidget * tickslider = gtk_spin_button_new(GTK_ADJUSTMENT(tickadj), 10, 0);
  g_signal_connect(tickadj, "value_changed", G_CALLBACK(adjusttick),
                   new bool(ismax));
  gtk_entry_set_max_length (GTK_ENTRY(tickslider), 6);
  gtk_entry_set_width_chars(GTK_ENTRY(tickslider), 6);
  return tickslider;
}

// speed spin button
static GtkWidget * make_speedslider()
{
  const int initialspeednum = 6;
  GtkObject * const speedadj = gtk_adjustment_new
    (initialspeednum, 1, 11, 1, 1, 0);
  set_intervals(initialspeednum);
  g_signal_connect(speedadj, "value_changed", G_CALLBACK(adjustspeed), NULL);

  GtkWidget * const speedslider
    = gtk_spin_button_new(GTK_ADJUSTMENT(speedadj), 10, 0);
  gtk_entry_set_max_length (GTK_ENTRY(speedslider), 2);
  gtk_entry_set_width_chars(GTK_ENTRY(speedslider), 2);
  return speedslider;
}

static GtkWidget * make_ticklabel(const bool ismax)
{
  GtkWidget * ticklabel = gtk_text_view_new();
  GtkTextBuffer * ticklabeltext = gtk_text_buffer_new(0);
  gtk_text_view_set_justification(GTK_TEXT_VIEW(ticklabel), GTK_JUSTIFY_CENTER);
  const char * const ticklabelbuf = ismax?"Max Tick":"Min Tick";
  gtk_text_buffer_set_text(ticklabeltext, ticklabelbuf, strlen(ticklabelbuf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(ticklabel), ticklabeltext);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(ticklabel), false);
  return ticklabel;
}

static GtkWidget * make_speedlabel()
{
  GtkWidget * speedlabel = gtk_text_view_new();
  GtkTextBuffer * speedlabeltext = gtk_text_buffer_new(0);
  gtk_text_view_set_justification(GTK_TEXT_VIEW(speedlabel), GTK_JUSTIFY_CENTER);
  const char * const speedlabelbuf = "Speed";
  gtk_text_buffer_set_text(speedlabeltext, speedlabelbuf, strlen(speedlabelbuf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(speedlabel), speedlabeltext);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(speedlabel), false);
  return speedlabel;
}

static GtkWidget * make_ueventbox()
{
  GtkWidget * ueventbox = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(ueventbox), 20);//length of a int64
  gtk_entry_set_width_chars(GTK_ENTRY(ueventbox), 5);
  g_signal_connect(ueventbox, "activate", G_CALLBACK(getuserevent), NULL);
  return ueventbox;
}

// Sets up the GTK window, add user event hooks, start the necessary
// timer(s), draw the first event.
static void setup()
{
  gtk_init(NULL, NULL);
  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win), "NOE: New nOva Event viewer");
  g_signal_connect(win, "delete-event", G_CALLBACK(close_window), 0);

  edarea = gtk_drawing_area_new();
  setboxes();
  g_signal_connect(win,"configure-event",G_CALLBACK(redraw_window),NULL);
  g_signal_connect(edarea,"expose-event",G_CALLBACK(redraw_event),NULL);
  g_signal_connect(edarea, "motion-notify-event", G_CALLBACK(mouseover), NULL);
  gtk_widget_set_events(edarea, gtk_widget_get_events(edarea)
                                | GDK_POINTER_MOTION_MASK);

  GtkWidget * next = gtk_button_new_with_mnemonic("_Next Event");
  g_signal_connect(next, "clicked", G_CALLBACK(to_next), new bool(true));

  GtkWidget * prev = gtk_button_new_with_mnemonic("_Previous Event");
  g_signal_connect(prev, "clicked", G_CALLBACK(to_next), new bool(false));

  animate_checkbox = gtk_check_button_new_with_mnemonic("_Animate");
  cum_ani_checkbox = gtk_check_button_new_with_mnemonic("_Cumulative animation");
  freerun_checkbox = gtk_check_button_new_with_mnemonic("_Free running");

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(animate_checkbox),
                               animate);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cum_ani_checkbox),
                               cumulative_animation);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(freerun_checkbox),
                               free_running);

  g_signal_connect(animate_checkbox, "toggled", G_CALLBACK(toggle_animate),edarea);
  g_signal_connect(cum_ani_checkbox, "toggled", G_CALLBACK(toggle_cum_ani),edarea);
  g_signal_connect(freerun_checkbox, "toggled", G_CALLBACK(toggle_freerun),edarea);

  GtkWidget * re_an_button = gtk_button_new_with_mnemonic("_Restart animation");
  g_signal_connect(re_an_button, "clicked", G_CALLBACK(restart_animation), NULL);

  maxtickslider                  = make_tickslider(true);
  mintickslider                  = make_tickslider(false);
  GtkWidget * const maxticklabel = make_ticklabel(true);
  GtkWidget * const minticklabel = make_ticklabel(false);
  GtkWidget * const speedslider  = make_speedslider();
  GtkWidget * const speedlabel   = make_speedlabel();
  ueventbox                      = make_ueventbox();

  ueventbut = gtk_button_new_with_mnemonic("_Go to event");
  g_signal_connect(ueventbut, "clicked",  G_CALLBACK(getuserevent), NULL);

  const int nrow = 3+NSTATBOXES, ncol = 11;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);

  GtkWidget * top_row_widgets[ncol] = {
    prev, next, mintickslider, maxtickslider, animate_checkbox, cum_ani_checkbox,
    re_an_button, freerun_checkbox, speedslider, ueventbox, ueventbut};

  GtkWidget * second_row_widgets[ncol] = {
    NULL, NULL, minticklabel, maxticklabel, NULL,
    NULL, NULL, NULL, speedlabel, NULL, NULL};

  for(int c = 0; c < ncol; c++){
    gtk_table_attach_defaults(GTK_TABLE(tab), top_row_widgets[c], c, c+1, 0, 1);

    if(second_row_widgets[c] != NULL){
      gtk_table_attach_defaults(GTK_TABLE(tab),second_row_widgets[c],c,c+1,1,2);
    }
    else{
      // Have to put a blank text box in each table cell or else the
      // background color is inconsistent.
      GtkWidget * blanklabel = gtk_text_view_new();
      gtk_text_view_set_editable(GTK_TEXT_VIEW(blanklabel), false);
      gtk_table_attach_defaults(GTK_TABLE(tab), blanklabel, c, c+1, 1, 2);
    }
  }

  for(int i = 0; i < NSTATBOXES; i++){
    statbox[i]  = gtk_text_view_new();
    stattext[i] = gtk_text_buffer_new(0);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[i]), stattext[i]);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(statbox[i]), false);
    gtk_table_attach_defaults(GTK_TABLE(tab), statbox[i], 0, ncol, 2+i, 3+i);
  }

  gtk_table_attach_defaults(GTK_TABLE(tab), edarea, 0, ncol, 2+NSTATBOXES, nrow);

  // This isn't the size I want, but along with requesting the size of the
  // edarea widget, it has the desired effect, at least more or less.
  gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);

  gtk_widget_show_all(win);

  get_event(0);
  handle_event();

  // This is a hack that gets all objects drawn fully. It seems that the window
  // initially getting set to the size of the ND and then resized to the size
  // of the FD fails to have the expected effect and the objects outside the
  // original area don't get redrawn. Maybe this is is an xmonad-only problem?
  // This is a case of a window resizing itself, and maybe window managers are
  // supposed to send an expose event in this case, but xmonad doesn't. Or maybe
  // they aren't supposed to according to the spec, but all the other ones do...
  gtk_widget_queue_draw(win);

  if(!ghave_read_all) g_timeout_add(20, prefetch_an_event, NULL);
}

/*********************************************************************/
/*                          Public functions                         */
/*********************************************************************/

// If we could ask for art events from art, this would be the entry point to
// the program.  However, art only allows access to the art events through its
// own event loop, so we have to read events in that loop, then run the GTK
// event loop for a while, then break out of that and go back to art's event
// loop to get more events, etc.  Each time, we return here.
//
// If there are no more events to read from art, have_read_all will be true
// and we will know that we should stay in the GTK event loop.
void realmain(const bool have_read_all)
{
  if(have_read_all) ghave_read_all = true;
  static bool first = true;
  if(first){
    first = false;
    setup();
  }
  else if(prefetching){
    set_eventn_status0();
    prefetching = false;
  }
  else{
    get_event(1);
    g_timeout_add(0, draw_event_from_timer, NULL);
  }
  gtk_main();
}
