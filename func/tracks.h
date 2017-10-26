struct screentrack_t {
  // positions of trajectory points in pixels
  std::vector< std::pair<int, int> > traj;

  // index into the full track array. If all tracks are displayed, it is
  // one-to-one, otherwise not.
  int i;
};

// Given cairo's for both views, draw all the tracks and cache the
// screen positions for mouseovers.
void draw_tracks(cairo_t ** cr, const DRAWPARS * const drawpars);
