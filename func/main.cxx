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
======================================================================

TODO:

* Animate by TNS times instead of TDC.  Be able to switch between?

* Adapt to the window size intelligently.

*/

#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include "drawing.h"
#include "event.h"
#include "geo.h"
#include "tracks.h"
#include "hits.h"
#include "status.h"
#include "zoompan.h"

// Let's see.  I believe both detectors read out in increments of 4 TDC units,
// but the FD is multiplexed whereas the ND isn't, so any given channel at the
// FD can only report every 4 * 2^N TDC units, where I think N = 2.
//
// TODO: be more clear with the user about what is being displayed when
// TDCSTEP != 1
static int TDCSTEP = 4;

/* The events and the current event index in the vector */
extern std::vector<noeevent> theevents;
int gevi = 0;

int active_plane = -1, active_cell = -1;

extern int first_mucatcher, ncells_perplane;
extern int nplanes;

/* GTK objects owned elsewhere */
extern GtkWidget * statbox[NSTATBOXES];
extern GtkTextBuffer * stattext[NSTATBOXES];

/* GTK objects owned here */
static GtkWidget * win = NULL;
extern GtkWidget * edarea[2]; // X and Y views
static GtkWidget * animate_checkbox = NULL,
                 * cum_ani_checkbox = NULL,
                 * freerun_checkbox = NULL;
static GtkWidget * ueventbut = NULL;
static GtkWidget * ueventbox = NULL;
static GtkWidget * mintickslider = NULL;
static GtkWidget * maxtickslider = NULL;
static GtkObject * speedadj = NULL;

/* Running flags.  */
bool ghave_read_all = false;
static bool prefetching = false;
static bool adjusttick_callback_inhibit = false; // XXX ug

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
static gulong statmsgtimeoutid = 0;

static bool visible_hit(const int32_t tdc)
{
  return tdc <= theevents[gevi].current_maxtick &&
         tdc >= theevents[gevi].current_mintick - (TDCSTEP-1);
}

// Unhighlight the cell that is no longer being moused over, indicated by
// oldactive_plane/cell, and highlight the new one.  Do this instead of a full
// redraw of edarea, which is expensive and causes very noticeable lag for the
// FD.
static void change_highlighted_cell(const int oldactive_plane,
                                    const int oldactive_cell)
{
  cairo_t * cr[kXorY];
  for(int i = 0; i < kXorY; i++){
    cr[i] = gdk_cairo_create(edarea[i]->window);
    cairo_set_line_width(cr[i], 1.0);
  }

  std::vector<hit> & THEhits = theevents[gevi].hits;

  // We may need to find any number of hits since more than one hit
  // can be in the same cell.  It is not guaranteed that the same hit
  // ends up visible, but I'm just going to live with that.
  for(unsigned int i = 0; i < THEhits.size(); i++){
    hit & thishit = THEhits[i];
    if((thishit.plane == oldactive_plane && thishit.cell == oldactive_cell) ||
       (thishit.plane ==    active_plane && thishit.cell ==    active_cell))
      draw_hit(cr[thishit.plane%2 == 1?kX:kY], thishit, edarea);
  }

  // NOTE: In principle we should redraw tracks here since we may have just
  // stomped on some.  However, in practice the visual effect isn't very
  // noticeable and since it's kinda a pain to do it from here, we'll skip it.

  for(int i = 0; i < kXorY; i++) cairo_destroy(cr[i]);
}

// Given a screen position, returns the cell number.  If no hit cell is in this
// position, return the number of the closest cell in screen y coordinates,
// i.e. the closest hit cell that is directly above or below the screen position,
// even if the closest one is in another direction.  But if this position is
// outside the detector boxes on the right or left, return -1.
static int screen_to_activecell(noe_view_t view, const int x, const int y)
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
    if(!visible_hit(THEhits[i].tdc)) continue;
    const int dist = abs(THEhits[i].cell - c);
    if(dist < mindist){
      mindist = dist;
      closestcell = THEhits[i].cell;
    }
  }

  return closestcell;
}

