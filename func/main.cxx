#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include "event.h"

static const int viewsep = 8; // vertical pixels between x and y views

// Want the nearest small integer box that has an aspect ratio close to 3.36:1.
// The options would seem to be 3:1 or 7:2.  7:2 makes the detector 3136 pixels
// wide, which is a bit much, so 3:1 it is, I guess.
const int pixx = 3, pixy = 1;
//const int pixx = 7, pixy = 2;
//const int pixx = 10, pixy = 3;

//static const int nplanes_perview = 8 * 12 + 11,
//                 first_mucatcher = 8 * 24,
//                 ncells_perplane = 3 * 32;
static const int nplanes_perview = 16 * 28,
                 first_mucatcher = 9999,
                 ncells_perplane = 12 * 32;

static const int nplanes = 2*nplanes_perview;

static const int ybox = ncells_perplane*pixy,
                 xboxnomu = pixx*(first_mucatcher/2),
                 yboxnomu = ybox/3, // 'cause it is.
                 xbox = pixx*(nplanes_perview +
                              (first_mucatcher < nplanes?
                               nplanes_perview - first_mucatcher/2: 0));

GtkWidget * edarea = NULL;
static GtkWidget * statbox[3];
static GtkTextBuffer * stattext[3];

static bool ghave_read_all = false;
static bool need_another_event = false;
static bool prefetching = false;
static bool cancel_draw = false;
static bool animating = false;
extern std::vector<nevent> theevents;

static int gevi = 0;

static bool can_animate = false;
static bool cumulative_animation = false;

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

  cairo_rectangle(cr, 0.5, 0.5, xbox+1, ybox+1);
  cairo_stroke(cr);

  cairo_rectangle(cr, 0.5, 0.5 + ybox + viewsep*pixy, xbox+1, ybox+1);
  cairo_stroke(cr);

  if(first_mucatcher < nplanes){
    cairo_rectangle(cr, 1.5 + xboxnomu, 0.5 + ybox + viewsep*pixy,
                        xbox-xboxnomu, yboxnomu);
    cairo_stroke(cr);
  }
}

static void draw_hits(cairo_t * cr, const int maxtick)
{
  cairo_set_line_width(cr, 1.0);

  nevent * THEevent = &theevents[gevi];

  std::sort(THEevent->hits.begin(), THEevent->hits.end(), by_charge);

  for(unsigned int i = 0; i < THEevent->hits.size(); i++){
    hit & thishit = THEevent->hits[i];
    if(can_animate){
      if( cumulative_animation && THEevent->hits[i].tdc >  maxtick) continue;
      if(!cumulative_animation && abs(THEevent->hits[i].tdc - maxtick) > 8) continue;
    }

    const int x = 1 + pixx*((thishit.plane
                  +(thishit.plane > first_mucatcher?thishit.plane-first_mucatcher:0))/2),
      // put y view on the bottom
      y = pixy*(ncells_perplane*2 + viewsep - thishit.cell
          - (thishit.plane%2)*(ncells_perplane + viewsep)) - (pixy-1);

    float red, green, blue;

    colorhit(thishit.adc, red, green, blue);

    cairo_set_source_rgb(cr, red, green, blue);

    cairo_rectangle(cr, x+0.5   , y+0.5,
                        pixx-1, pixy-1);
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
  if(!can_animate)
    set_status(2, "Showing all ticks");
  else
    set_status(2, "Showing tick %d (%.3f us)", currenttick,
               currenttick/64.);
}

static gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee,
                           __attribute__((unused)) gpointer data)
{
  // Animate only if drawing for the first time, not if exposed.
  const bool animate = !ee && can_animate;

  static int drawn = 0;
  if(!ee) drawn++;

  printf("draw_event() %d exposed? %s\n", drawn, ee == NULL?"no":"yes");

  nevent * THEevent = &theevents[gevi];

  cancel_draw = false;
  for(int ti = animate?THEevent->mintick:THEevent->maxtick;
      ti <= THEevent->maxtick;
      ti+=4){

    set_eventn_status(ti);

    cairo_t * cr = gdk_cairo_create(widg->window);
    cairo_push_group(cr);

    draw_background(cr);
    draw_hits(cr, ti);

    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
    cairo_destroy(cr);

    if(animate && ti != THEevent->maxtick){
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
  cancel_draw = true;
  animating = false;

  printf("Done drawing\n");

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
  printf("to_next()\n");
  cancel_draw = true;

  const bool * const forward = (const bool * const)data;
  if(get_event((*forward)?1:-1))
    draw_event(edarea, NULL, NULL);
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

static void toggle_cum_ani(__attribute__((unused)) GtkWidget * w,
                           __attribute__((unused)) gpointer dt)
{
  cumulative_animation = !cumulative_animation;
}

static void toggle_animate(__attribute__((unused)) GtkWidget * w, gpointer dt)
{
  can_animate = !can_animate;
  draw_event((GtkWidget *)dt, NULL, NULL);
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
  gtk_widget_set_size_request(edarea,
                              xbox + 2,
                              ybox*2 + viewsep*pixy + 2);
  g_signal_connect(edarea,"expose-event",G_CALLBACK(draw_event),NULL);

  butpair npbuts = mkbutton((char *)"Event");

  const int nrow = 5, ncol = 4;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);

  GtkWidget * animate_checkbox =
    gtk_check_button_new_with_mnemonic("_Animate");
  GtkWidget * cum_ani_checkbox =
    gtk_check_button_new_with_mnemonic("_Cumulative animation");

  for(int i = 0; i < 3; i++){
    statbox[i]  = gtk_text_view_new();
    stattext[i] = gtk_text_buffer_new(0);
    gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[i]), stattext[i]);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(statbox[i]), false);
  }

  g_signal_connect(animate_checkbox, "toggled", G_CALLBACK(toggle_animate), edarea);
  g_signal_connect(cum_ani_checkbox, "toggled", G_CALLBACK(toggle_cum_ani), edarea);

  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.prev,      0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.next,      1, 2, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), animate_checkbox, 2, 3, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), cum_ani_checkbox, 3, 4, 0, 1);
  for(int i = 0; i < 3; i++)
    gtk_table_attach_defaults(GTK_TABLE(tab), statbox[i], 0, ncol, 1+i, 2+i);
  gtk_table_attach_defaults(GTK_TABLE(tab), edarea, 0, ncol, 4, nrow);
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
