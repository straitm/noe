#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <string.h>
#include "event.h"

const int viewsep = 8; // vertical pixels between x and y views

const int nplanes_perview = 16 * 28,
          ncells_perplane = 12 * 32;

// Want the nearest small integer box that has an aspect ratio close to 3.36:1.
// The options would seem to be 3:1 or 7:2.  7:2 makes the detector 3136 pixels
// wide, which is a bit much, so 3:1 it is, I guess.
const int pixx = 3,
          pixy = 1;

GtkWidget * edarea = NULL;
static GtkWidget * statbox[3];
static GtkTextBuffer * stattext[3];

extern std::vector<nevent> theevents;

static nevent THEevent;

static bool can_animate = false; // TODO: set to a ticky box
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

void colorhit(const int32_t adc, float & red, float & green, float & blue)
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

  cairo_rectangle(cr, 0.5, 0.5,
    nplanes_perview*pixx+1, ncells_perplane+1);
  cairo_stroke(cr);

  cairo_rectangle(cr, 0.5, 0.5 + ncells_perplane + viewsep,
    nplanes_perview*pixx+1, ncells_perplane+1);
  cairo_stroke(cr);
}

static void draw_hits(cairo_t * cr, nevent * MYevent, const int maxtick)
{
  cairo_set_line_width(cr, 1.0);

  std::sort(MYevent->hits.begin(), MYevent->hits.end(), by_charge);

  for(unsigned int i = 0; i < MYevent->hits.size(); i++){
    hit & thishit = MYevent->hits[i];

    if(can_animate){
      if( cumulative_animation && MYevent->hits[i].tdc >  maxtick) continue;
      if(!cumulative_animation && abs(MYevent->hits[i].tdc - maxtick) > 8) continue;
    }

    const int x = pixx*(thishit.plane/2) + 1,
      // put y view on the bottom
      y = ncells_perplane*pixy*2 + viewsep - thishit.cell
          - (thishit.plane%2)*(ncells_perplane + viewsep);

    float red, green, blue;

    colorhit(thishit.adc, red, green, blue);

    cairo_set_source_rgb(cr, red, green, blue);

    cairo_move_to(cr, x     , y+0.5);
    cairo_line_to(cr, x+pixx, y+0.5+pixy-1); // TODO: make work for pixy != 1
    cairo_stroke(cr);
  }
}

static void set_eventn_status(const int currenttick)
{
  set_status(0, "Event %'d, ticks %'d through %'d, %d hits",
             THEevent.nevent, THEevent.mintick, THEevent.maxtick,
             (int)THEevent.hits.size());
  set_status(1, "There are %'d events in this file", (int)theevents.size());
  if(!can_animate)
    set_status(2, "");
  else
    set_status(2, "Showing tick %d\n", currenttick);
}

gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee, gpointer data)
{
  nevent * MYevent = (nevent *)data;

  // Animate only if drawing for the first time, not if exposed.
  const bool animate = !ee && can_animate;

  for(int ti = animate?MYevent->mintick:MYevent->maxtick;
      ti <= MYevent->maxtick;
      ti+=4){

    set_eventn_status(ti);

    cairo_t * cr = gdk_cairo_create(widg->window);
    cairo_push_group(cr);

    draw_background(cr);
    draw_hits(cr, MYevent, ti);

    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
    cairo_destroy(cr);

    if(ti != MYevent->maxtick){
      usleep(10e3);
      bool getout = false;
      while(g_main_context_iteration(NULL, FALSE)) getout = true;
      if(getout) break;
    }
  }

  return FALSE;
}

struct butpair{
  GtkWidget * next, * prev;
};

void get_event(nevent & event, const int change)
{
  static int evi = 0;
  evi += change;
  if(evi >= (int)theevents.size()) evi = theevents.size() - 1;
  if(evi <          0            ) evi = 0;
  event = theevents[evi];
}

/** Display the next or previous event that satisfies the condition
given in the test function passed in. */
static void to_next(__attribute__((unused)) GtkWidget * widget,
                    gpointer data)
{
  bool * forward = (bool *)data;
  if(*forward) get_event(THEevent,  1);
  else         get_event(THEevent, -1);

  draw_event(edarea, 0, &THEevent);
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

static void toggle_cum_ani(__attribute__((unused)) GtkWidget * w, gpointer dt)
{
  cumulative_animation = !cumulative_animation;
  //if(can_animate) draw_event((GtkWidget *)dt, 0, &THEevent);
}

static void toggle_animate(__attribute__((unused)) GtkWidget * w, gpointer dt)
{
  can_animate = !can_animate;

  // This doesn't go well - the ticky box doesn't change state graphically
  // until it is done, and the animation only works on the second try.  Is that
  // because I'm catching events inside the animation, and releasing the mouse
  // button is an event, or something?
  //draw_event((GtkWidget *)dt, 0, &THEevent);
}

static void close_window()
{
  gtk_main_quit();
  exit(0);
}

void realmain()
{
  gtk_init(0, 0);
  GtkWidget * win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win),
                       "NOE: New nOva Event viewer");
  g_signal_connect(win, "delete-event", G_CALLBACK(close_window), 0);


  edarea = gtk_drawing_area_new();
  gtk_widget_set_size_request(edarea, nplanes_perview*pixx + 2,
                                      ncells_perplane*pixy*2 + viewsep + 2);
  g_signal_connect(edarea,"expose-event",G_CALLBACK(draw_event),&THEevent);

  butpair npbuts = mkbutton((char *)"Event");

  const int nrow = 5, ncol = 4;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);

  GtkWidget * animate_checkbox = gtk_check_button_new_with_mnemonic("_Animate");
  GtkWidget * cum_ani_checkbox = gtk_check_button_new_with_mnemonic("_Cumulative animation");

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

  get_event(THEevent, 0);
  draw_event(edarea, 0, &THEevent);

  gtk_main();
}
