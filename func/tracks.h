// Given cairo's for both views, draw all the tracks and cache the
// screen positions for mouseovers.
//
// Currently ignores the drawing parameters and just unconditionally
// draws everything, but could in principle use the time range and
// redrawing hints.
void draw_tracks(cairo_t ** cr, const DRAWPARS * const drawpars);