// draw_event and to_next_free_run circularly refer to each other...
static gboolean to_next_free_run(__attribute__((unused)) gpointer data);

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

  drawpars.firsttick = E.current_maxtick-(TDCSTEP-1);
  drawpars.lasttick  = E.current_maxtick;
  draw_event(&drawpars);

  const bool stillanimating =
    animate && E.current_maxtick < theevents[gevi].user_maxtick;

  // If we are animating and free running, go directly to the next event
  // at the end of this one, not worrying about the free run delay. This
  // avoids the need to switch back and forth between timers. Keep the
  // animation timer running unless this was the last event, as signaled
  // by the return value of to_next_free_run().
  if(!stillanimating && free_running)
    return to_next_free_run(NULL);

  return stillanimating;
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

    // Use a priority lower than G_PRIORITY_HIGH_IDLE + 10, which is
    // what GTK uses for resizing.  This means that even while animating
    // furiously, we'll still be responsive to window resizes.
    animatetimeoutid = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE,
      std::max(1, (int)animationinterval), animation_step, NULL, NULL);
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
      // NOTE: this does not work with abs(change) > 1, but it doesn't
      // matter since we never call get_event with a bigger number.
      //
      // NOTE: Leaving the GTK loop *seems* to be OK, but I'm not clear
      // on what happens when user events arrive when we're outside.
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
      set_status(3, "Entered event invalid or not available. I have "
                 "events %d through %d%s%s",
                 theevents[0].nevent,
                 theevents[theevents.size()-1].nevent,
                 theevents[theevents.size()-1].nevent-theevents[0].nevent
                 == theevents.size()-1?"":" (not consecutive)",
                 ghave_read_all?"":". I'm still loading events.");
    if(statmsgtimeoutid) g_source_remove(statmsgtimeoutid);
    statmsgtimeoutid = g_timeout_add(8e3, clear_error_message, NULL);
    return;
  }

  if(userevent == (int)theevents[gevi].nevent) return;

  const bool forward = userevent > (int)theevents[gevi].nevent;
  while(userevent != (int)theevents[gevi].nevent)
    // Don't go through get_event because we do *not* want to try
    // getting more events from the file
    gevi += (forward?1:-1);

  prepare_to_swich_events();
  handle_event();
}

// Handle the mouse arriving at a spot in the drawing area. Find what cell the
// user is pointing at, highlight it, and show information about it.
static gboolean mouseover(GtkWidget * widg, GdkEventMotion * gevent,
                          __attribute__((unused)) gpointer data)
{
  if(gevent == NULL) return TRUE; // shouldn't happen
  if(theevents.empty()) return TRUE; // No coordinates in this case

  const noe_view_t V = widg == edarea[kX]? kX: kY;

  if(gevent->state & GDK_BUTTON1_MASK){
    dopanning(V, gevent);
    return TRUE;
  }

  const int oldactive_plane = active_plane;
  const int oldactive_cell  = active_cell;

  active_plane = screen_to_plane     (V, (int)gevent->x);
  active_cell  = screen_to_activecell(V, (int)gevent->x, (int)gevent->y);

  change_highlighted_cell(oldactive_plane, oldactive_cell);
  set_eventn_status2();

  return TRUE;
}

static void stop_freerun_timer()
{
  if(freeruntimeoutid) g_source_remove(freeruntimeoutid);
  freeruntimeoutid = 0;
}

static void start_freerun_timer()
{
  stop_freerun_timer(); // just in case

  // Do not call g_timeout_add with an interval of zero, since that
  // seems to be a special case that causes the function to be run
  // repeatedly *without* returning to the main loop, or without
  // doing all the things in the main loop that are usually done, or
  // something. In any case, empirically, it causes multiple (infinite?)
  // calls to to_next_free_run after gtk_main_quit() has been called,
  // which locks us up. It is quite possible that by setting the
  // interval to 1, it only makes it *unlikely* that further events are
  // processed between gtk_main_quit() and exiting the main loop, in
  // which case I have a bug that has only been suppressed instead of
  // fixed.
  //
  // See comments on the animation timer for why we use this priority.
  freeruntimeoutid = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE,
    std::max((gulong)1, freeruninterval), to_next_free_run, NULL, NULL);
}

