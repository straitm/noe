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

static const int viewsep = 8; // vertical cell widths between x and y views

// Maximum length of any string being printed to a status bar
static const int MAXSTATUS = 1024;

// Let's see.  I believe both detectors read out in increments of 4 TDC units,
// but the FD is multiplexed whereas the ND isn't, so any given channel at the
// FD can only report every 4 * 2^N TDC units, where I think N = 2.
static const int TDCSTEP = 4;

/* The events and the current event index in the vector */
extern std::vector<noeevent> theevents;
static int gevi = 0;

/* GTK objects */
static const int NSTATBOXES = 4;
static GtkWidget * statbox[NSTATBOXES];
static GtkTextBuffer * stattext[NSTATBOXES];
static GtkWidget * edarea = NULL;
static GtkWidget * animate_checkbox = NULL,
                 * cum_ani_checkbox = NULL,
                 * freerun_checkbox = NULL;
static GtkWidget * ueventbut = NULL;
static GtkWidget * ueventbox = NULL;

/* Running flags */
static bool ghave_read_all = false;
static bool prefetching = false;
static bool cancel_draw = false;
static bool switch_to_cumulative = false;
static bool animating = false;
static bool launch_next_freerun_timer_at_draw_end = false;
static int currenttick = 0;
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

static int freeruninterval = 0; // ms.  Immediately overwritten.
static gulong freeruntimeoutid = 0;

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

// Update the given status bar to the given text and also process all
// window events.
static void set_status(const int boxn, const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  static char buf[MAXSTATUS];
  vsnprintf(buf, MAXSTATUS-1, format, ap);

  gtk_text_buffer_set_text(stattext[boxn], buf, strlen(buf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[boxn]), stattext[boxn]);

  // Makes performance much better for reasons I don't understand
  while(g_main_context_iteration(NULL, FALSE));
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
    if(animating && THEhits[i].tdc > currenttick) continue;
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

  set_status(0, "Run %'d, subrun %d Event %'d (%'d/%'d%s in the file)",
    theevents[gevi].nrun, theevents[gevi].nsubrun,
    theevents[gevi].nevent, gevi+1,
    (int)theevents.size(), ghave_read_all?"":"+");
}

// Set second status line, which reports on timing information
static void set_eventn_status1()
{
  noeevent * THEevent = &theevents[gevi];

  char status1[MAXSTATUS];

  int pos = snprintf(status1, MAXSTATUS-1, "Ticks %s%'d through %'d.  ",
             BOTANY_BAY_OH_INT(THEevent->mintick), THEevent->maxtick);
  if(!animate)
    pos += snprintf(status1+pos, MAXSTATUS-1-pos, "Showing all ticks");
  else if(cumulative_animation)
    pos += snprintf(status1+pos, MAXSTATUS-1-pos,
      "Showing ticks %s%d through %s%d (%s%.3f μs)",
      BOTANY_BAY_OH_INT(THEevent->mintick), BOTANY_BAY_OH_INT(currenttick),
      BOTANY_BAY_OH_NO(currenttick/64.));
  else
    pos += snprintf(status1+pos, MAXSTATUS-1-pos,
      "Showing tick %s%d (%s%.3f μs)",
      BOTANY_BAY_OH_INT(currenttick), BOTANY_BAY_OH_NO(currenttick/64.));

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
  int pos = snprintf(status2, MAXSTATUS-1, "Plane %d, cell %d: ",
                     active_plane, active_cell);

  // TODO: display calibrated energies
  std::vector<hit> & THEhits = theevents[gevi].hits;
  bool needseparator = false;
  for(unsigned int i = 0; i < THEhits.size(); i++){
    if(THEhits[i].plane == active_plane &&
       THEhits[i].cell  == active_cell){
      pos += snprintf(status2+pos, MAXSTATUS-1-pos,
          "%sTDC = %s%d (%s%.3f μs), ADC = %s%d",
          needseparator?"; ":"",
          BOTANY_BAY_OH_INT(THEhits[i].tdc),
          BOTANY_BAY_OH_NO (THEhits[i].tdc/64.),
          BOTANY_BAY_OH_INT(THEhits[i].adc));
      needseparator = true;
    }
  }
  set_status(2, status2);
}

