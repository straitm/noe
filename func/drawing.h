struct DRAWPARS{
  // The ticks to draw right now, typically just the minimum needed
  // and not the whole range that is visible.
  int32_t firsttick, lasttick;
  bool clear;
};

// Refresh the event display in its current state.  For use when exposed.
gboolean redraw_event(GtkWidget *widg, GdkEventExpose * ee,
                      gpointer data);

// Draw a whole event, a range, or an animation frame, as dictated by
// the DRAWPARS.
void draw_event(const DRAWPARS * const drawpars);

// Set the size of the event display areas to the size of the detector
// at the default zoom level
void request_edarea_size();
