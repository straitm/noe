struct screenvertex_t {
  // position in pixels
  std::pair<int, int> pos;

  // index into the full vertex array. If all vertices are displayed, it is
  // one-to-one, otherwise not.
  int i;
};

void draw_vertices(cairo_t ** cr, const DRAWPARS * const drawpars);

