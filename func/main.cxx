#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include "event.h"

GtkWidget * edarea = NULL;

extern std::vector<nevent> theevents;

struct tonext {
  bool(*func)(const nevent &); // is event is interesting?
  bool forward;  // True if we are seeking forward in the file
  tonext(bool(*func_in)(const nevent &), const bool forward_in)
  {
    func = func_in;
    forward = forward_in;
  }
};

static nevent THEevent;

void colorhit(const int32_t adc, float & red, float & green, float & blue)
{
  // Oh so hacky!
  const float graycut = 60;
  const float nextcut = 120;
  if(adc < graycut) red = green = blue = 0.2;
  else if(adc < nextcut)
    blue  = 0.9 + 0.3*(adc-graycut)/(nextcut-graycut),
    red   = 0.9 - 0.7*(adc-graycut)/(nextcut-graycut),
    green = 0.9 - 0.7*(adc-graycut)/(nextcut-graycut);
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

const int viewsep = 8; // pixels between x and y views

static void draw_background(GtkWidget * widget, cairo_t * cr)
{
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  cairo_set_source_rgb(cr, 1, 0, 1);
  cairo_set_line_width(cr, 1.0);

  cairo_rectangle(cr, 0.5, 0.5,                   16*28*3+1, 12*32+1);
  cairo_stroke(cr);

  cairo_rectangle(cr, 0.5, 0.5 + 12*32 + viewsep, 16*28*3+1, 12*32+1);
  cairo_stroke(cr);
}

gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee, gpointer data)
{
  nevent * MYevent = (nevent *)data;

  // Animate only if drawing for the first time, not if exposed.
  // bool animate = !ee;

  cairo_t * cr = gdk_cairo_create(widg->window);
  cairo_push_group(cr);

  draw_background(widg, cr);

  cairo_set_line_width(cr, 1.0);

  printf("I'm going to draw an event of %u hits\n", (unsigned int)MYevent->hits.size());
  std::sort(MYevent->hits.begin(), MYevent->hits.end(), by_charge);

  for(unsigned int i = 0; i < MYevent->hits.size(); i++){
    hit & thishit = MYevent->hits[i];

    // Want the nearest small integer box that has an aspect ratio
    // close to 3.36:1.  The options would seem to be 3:1 or 7:2.
    // 7:2 makes the detector 3136 pixels wide, which is a bit much, so
    // 3:1 it is, I guess
    const int x = 3*(thishit.plane/2) + 1,
      // put y view on the bottom
      y = 12*32*2 + viewsep - thishit.cell - (thishit.plane%2)*(12*32 + viewsep);

    float red, green, blue;

    colorhit(thishit.adc, red, green, blue);

    cairo_set_source_rgb(cr, red, green, blue);

    cairo_move_to(cr, x  , y+0.5);
    cairo_line_to(cr, x+3, y+0.5);
    cairo_stroke(cr);
  }

  cairo_pop_group_to_source(cr);
  cairo_paint(cr);
  cairo_destroy(cr);

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
  tonext * directions = (tonext *)data;
  if(directions->forward) get_event(THEevent, 1);
  else                    get_event(THEevent, -1);

  draw_event(edarea, 0, &THEevent);
}

static butpair mkbutton(char * label, bool(*func)(const nevent &))
{
  struct butpair thepair;
  static char labelbuf[1024];

  snprintf(labelbuf, 1023, "Next %s", label);
  thepair.next = gtk_button_new_with_label(labelbuf);
  g_signal_connect(thepair.next, "clicked", G_CALLBACK(to_next),
                   (void*)(new tonext(func, true)));

  snprintf(labelbuf, 1023, "Previous %s", label);
  thepair.prev = gtk_button_new_with_label(labelbuf);
  g_signal_connect(thepair.prev, "clicked", G_CALLBACK(to_next),
                  (void*)(new tonext(func, false)));
  return thepair;
}

bool just_go(__attribute__((unused)) const nevent & event)
{
  return true;
}

void realmain()
{
  gtk_init(0, 0);
  GtkWidget * win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win),
                       "NOE: New nOva Event viewer");

  edarea = gtk_drawing_area_new();
  gtk_widget_set_size_request(edarea, 28*16*3 + 2, 12*32*2 + viewsep + 2);
  g_signal_connect(edarea,"expose-event",G_CALLBACK(draw_event),&THEevent);

  butpair npbuts = mkbutton((char *)"Event", &just_go);

  const int nrow = 7, ncol = 4;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.next, 0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.prev, 1, 2, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), edarea, 0, ncol, 1, nrow);
  gtk_widget_show_all(win);
  gtk_main();
}
