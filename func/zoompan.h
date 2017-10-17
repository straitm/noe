void dopanning(const noe_view_t V, GdkEventMotion * gevent);
gboolean dozooming(GtkWidget * widg, GdkEventScroll * gevent,
                          gpointer data);
gboolean mousebuttonpress(GtkWidget * widg, GdkEventMotion * gevent,
                          gpointer data);
gboolean redraw_event(GtkWidget *widg, GdkEventExpose * ee,
                      gpointer data);
