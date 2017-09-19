#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include "event.h"

static const int viewsep = 8; // vertical cell widths between x and y views

static const int MAXSTATUS = 1024;

/* The events and the current event */
extern std::vector<nevent> theevents;
static int gevi = 0;

/* GTK objects */
static const int NSTATBOXES = 3;
static GtkWidget * statbox[NSTATBOXES];
static GtkTextBuffer * stattext[NSTATBOXES];
static GtkWidget * edarea = NULL;
static GtkWidget * animate_checkbox = NULL,
                 * cum_ani_checkbox = NULL,
                 * freerun_checkbox = NULL;
static gulong mouseover_handle = 0;

/* Running flags */
static bool ghave_read_all = false;
static bool need_another_event = false;
static bool prefetching = false;
static bool cancel_draw = false;
static bool switch_to_cumulative = false;
static bool animating = false;
static int currenttick = 0;
static int active_plane = -1, active_cell = -1;

/* Ticky boxes flags */
// Animate must start false, because the first thing that happens is that we
// get two expose events (I don't know why) and I don't want to handle an
// animated expose when we haven't drawn yet at all.
//
// Could eliminate these bools and always consult GTK_TOGGLE_BUTTON::active.
// Would that be better?
//
// XXX Still a bug, probably related to the mouseover, that makes the
// animation run multiple times sometimes.
static bool animate = false;
static bool cumulative_animation = true;
static bool free_running = false;

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

/* Update the given status bar to the given text and also process all
 * window events.  */
static void set_status(const int boxn, const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  static char buf[MAXSTATUS];
  vsnprintf(buf, MAXSTATUS-1, format, ap);

  gtk_text_buffer_set_text(stattext[boxn], buf, strlen(buf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[boxn]),
                           stattext[boxn]);

  // Makes performance much better for reasons I don't understand
  while(g_main_context_iteration(NULL, FALSE));
}

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
    const int dist = abs(THEhits[i].cell - c);
    if(dist < mindist){
      mindist = dist;
      closestcell = THEhits[i].cell;
    }
  }

  return closestcell;
}

static void draw_hit(cairo_t * cr, const hit & thishit)
{
  const int screenx = det_to_screen_x(thishit.plane),
            screeny = det_to_screen_y(thishit.plane, thishit.cell);

  float red, green, blue;

  colorhit(thishit.adc, red, green, blue,
           thishit.plane == active_plane && thishit.cell == active_cell);

  cairo_set_source_rgb(cr, red, green, blue);

  cairo_rectangle(cr, screenx+0.5, screeny+0.5,
                      pixx-1,      pixy-1);
  cairo_stroke(cr);
}

static void draw_hits(cairo_t * cr, const bool fullredraw)
{
  cairo_set_line_width(cr, 1.0);

  nevent * THEevent = &theevents[gevi];

  std::sort(THEevent->hits.begin(), THEevent->hits.end(), by_charge);

  for(unsigned int i = 0; i < THEevent->hits.size(); i++){
    hit & thishit = THEevent->hits[i];

    // If we're not animating, we're just going to draw everything no matter
    // what.  Otherwise, we need to know whether to draw just the new stuff
    // or everthing up to the current tick.
    if(animate){
      if(cumulative_animation){
        if(fullredraw){
          if(THEevent->hits[i].tdc > currenttick) continue;
        }
        else{
          if(THEevent->hits[i].tdc != currenttick) continue;
        }
      }
      else if(abs(THEevent->hits[i].tdc - currenttick) > 8){
        continue;
      }
    }

    draw_hit(cr, thishit);
  }
}

static void set_eventn_status0()
{
  if(theevents.empty()){
    set_status(0, "No events in file");
    return;
  }

  nevent * THEevent = &theevents[gevi];
  set_status(0, "Event %'d (%'d/%'d%s in the file)",
    THEevent->nevent, gevi+1,
    (int)theevents.size(), ghave_read_all?"":"+");
}