// Handle the user clicking the "run freely" check box.
static void toggle_freerun(__attribute__((unused)) GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  free_running = GTK_TOGGLE_BUTTON(w)->active;

  // If free running *and* animating, the animation timer will handle
  // switching to the next event.
  if(free_running && !animate) start_freerun_timer();
  else                         stop_freerun_timer();

  handle_event();
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

// Convert the abstract "speed" number from the user into a delay.
static void set_intervals(const int speednum)
{
  freeruninterval = (int)pow(10, 6.5 - speednum/2.0);

  // No point in trying to go faster than ~50Hz since the monitor won't
  // keep up (to say nothing of the human eye). Control speeds of 6 and
  // higher exclusively with the TDCSTEP. This has the added benefit of
  // putting several ticks on the screen at once, which makes it easier
  // to see interesting things.
  animationinterval = std::max(20, (int)pow(10, 5.0 - speednum/2.0));

  switch(speednum < 1?1:speednum > 11?11:speednum){
    case  1: TDCSTEP =    1; break;
    case  2: TDCSTEP =    2; break;
    case  3: TDCSTEP =    4; break;
    case  4: TDCSTEP =    4; break;
    case  5: TDCSTEP =    8; break;
    case  6: TDCSTEP =   32; break;
    case  7: TDCSTEP =   64; break;
    case  8: TDCSTEP =  128; break;
    case  9: TDCSTEP =  256; break;
    case 10: TDCSTEP =  512; break;
    case 11: TDCSTEP = 2048; break;
  }
}

// Handle the user clicking the "animate" check box.
static void toggle_animate(GtkWidget * w, __attribute__((unused)) gpointer dt)
{
  animate = GTK_TOGGLE_BUTTON(w)->active;
  if(animate){
    // If free running *and* animating, the animation timer will handle
    // switching to the next event.
    stop_freerun_timer();
    set_intervals(gtk_adjustment_get_value(GTK_ADJUSTMENT(speedadj)));
  }
  else{
    // If we stop animating, the free run timer has to take over free running
    if(free_running) start_freerun_timer();
    TDCSTEP = 1;
  }

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
  if(free_running && !animate) start_freerun_timer();

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
static gboolean redraw_window(GtkWidget * win, GdkEventConfigure * event,
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

static GtkWidget * make_speedslider()
{
  const int initialspeednum = 6;
  speedadj = gtk_adjustment_new (initialspeednum, 1, 11, 1, 1, 0);
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

  g_signal_connect(win,"configure-event",G_CALLBACK(redraw_window),NULL);
  for(int i = 0; i < kXorY; i++){
    edarea[i] = gtk_drawing_area_new();
    g_signal_connect(edarea[i],"expose-event",G_CALLBACK(redraw_event),NULL);
    g_signal_connect(edarea[i], "motion-notify-event", G_CALLBACK(mouseover), NULL);
    g_signal_connect(edarea[i], "scroll-event", G_CALLBACK(dozooming), new bool(i));
    g_signal_connect(edarea[i], "button-press-event",
                     G_CALLBACK(mousebuttonpress), NULL);
    gtk_widget_set_events(edarea[i], gtk_widget_get_events(edarea[i])
                                     | GDK_POINTER_MOTION_HINT_MASK
                                     | GDK_POINTER_MOTION_MASK
                                     | GDK_BUTTON_PRESS_MASK
                                     | GDK_SCROLL_MASK);
  }
  setboxes();
  request_edarea_size();

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

  g_signal_connect(animate_checkbox, "toggled", G_CALLBACK(toggle_animate),NULL);
  g_signal_connect(cum_ani_checkbox, "toggled", G_CALLBACK(toggle_cum_ani),NULL);
  g_signal_connect(freerun_checkbox, "toggled", G_CALLBACK(toggle_freerun),NULL);

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

  const int nrow = 5+NSTATBOXES, ncol = 11;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);

  GtkWidget * top_row_widgets[ncol] = {
    prev, next, mintickslider, maxtickslider, animate_checkbox, cum_ani_checkbox,
    re_an_button, freerun_checkbox, speedslider, ueventbox, ueventbut};

  GtkWidget * second_row_widgets[ncol] = {
    NULL, NULL, minticklabel, maxticklabel, NULL,
    NULL, NULL, NULL, speedlabel, NULL, NULL};

  for(int c = 0; c < ncol; c++){
    gtk_table_attach(GTK_TABLE(tab), top_row_widgets[c], c, c+1, 0, 1,
      GtkAttachOptions(GTK_EXPAND | GTK_FILL), GTK_SHRINK, 0, 0);

    if(second_row_widgets[c] != NULL){
      gtk_table_attach(GTK_TABLE(tab),second_row_widgets[c],c,c+1,1,2,
        GtkAttachOptions(GTK_EXPAND | GTK_FILL), GTK_SHRINK, 0, 0);
    }
    else{
      // Have to put a blank text box in each table cell or else the
      // background color is inconsistent.
      GtkWidget * blanklabel = gtk_text_view_new();
      gtk_text_view_set_editable(GTK_TEXT_VIEW(blanklabel), false);
      gtk_table_attach(GTK_TABLE(tab), blanklabel, c, c+1, 1, 2,
        GtkAttachOptions(GTK_EXPAND | GTK_FILL), GTK_SHRINK, 0, 0);
    }
  }

  for(int i = 0; i < NSTATBOXES; i++){
    statbox[i]  = gtk_text_view_new();
    stattext[i] = gtk_text_buffer_new(0);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[i]), stattext[i]);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(statbox[i]), false);
    gtk_table_attach(GTK_TABLE(tab), statbox[i], 0, ncol, 2+i, 3+i,
      GtkAttachOptions(GTK_EXPAND | GTK_FILL), GTK_SHRINK, 0, 0);
  }

  for(int i = 0; i < kXorY; i++)
    gtk_table_attach(GTK_TABLE(tab), edarea[i], 0, ncol,
                     2+NSTATBOXES+i*2, 3+NSTATBOXES+i*2, // too clever
                     GtkAttachOptions(GTK_EXPAND | GTK_FILL),
                     GtkAttachOptions(GTK_EXPAND | GTK_FILL), 0, 0);

  gtk_table_attach(GTK_TABLE(tab), gtk_hseparator_new(), 0, ncol,
                   3+NSTATBOXES, 4+NSTATBOXES,
                   GtkAttachOptions(GTK_EXPAND | GTK_FILL),
                   GtkAttachOptions(GTK_SHRINK), 0, 0);

  // This isn't the size I want, but along with requesting the size of the
  // edarea widgets, it has the desired effect, at least more or less.
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
