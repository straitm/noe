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

// Given the number of pixels used for the horizontal extent of a plane in one
// view, return the number of pixels we should use for the extent of
// scintillator.  This is about 0.42 of the input.
int scintpix_from_pixx(const int x);

// Given a screen y position, return true if we are in the x-view, or if we are
// in neither view, return true if we are closer to the x-view.  The empty part
// of the detector above the muon catcher is considered to be part of the
// y-view as though the detector were a rectangle in both views.
bool screen_y_to_xview(const int y);

// Given the plane and cell, return the top of the screen position in
// Cairo coordinates.  More precisely, returns half a pixel above the top.
int det_to_screen_y(const int plane, const int cell);

// Given the plane, returns the left side of the screen position in Cairo
// coordinates.  More precisely, returns half a pixel to the left of the left
// side.
int det_to_screen_x(const int plane);

// Given a screen position, returns the plane number. Any x position within the
// boundaries of hits displayed in a plane, including the right and left
// pixels, will return the same plane number.  In the muon catcher, return the
// nearest plane in the view if the screen position is in dead material.  If
// the screen position is outside the detector boxes to the right or left or
// above or below both views, return -1.  If it is between the two views,
// return the plane for the closer view.
int screen_to_plane(const int x, const int y);

// Given a screen position, returns the cell number.  If this position is
// outside the detector boxes on the right or left, return -1.  If this
// position is outside the detector boxes to the top or bottom, returns a cell
// number as if the detector continued in that direction.
int screen_to_cell(const int x, const int y);