static void set_eventn_status1()
{
  nevent * THEevent = &theevents[gevi];

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

static void set_eventn_status2()
{
  if(active_plane < 0 || active_cell < 0){
    set_status(2, "%souse over a cell for more information",
      animate || free_running?"Turn off animation and free running and m":"M");
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

static void set_eventn_status()
{
  set_eventn_status0();

  if(theevents.empty()) return;

  set_eventn_status1();
  set_eventn_status2();
}

// Unhighlight the cell that is no longer being mousedover, indicated by
// oldactive_plane/cell, and highlight the new one.  Do this instead of a full
// redraw of edarea, which is expensive and causes very noticable lag for the
// FD. This should only be called when we are not animating, since it does not
// check the hit times (although this would be a straightforward extension).
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

static gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee,
                           __attribute__((unused)) gpointer data)
{
  set_eventn_status();
  if(theevents.empty()) return TRUE;

  static int drawn = 0;
  const bool exposed = ee != NULL;
  if(!exposed) drawn++;

  nevent * THEevent = &theevents[gevi];

  if(!isfd && THEevent->fdlike) setfd();

  cancel_draw = false;

  const int thisfirsttick = !animate? THEevent->maxtick
                          : exposed ? currenttick
                          :           THEevent->mintick,
            thislasttick =  !animate?THEevent->maxtick
                          : exposed ? currenttick
                          :           THEevent->maxtick;

  for(currenttick = thisfirsttick; currenttick <= thislasttick; currenttick+=4){

    set_eventn_status();

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

    if(animate && currenttick != THEevent->maxtick){
      usleep(15e3);
      animating = true;

      // Check if we have started drawing another event while
      // still in here.  If so, don't keep drawing this one.
      const int thisdrawn = drawn;
      while(g_main_context_iteration(NULL, FALSE));
      if(cancel_draw || drawn != thisdrawn) break;
    }
  }

  // If we are moving to a new detector event, make sure we don't keep trying
  // to draw this detector event.  However, if we're here because of an X
  // expose event, we do want to keep going.
  if(!exposed){
    cancel_draw = true;
    animating = false;
  }

  return FALSE;
}

// Used, e.g., with g_timeout_add() to get an event drawn after re-entering the
// GTK main loop.
static gboolean draw_event_from_timer(gpointer data)
{
  draw_event(edarea, NULL, data);
  return FALSE; // don't call me again
}

// Return true if the event is ready to be drawn.  Otherwise, we will have
// to fetch it and call draw_event() later.
static bool get_event(const int change)
{
  if(gevi+change >= (int)theevents.size()){
    if(ghave_read_all){
      gevi = theevents.size() - 1;
    }
    else{
      // TODO make this work with abs(change) > 1
      // XXX this is great except that if events happen while not
      // in the GTK loop, we get an assertion failure on the console.
      // Maybe it's harmless, or nearly harmless, in that we just lose
      // the user input.  Not sure.  It more or less seems to work.
      need_another_event = true;
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

__attribute__((unused)) static gboolean
fetch_an_event(__attribute__((unused)) gpointer data)
{
  if(ghave_read_all) return FALSE; // don't call this again

  if(animating || gtk_events_pending()){
    return TRUE;
  }

  // exit GTK event loop to get another event from art
  prefetching = true;
  gtk_main_quit();
  return TRUE;
}

/** Display the next or previous event. */
static void to_next(__attribute__((unused)) GtkWidget * widget,
                    gpointer data)
{
  cancel_draw = true;
  active_cell = active_plane = -1;

  const bool * const forward = (const bool * const)data;
  if(get_event((*forward)?1:-1))
    draw_event(edarea, NULL, NULL);
}

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

static gboolean mouseover(__attribute__((unused)) GtkWidget * widg,
                          GdkEventMotion * gevent,
                          __attribute__((unused)) gpointer data)
{
  if(gevent == NULL) return TRUE; // shouldn't happen
  if(animate) return TRUE; // Too hard to handle, and unlikely
                           // that the user wants it anyway.
  if(theevents.empty()) return TRUE; // No coordinates in this case

  const int oldactive_plane = active_plane;
  const int oldactive_cell  = active_cell;

  active_plane = screen_to_plane((int)gevent->x, (int)gevent->y);
  active_cell  = screen_to_cell ((int)gevent->x, (int)gevent->y);

  change_highlighted_cell(edarea, oldactive_plane, oldactive_cell);
  set_eventn_status2();

  return TRUE;
}

static void sub_toggle_mouseover()
{
  // Don't listen for mouseovers while animating or free running.  Otherwise,
  // with animation, we get into odd situations that cause the animation to
  // loop many times even if we try to ignore the signals at a higher level.
  // With free running, we draw the current event many times and appear to be
  // stuck.
  if(free_running || animate){
    if(mouseover_handle > 0) g_signal_handler_disconnect(edarea, mouseover_handle);
    mouseover_handle = active_plane = active_cell = -1;
  }
  else{
    mouseover_handle =
      g_signal_connect(edarea, "motion-notify-event", G_CALLBACK(mouseover), NULL);
  }
}

static void toggle_freerun(__attribute__((unused)) GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  free_running = GTK_TOGGLE_BUTTON(w)->active;
  sub_toggle_mouseover();
  if(free_running)
    g_timeout_add(100 /* ms */, to_next_free_run, NULL);
}

static void toggle_cum_ani(GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  cumulative_animation = GTK_TOGGLE_BUTTON(w)->active;

  // Must note that we've switched to trigger a full redraw
  if(cumulative_animation) switch_to_cumulative = true;
}

static void toggle_animate(GtkWidget * w, __attribute__((unused)) gpointer dt)
{
  animate = GTK_TOGGLE_BUTTON(w)->active;
  sub_toggle_mouseover();
  // Schedule the draw for the next time the main event loop runs instead of
  // drawing immediately because otherwise, the checkbox doesn't appear checked
  // until the user moves the mouse off of it.  I don't really understand how
  // the problem comes about, but this fixes it.  It is not a problem for
  // any of the other checkboxes.
  g_timeout_add(0, draw_event_from_timer, w);
}

static void close_window()
{
  gtk_main_quit();
  exit(0);
}

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
  mouseover_handle =
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

  g_signal_connect(animate_checkbox, "toggled", G_CALLBACK(toggle_animate), edarea);
  g_signal_connect(cum_ani_checkbox, "toggled", G_CALLBACK(toggle_cum_ani), edarea);
  g_signal_connect(freerun_checkbox, "toggled", G_CALLBACK(toggle_freerun), edarea);

  const int nrow = 2+NSTATBOXES, ncol = 5;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);
  gtk_table_attach_defaults(GTK_TABLE(tab), prev,             0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), next,             1, 2, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), animate_checkbox, 2, 3, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), cum_ani_checkbox, 3, 4, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), freerun_checkbox, 4, 5, 0, 1);
  for(int i = 0; i < NSTATBOXES; i++){
    statbox[i]  = gtk_text_view_new();
    stattext[i] = gtk_text_buffer_new(0);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[i]), stattext[i]);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(statbox[i]), false);
    gtk_table_attach_defaults(GTK_TABLE(tab), statbox[i], 0, ncol, 1+i, 2+i);
  }
  gtk_table_attach_defaults(GTK_TABLE(tab), edarea, 0, ncol, 4, nrow);

  // This isn't the size I want, but along with requesting the size of the
  // edarea widget, it has the desired effect, at least more or less.
  gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);

  gtk_widget_show_all(win);

  get_event(0);
  draw_event(edarea, NULL, NULL);

  if(!ghave_read_all)
    g_timeout_add(20 /* ms */, fetch_an_event, NULL);
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
