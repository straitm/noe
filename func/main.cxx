#include <gtk/gtk.h>
#include <stdio.h>
#include <vector>

struct hit{
  int cell, plane;
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

gboolean draw_event(GtkWidget *widg, GdkEventExpose * ee, gpointer data)
{
  nevent * MYevent = (nevent *)data;

  cairo_t * cr = gdk_cairo_create(widg->window);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  cairo_set_line_width(cr, 1);

  printf("I'm going to draw an event of %u hits\n", (unsigned int)MYevent->hits.size());

  for(unsigned int i = 0; i < MYevent->hits.size(); i++){
    int x = MYevent->hits[i].plane,
        y = MYevent->hits[i].cell;

    if(x%2 == 1) y += 400;

    y = 12*32 + 400 - y;

    cairo_set_source_rgb(cr, 0, 1, 1);

    cairo_move_to(cr, x  , y);
    cairo_line_to(cr, x+1, y);
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
    fscanf(TEMP, "%d %d", &event.hits[i].plane, &event.hits[i].cell);
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

  const int nrow = 3, ncol = 1;
  GtkWidget * tab = gtk_table_new(nrow, ncol, FALSE);
  gtk_container_add(GTK_CONTAINER(win), tab);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.next, 0, 1, 0, 1);
  gtk_table_attach_defaults(GTK_TABLE(tab), npbuts.prev, 0, 1, 1, 2);
  gtk_table_attach_defaults(GTK_TABLE(tab), edarea, 0, 1, 2, 3); 
  gtk_widget_show_all(win);
  gtk_main();
}
