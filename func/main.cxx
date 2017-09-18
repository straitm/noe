#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include "event.h"

static const int viewsep = 8; // vertical cell widths between x and y views

// We're going to assume the ND until we see a hit that indicates it's FD
static bool isfd = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static const int NDnplanes_perview = 8 * 12 + 11,
                 NDfirst_mucatcher = 8 * 24,
                 NDncells_perplane = 3 * 32;
static const int FDnplanes_perview = 16 * 28,
                 FDfirst_mucatcher = 9999, // i.e. no muon catcher
                 FDncells_perplane = 12 * 32;

// Want the nearest small integer box that has an aspect ratio close to 3.36:1.
// Options: 3:1, 2:7, 3:10, 4:13, 5:17, etc.
static const int FDpixx = 3, FDpixy = 1;
static const int NDpixx = 10, NDpixy = 3;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
static int nplanes_perview = NDnplanes_perview,
           first_mucatcher = NDfirst_mucatcher,
           ncells_perplane = NDncells_perplane;

static int pixx = NDpixx, pixy = NDpixy;

static int nplanes = 2*nplanes_perview;
static int ybox, xboxnomu, yboxnomu, xbox;

static GtkWidget * edarea = NULL;

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

extern std::vector<nevent> theevents;


static GtkWidget * statbox[3];
static GtkTextBuffer * stattext[3];

static bool ghave_read_all = false;
static bool need_another_event = false;
static bool prefetching = false;
static bool cancel_draw = false;
static bool switch_to_cumulative = false;

static bool animating = false;
static int currenttick = 0;

static int gevi = 0;

// Must start false, because the first thing that happens is that we get two
// expose events (I don't know why) and I don't want to handle an animated
// expose when we haven't drawn yet at all.
static bool animate = false;

static bool cumulative_animation = true;
static bool free_running = false;

/* Update the given status bar to the given text and also process all
 * window events.  */
static void set_status(const int boxn, const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  static char buf[1024];
  vsnprintf(buf, 1023, format, ap);

  gtk_text_buffer_set_text(stattext[boxn], buf, strlen(buf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[boxn]),
                           stattext[boxn]);
  while(g_main_context_iteration(NULL, FALSE));
}

static void colorhit(const int32_t adc, float & red, float & green, float & blue)
{
  // Oh so hacky!
  const float graycut = 60;
  const float nextcut = 120;
  if(adc < graycut) red = green = blue = 0.5*adc/graycut;
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

// Given a screen position, returns the plane number. Any x position within
// the boundaries of hits displayed in a plane, including the right and left
// pixels, will return the same plane number.  In the muon catcher, return the
// nearest plane in the view if the screen position is in dead material.
// If the screen position is outside the detector boxes, return the plane for
// the closer box.
static int screen_to_plane(const int x, const int y)
{
  return screen_y_to_xview(y);
}

// Given a screen position, returns the cell number.  If no cell is in this
// position, return the number of the closest cell in screen y coordinates,
// i.e. the closest cell that is directly above or below the screen position,
// even if the closest one is in another direction.
static int screen_to_cell(const int x, const int y)
{
  return 0;
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

    const int screenx = det_to_screen_x(thishit.plane),
              screeny = det_to_screen_y(thishit.plane, thishit.cell);

    float red, green, blue;

    colorhit(thishit.adc, red, green, blue);

    cairo_set_source_rgb(cr, red, green, blue);

    cairo_rectangle(cr, screenx+0.5, screeny+0.5,
                        pixx-1,      pixy-1);
    cairo_stroke(cr);
  }
}

static void set_eventn_status0()
{
  nevent * THEevent = &theevents[gevi];
  set_status(0, "Event %'d (%'d/%'d%s in the file)",
    THEevent->nevent, gevi+1,
    (int)theevents.size(), ghave_read_all?"":"+");
}

static void set_eventn_status(const int currenttick)
{
  nevent * THEevent = &theevents[gevi];

  set_eventn_status0();
  set_status(1, "Ticks %'d through %'d, %d hits",
             THEevent->mintick, THEevent->maxtick,
             (int)THEevent->hits.size());
  if(!animate)
    set_status(2, "Showing all ticks");
  else if(cumulative_animation)
    set_status(2, "Showing ticks %d through %d (%.3f us)", THEevent->mintick,
               currenttick, currenttick/64.);
  else
    set_status(2, "Showing tick %d (%.3f us)", currenttick,
               currenttick/64.);
}

static gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee,
                           __attribute__((unused)) gpointer data)
{
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

    set_eventn_status(currenttick);

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
      if(cancel_draw) break;
      if(drawn != thisdrawn){
        printf("Cancelling draw %d, since another one has started\n", thisdrawn);
        break;
      }
    }
  }

  // If we are moving to a new detector event, make sure we don't keep trying
  // to draw this detector event.  However, if we're here because of an X
  // expose event, we do want to keep going.
  if(ee == NULL){
    cancel_draw = true;
    animating = false;
  }

  return FALSE;
}

// Used with g_timeout_add() to get an event drawn after re-entering the
// GTK main loop.
static gboolean draw_event_from_timer(gpointer data)
{
  draw_event(edarea, NULL, data);
  return FALSE; // don't call me again
}

struct butpair{
  GtkWidget * next, * prev;
};

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

  const bool * const forward = (const bool * const)data;
  if(get_event((*forward)?1:-1))
    draw_event(edarea, NULL, NULL);
}

static gboolean to_next_free_run(__attribute__((unused)) gpointer data)
{
  bool forward = true;
  to_next(NULL, &forward);
  return free_running;
}

static butpair mkbutton(char * label)
{
  struct butpair thepair;
  static char labelbuf[1024];

  snprintf(labelbuf, 1023, "Next %s", label);
  thepair.next = gtk_button_new_with_label(labelbuf);
  g_signal_connect(thepair.next, "clicked", G_CALLBACK(to_next),
                   new bool(true));

  snprintf(labelbuf, 1023, "Previous %s", label);
  thepair.prev = gtk_button_new_with_label(labelbuf);
  g_signal_connect(thepair.prev, "clicked", G_CALLBACK(to_next),
                  new bool(false));
  return thepair;
}

static void toggle_freerun(__attribute__((unused)) GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  free_running = GTK_TOGGLE_BUTTON(w)->active;

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

static void toggle_animate(__attribute__((unused)) GtkWidget * w, gpointer dt)
{
  animate = GTK_TOGGLE_BUTTON(w)->active;
  draw_event((GtkWidget *)dt, NULL, NULL);
}

static gboolean mouseover(__attribute__((unused)) GtkWidget * widg,
                          GdkEventMotion * gevent,
                          __attribute__((unused)) gpointer data)
{
  if(gevent == NULL) return TRUE; // shouldn't happen

  // TODO something more interesting, like printing the hit's time and ADC
  printf("%f %f\n", gevent->x, gevent->y);

  return TRUE;
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

  butpair npbuts = mkbutton((char *)"Event");

  const int nrow = 5, ncol = 5;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);

  GtkWidget * animate_checkbox =
    gtk_check_button_new_with_mnemonic("_Animate");
  GtkWidget * cum_ani_checkbox =
    gtk_check_button_new_with_mnemonic("_Cumulative animation");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cum_ani_checkbox),
                               cumulative_animation);
  GtkWidget * freerun_checkbox =
    gtk_check_button_new_with_mnemonic("_Free running");

  for(int i = 0; i < 3; i++){
    statbox[i]  = gtk_text_view_new();
    stattext[i] = gtk_text_buffer_new(0);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[i]), stattext[i]);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(statbox[i]), false);
  }

  g_signal_connect(animate_checkbox, "toggled", G_CALLBACK(toggle_animate), edarea);
  g_signal_connect(cum_ani_checkbox, "toggled", G_CALLBACK(toggle_cum_ani), edarea);
  g_signal_connect(freerun_checkbox, "toggled", G_CALLBACK(toggle_freerun), edarea);

  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.prev,      0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.next,      1, 2, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), animate_checkbox, 2, 3, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), cum_ani_checkbox, 3, 4, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), freerun_checkbox, 4, 5, 0, 1);
  for(int i = 0; i < 3; i++)
    gtk_table_attach_defaults(GTK_TABLE(tab), statbox[i], 0, ncol, 1+i, 2+i);
  gtk_table_attach_defaults(GTK_TABLE(tab), edarea, 0, ncol, 4, nrow);
  g_signal_connect(edarea, "motion-notify-event", G_CALLBACK(mouseover), NULL);
  gtk_widget_set_events(edarea, gtk_widget_get_events(edarea)
                                | GDK_POINTER_MOTION_MASK);
  gtk_widget_show_all(win);

  get_event(0);
  draw_event(edarea, NULL, NULL);

  g_timeout_add(20 /* ms */, fetch_an_event, NULL);
}

void realmain(const bool have_read_all)
{
  if(have_read_all) ghave_read_all = true;
  static bool first = true;
  if(first){
    first = false;
    setup();
  }
  else{
    if(prefetching){
      set_eventn_status0();
    }
    else{
      get_event(1);
      g_timeout_add(0, draw_event_from_timer, NULL);
    }
  }
  gtk_main();
}
