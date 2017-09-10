#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>

struct hit{
  int32_t cell, plane, adc, tdc;
};

struct nevent{
  std::vector<hit> hits;
};

GtkWidget * edarea = NULL;
static FILE * TEMP = NULL;

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
  if(adc < 40) red = green = blue = 0.3 + 0.01*adc;
  else if(adc < 120) blue  = 0.7 + 0.3*(adc-40)/80.0, 
                     red   = 0.7 - 0.7*(adc-40)/80.0, 
                     green = 0.7 - 0.7*(adc-40)/80.0;
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

gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee, gpointer data)
{
  nevent * MYevent = (nevent *)data;

  cairo_t * cr = gdk_cairo_create(widg->window);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  cairo_set_line_width(cr, 1.0);

  printf("I'm going to draw an event of %u hits\n", (unsigned int)MYevent->hits.size());

  for(unsigned int i = 0; i < MYevent->hits.size(); i++){
    hit & thishit = MYevent->hits[i];
    int x = thishit.plane, y = thishit.cell;

    if(x%2 == 1) y += 400; // put y view on the bottom

    y = 12*32 + 400 - y; // Flip into familiar orientation

    // Want the nearest small integer box that has an aspect ratio
    // close to 3.36:1.  The options would seem to be 3:1 or 7:2.
    // 7:2 makes the detector 3136 pixels wide, which is a bit much, so
    // 3:1 it is, I guess

    x /= 2; // dispense with wrong-parity planes
    x *= 3; // stretch to desired dimensions

    float red, green, blue;

    colorhit(thishit.adc, red, green, blue);

    cairo_set_source_rgb(cr, red, green, blue);

    cairo_move_to(cr, x  , y+0.5);
    cairo_line_to(cr, x+3, y+0.5);
    cairo_stroke(cr);
  }

  cairo_destroy(cr);
 
  return FALSE;
}

struct butpair{
  GtkWidget * next, * prev;
};

void get_event(nevent & event)
{
  int nhit = 0;
  fscanf(TEMP, "%d", &nhit);
  event.hits.resize(nhit);
  for(int i = 0; i < nhit; i++)
    fscanf(TEMP, "%d %d %d %d",
           &event.hits[i].plane,
           &event.hits[i].cell,
           &event.hits[i].adc,
           &event.hits[i].tdc);
}

/** Display the next or previous event that satisfies the condition
given in the test function passed in. */
static void to_next(__attribute__((unused)) GtkWidget * widget, 
                    gpointer data)
{
  get_event(THEevent);
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
  TEMP = fopen("temp", "r");

  gtk_init(0, 0); 
  GtkWidget * win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(win),
                       "NOE: New nOva Event viewer");

  edarea = gtk_drawing_area_new();
  gtk_widget_set_size_request(edarea, 896, int(384 * 2.1));
  g_signal_connect(edarea,"expose-event",G_CALLBACK(draw_event),&THEevent);

  butpair npbuts = mkbutton((char *)"Event", &just_go);

  const int nrow = 7, ncol = 1;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.next, 0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.prev, 0, 1, 1, 2);
  gtk_table_attach_defaults(GTK_TABLE(tab), edarea, 0, 1, 2, 7); 
  gtk_widget_show_all(win);
  gtk_main();
}