// Set third status line to a special status when we are reading a big event
static void set_eventn_status2alt(const int nhit, const int tothits)
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
static void draw_hits(cairo_t * cr, const bool fullredraw)
{
  cairo_set_line_width(cr, 1.0);

  std::vector<hit> & THEhits = theevents[gevi].hits;

  std::sort(THEhits.begin(), THEhits.end(), by_charge);

  const bool bigevent = THEhits.size() > 50000;

  for(unsigned int i = 0; i < THEhits.size(); i++){
    const hit & thishit = THEhits[i];

    if(animating && (thishit.tdc - currenttick)%TDCSTEP != 0)
      printf("%d %d\n", thishit.tdc, currenttick);

    // If we're not animating, we're just going to draw everything no matter
    // what.  Otherwise, we need to know whether to draw just the new stuff
    // or everthing up to the current tick.
    if(animate){
      if(cumulative_animation && fullredraw){
        if(thishit.tdc > currenttick) continue;
      }
      else{
        if(thishit.tdc > currenttick ||
           thishit.tdc < currenttick - TDCSTEP-1) continue;
      }
    }
    else if(bigevent && i%50000 == 0){
      set_eventn_status2alt(i, THEhits.size());
    }

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

// Draw an event in its entirety. If animating, this function handles the
// animation and being responsive to user actions during the animation.  It's
// the most complicated part.
static gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee,
                           __attribute__((unused)) gpointer data)
{
  set_eventn_status();
  if(theevents.empty()) return TRUE;

  static int drawn = 0;
  const bool exposed = ee != NULL;
  if(!exposed) drawn++;

  noeevent * THEevent = &theevents[gevi];

  if(!isfd && THEevent->fdlike) setfd();

  cancel_draw = false;

  const int thisfirsttick = !animate? THEevent->maxtick
                          : exposed ? currenttick
                          :           THEevent->mintick,
            thislasttick =  !animate?THEevent->maxtick
                          : exposed ? currenttick
                          :           THEevent->maxtick;

  if(animate) animating = true;
  for(currenttick = thisfirsttick;
      currenttick <= thislasttick;
      currenttick+=TDCSTEP){

    cairo_t * cr = gdk_cairo_create(widg->window);
    cairo_push_group(cr);

    // Do not blank the display in the middle of an animation unless necessary
    const bool need_redraw = switch_to_cumulative || exposed || !animate ||
       (animate && currenttick == THEevent->mintick) ||
       (animate && !cumulative_animation);
    switch_to_cumulative = false;
    if(need_redraw) draw_background(cr);

    draw_hits(cr, need_redraw);

    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
    cairo_destroy(cr);

    // Check if we have started drawing another event while
    // still in here.  If so, don't keep drawing this one.
    const int thisdrawn = drawn;
    set_eventn_status();

    if(animate && currenttick != THEevent->maxtick){
      if(freeruninterval > 0){
        // The delay between animation frames is 3/100 the delay between free
        // running events, but measured in microseconds instead of milliseconds.
        const int animationmult = 30;

        // To keep the application responsive, sleep in 50ms chunks.  This is
        // kinda dumb.  GTK doesn't seem to have a way of saying "go back to
        // the main loop for X seconds".  I think the correct way to do this is
        // to use a g_timeout_add with each call doing one iteration of the
        // animation, but that's inside out from how I've written the animation
        // so far.
        int left = freeruninterval * animationmult;
        do{
          usleep(std::min(left, 50000));
          left -= 50000;

          // If the new interval is smaller, respond immediately by reducing
          // the amount of time until the next change, and vice versa.
          const int oldinterval = freeruninterval;
          while(g_main_context_iteration(NULL, FALSE));
          left -= (oldinterval - freeruninterval)*animationmult;
        }while(left > 0);
      }

      while(g_main_context_iteration(NULL, FALSE));
      if(cancel_draw || drawn != thisdrawn) break;
    }
  }

  // Set again at the end in case we were displaying a status bar
  if(!cancel_draw)
    set_eventn_status();

  // If we are moving to a new detector event, make sure we don't keep trying
  // to draw this detector event.  However, if we're here because of an X
  // expose event, we do want to keep going.
  if(!exposed){
    cancel_draw = true;
    animating = false;
  }

  if(launch_next_freerun_timer_at_draw_end){
    freeruntimeoutid = g_timeout_add(freeruninterval, to_next_free_run, NULL);
    launch_next_freerun_timer_at_draw_end = false;
  }

  return FALSE;
}

// Used, e.g., with g_timeout_add() to get an event drawn after re-entering the
// GTK main loop.
static gboolean draw_event_from_timer(__attribute__((unused)) gpointer data)
{
  draw_event(edarea, NULL, NULL);
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

  if(animating || gtk_events_pending()) return TRUE;

  // exit GTK event loop to get another event from art
  prefetching = true;
  gtk_main_quit();
  return TRUE;
}

// Why are you always preparing?  You're always preparing! Just go!
static void prepare_to_swich_events()
{
  cancel_draw = true;
  active_cell = active_plane = -1;
}

// Display the next or previous event.
static void to_next(__attribute__((unused)) GtkWidget * widget,
                    gpointer data)
{
  prepare_to_swich_events();
  const bool * const forward = (const bool * const)data;
  if(get_event((*forward)?1:-1))
    draw_event(edarea, NULL, NULL);
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
  draw_event(edarea, NULL, NULL);
}

// Handle the user clicking the "run freely" check box.
static void toggle_freerun(__attribute__((unused)) GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  free_running = GTK_TOGGLE_BUTTON(w)->active;
  if(free_running){
    freeruntimeoutid = g_timeout_add(freeruninterval, to_next_free_run, NULL);
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

  // Must note that we've switched to trigger a full redraw
  if(cumulative_animation) switch_to_cumulative = true;
}

// Handle the user clicking the "animate" check box.
static void toggle_animate(GtkWidget * w, __attribute__((unused)) gpointer dt)
{
  animate = GTK_TOGGLE_BUTTON(w)->active;
  // Schedule the draw for the next time the main event loop runs instead of
  // drawing immediately because otherwise, the checkbox doesn't appear checked
  // until the user moves the mouse off of it.  I don't really understand how
  // the problem comes about, but this fixes it.  It is not a problem for
  // any of the other checkboxes.
  g_timeout_add(0, draw_event_from_timer, NULL);
}

static void restart_animation(__attribute__((unused)) GtkWidget * w,
                              __attribute__((unused)) gpointer d)
{
  // Assume that if the user wants the animation restarted, then the
  // user wants animation.
  animate = true;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(animate_checkbox), TRUE);
  g_timeout_add(0, draw_event_from_timer, NULL);
}

// Convert the abstract "speed" number from the user into a delay.
static void set_freeruninterval(const int speednum)
{
  switch(speednum < 1?1:speednum > 11?11:speednum){
    case  1: freeruninterval = (int)pow(10, 5.0); break;
    case  2: freeruninterval = (int)pow(10, 4.5); break;
    case  3: freeruninterval = (int)pow(10, 4.0); break;
    case  4: freeruninterval = (int)pow(10, 3.5); break;
    case  5: freeruninterval = (int)pow(10, 3.0); break;
    case  6: freeruninterval = (int)pow(10, 2.5); break;
    case  7: freeruninterval = (int)pow(10, 2.0); break;
    case  8: freeruninterval = (int)pow(10, 1.5); break;
    case  9: freeruninterval = (int)pow(10, 1.0); break;
    case 10: freeruninterval = (int)pow(10, 0.5); break;
    case 11: freeruninterval = 0; break;
  }
}

// Respond to changes in the spin button for animation/free running speed
static void adjustspeed(GtkWidget * wg,
                        __attribute__((unused)) const gpointer dt)
{
  set_freeruninterval(gtk_adjustment_get_value(GTK_ADJUSTMENT(wg)));

  // If we are neither animating nor free running, we're done.
  // If we are animating but not free running, we're also done.
  if(!free_running) return;

  if(freeruntimeoutid) g_source_remove(freeruntimeoutid);

  // If we are free running, but not animating, we can (and must) fire off
  // the timer for the next event here.
  if(!animating)
    freeruntimeoutid = g_timeout_add(freeruninterval, to_next_free_run, NULL);

  // If we are animating and free running, things are tricky.  We want
  // to continue the animation at the new speed, but we can't fire off
  // the timer for the next free-run event, or else it might start in the
  // middle of the current event.
  //
  // Instead set a flag that a new timer is needed once the current
  // animation has finished.
  else
    launch_next_freerun_timer_at_draw_end = true;
}

static void close_window()
{
  // We could quit gently:
  // gtk_main_quit(); exit(0);
  // But there is nothing to save, so just drop everything quickly.
  _exit(0);
}

// Sets up the GTK window, add user event hooks, start the necessary
// timer(s), draw the first event.
static void setup()
{
  gtk_init(NULL, NULL);
  GtkWidget * win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win),
                       "NOE: New nOva Event viewer");
  g_signal_connect(win, "delete-event", G_CALLBACK(close_window), 0);

  edarea = gtk_drawing_area_new();
  setboxes();
  g_signal_connect(edarea,"expose-event",G_CALLBACK(draw_event),NULL);
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

  const int initialspeednum = 5;
  GtkObject * const speedadj = gtk_adjustment_new
    (initialspeednum, 1, 11, 1, 1, 0);
  set_freeruninterval(initialspeednum);
  g_signal_connect(speedadj, "value_changed", G_CALLBACK(adjustspeed), NULL);

  GtkWidget * const speedslider
    = gtk_spin_button_new(GTK_ADJUSTMENT(speedadj), 10, 0);

  GtkWidget * speedlabel = gtk_text_view_new();
  GtkTextBuffer * speedlabeltext = gtk_text_buffer_new(0);
  gtk_text_view_set_justification(GTK_TEXT_VIEW(speedlabel), GTK_JUSTIFY_CENTER);
  const char * const speedlabelbuf = "Animation/free run speed";
  gtk_text_buffer_set_text(speedlabeltext, speedlabelbuf, strlen(speedlabelbuf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(speedlabel), speedlabeltext);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(speedlabel), false);

  ueventbox = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(ueventbox), 20);//length of a int64
  gtk_entry_set_width_chars(GTK_ENTRY(ueventbox), 10);
  g_signal_connect(ueventbox, "activate", G_CALLBACK(getuserevent), NULL);

  ueventbut = gtk_button_new_with_mnemonic("_Go to event");
  g_signal_connect(ueventbut, "clicked",  G_CALLBACK(getuserevent), NULL);


  const int nrow = 3+NSTATBOXES, ncol = 9;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);

  {
  int c = 0;
  gtk_table_attach_defaults(GTK_TABLE(tab), prev,             c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), next,             c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), animate_checkbox, c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), cum_ani_checkbox, c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), re_an_button,     c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), freerun_checkbox, c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), speedslider,      c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), ueventbox,        c, c+1, 0, 1); c++;
  gtk_table_attach_defaults(GTK_TABLE(tab), ueventbut,        c, c+1, 0, 1); c++;
  }

  for(int i = 0; i < ncol; i++){
    if(i == 6){
      gtk_table_attach_defaults(GTK_TABLE(tab), speedlabel, i, i+1, 1, 2);
    }
    else{
      // Have to put a blank text box in each table cell or else the
      // background color is inconsistent.
      GtkWidget * blanklabel = gtk_text_view_new();
      gtk_text_view_set_editable(GTK_TEXT_VIEW(blanklabel), false);
      gtk_table_attach_defaults(GTK_TABLE(tab), blanklabel, i, i+1, 1, 2);
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
  draw_event(edarea, NULL, NULL);

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
  }
  else{
    get_event(1);
    g_timeout_add(0, draw_event_from_timer, NULL);
  }
  gtk_main();
}
