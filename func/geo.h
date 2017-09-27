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
