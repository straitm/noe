enum noe_view_t{ kX, kY, kXorY };

struct rect{
  int xmin, ymin;
  int xsize, ysize;
  int ymax(){ return ymin + ysize; }
  int xmax(){ return xmin + xsize; }
};

// Given the number of pixels used for the vertical extent of a hit in one
// plane, return the number of pixels we should use the for the vertical
// extent.
int pixx_from_pixy(const int y);

// Set the variables that tell us where to draw the edges of the detector
void setboxes();

// Switch from ND to FD
void setfd();

// Given the number of horizontal pixels per cell, return the size of the
// horizontal detector box.  This is the number of pixels from the border
// to the last non-border pixel.
int total_x_pixels(const int pixels_x);

// Given the number of vertical pixels per cell, return the size of the
// vertical detector box.  This is the number of pixels from the border
// to the last non-border pixel.
int total_y_pixels(const int pixels_y);

// Given the number of pixels used for the horizontal extent of a plane in one
// view, return the number of pixels we should use for the extent of
// scintillator.  This is about 0.42 of the input.
int scintpix_from_pixx(const int x);

// Given the plane and cell, return the top of the screen position in
// Cairo coordinates.  More precisely, returns half a pixel above the top.
int det_to_screen_y(const int plane, const int cell);

// Given the plane, returns the left side of the screen position in Cairo
// coordinates.  More precisely, returns half a pixel to the left of the left
// side.
int det_to_screen_x(const int plane);

// Given a point in fractinal cell and plane coordinates, return the coordinates
// in pixels.  The view is inferred from the plane number.
std::pair<int, int> trackpoint_to_screen(const cppoint & tp);

// Given a screen position, returns the plane number. Any x position within the
// boundaries of hits displayed in a plane, including the right and left
// pixels, will return the same plane number.  In the muon catcher, return the
// nearest plane in the view if the screen position is in dead material.  If
// the screen position is outside the detector boxes to the right or left,
// return -1.
int screen_to_plane(const noe_view_t view, const int x);

// Same as screen_to_plane(), but is happy to return negative numbers and
// numbers bigger than the number of planes.
int screen_to_plane_unbounded(const noe_view_t view, const int x);

// Given a screen position, returns the cell number.  If this position is
// outside the detector boxes on the right or left, return -1.  If this
// position is outside the detector boxes to the top or bottom, returns a cell
// number as if the detector continued in that direction.
int screen_to_cell(const noe_view_t view, const int x, const int y);

// Same as screen_to_cell(), but is happy to return negative numbers and
// numbers bigger than the number of cells.
int screen_to_cell_unbounded(const noe_view_t view, const int x, const int y);
