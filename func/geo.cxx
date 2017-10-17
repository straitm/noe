#include "geo.h"

static const int NDnplanes_perview = 8 * 12 + 11,
                 NDfirst_mucatcher = 8 * 24,
                 NDncells_perplane = 3 * 32;
static const int FDnplanes_perview = 16 * 28,
                 FDfirst_mucatcher = 9999, // i.e. no muon catcher
                 FDncells_perplane = 12 * 32;

int FDpixy = 1, FDpixx = pixx_from_pixy(FDpixy);
int NDpixy = 4, NDpixx = pixx_from_pixy(NDpixy);

// We're going to assume the ND until we see a hit that indicates it's FD
bool isfd = false;

int nplanes_perview = NDnplanes_perview,
    first_mucatcher = NDfirst_mucatcher,
    ncells_perplane = NDncells_perplane;
int nplanes = 2*nplanes_perview;
int pixx = NDpixx, pixy = NDpixy;

// The screen coordinates of the x and y view and muon catcher cutout,
// from the upper left corner of the border to the lower right corner
// of the inside.
rect screenview[kXorY], screenmu;

static const double ExtruDepth      = 66.1  ; // mm
static const double ExtruWallThick  =  5.1  ;
static const double ModuleGlueThick =  0.379;
static const double FDBlockGap      =  4.5  ;
static const double NDBlockGap      =  6.35 ;

static const int FD_planes_per_block = 32;
static const int ND_planes_per_block = 24;

static const double ExtruWidth     = 634.55;
static const double ExtruGlueThick =   0.48;

static const double mean_block_gap_per_plane =
  (FDBlockGap/FD_planes_per_block + NDBlockGap/ND_planes_per_block)/2;

static const double celldepth = ExtruDepth+ModuleGlueThick+mean_block_gap_per_plane;


// Given 'y', the number of vertical pixels we will use for each cell,
// return the number of horizontal pixels we will use.
int pixx_from_pixy(const int y)
{
  const double meancellwidth = (2*ExtruWidth+ExtruGlueThick)/32;

  // Comes out to 3.36, giving pixel ratios 3:1, 2:7, 3:10, 4:13, 5:17, etc.
  const double planepix_per_cellpix = 2*celldepth/meancellwidth;

  return int(y*planepix_per_cellpix + 0.5);
}

int scintpix_from_pixx(const int x)
{
  const double scintdepth = ExtruDepth - 2*ExtruWallThick;
  return int(x * scintdepth/(2*celldepth) + 0.5);
}

int total_x_pixels(const int pixels_x)
{
  return pixels_x*(nplanes_perview +
         (first_mucatcher < nplanes?
         nplanes_perview - first_mucatcher/2: 0)) + 1;
}

int total_y_pixels(const int pixels_y)
{
  return ncells_perplane*pixels_y
         + pixels_y/2 /* cell stagger */ + 1 /* border */;
}

void setfd()
{
  isfd = true;

  nplanes_perview = FDnplanes_perview;
  first_mucatcher = FDfirst_mucatcher;
  ncells_perplane = FDncells_perplane;

  nplanes = 2*nplanes_perview;

  pixx = FDpixx;
  pixy = FDpixy;

  setboxes();
}

// When we're zoomed, this stores the amount to the left of the screen
// that the left/top of the first plane/cell is.
int screenxoffset = 0, screenyoffset_xview = 0, screenyoffset_yview = 0;

int det_to_screen_x(const int plane)
{
  const bool xview = plane%2 == 1;
  return 1 + // Don't overdraw the border
    pixx*((plane

         // space out the muon catcher planes so they are twice as far
         // apart as normal.  This is very close to right, since the depth
         // of (two scintillator planes + one steel plane + air gaps) is
         // within 10% of the depth of two scintillator planes.
         +(plane > first_mucatcher?plane-first_mucatcher:0))/2)

        // stagger x and y planes
      + xview*pixx/2

      - screenxoffset;
}

int det_to_screen_y(const int plane, const int cell)
{
  const bool xview = plane%2 == 1;

  // In each view, every other plane is offset by half a cell width
  const bool celldown = !((plane/2)%2 ^ (plane%2));

  return + pixy*(ncells_perplane-cell) // cells numbered from the bottom

         - (pixy-1)

         // Physical stagger of planes in each view
         + celldown*pixy/2

         - (xview?screenyoffset_xview:screenyoffset_yview);
}

// Calculate the size of the bounding boxes for the detector's x and y
// views, plus the muon catcher cutaway.
void setboxes()
{
  const int ybox = total_y_pixels(pixy);
  const int xbox = total_x_pixels(pixx);

  const int xboxnomu = pixx*(first_mucatcher/2) + pixy/2 /* cell stagger */;

  // muon catcher is 1/3 empty.  Do not include cell stagger here since we want
  // the extra half cells to be inside the active box.
  const int yboxnomu = (ncells_perplane/3)*pixy;

  screenview[kX].xmin = -screenxoffset + pixx/2 /* plane stagger */;
  screenview[kX].ymin = -screenyoffset_xview;
  screenview[kX].xsize = xbox;
  screenview[kX].ysize = ybox;

  const bool hasmucatch = first_mucatcher < nplanes;

  // In the x view the blank spaces are to the left of the hits, but in
  // the y view, they are to the right, but I don't want the box to include them.
  const int hacky_subtraction_for_y_mucatch = hasmucatch * pixx;

  screenview[kY].xmin = -screenxoffset;
  screenview[kY].ymin = -screenyoffset_yview;
  screenview[kY].xsize = xbox-hacky_subtraction_for_y_mucatch;
  screenview[kY].ysize = ybox;

  screenmu.xmin = -screenxoffset + 1 + xboxnomu;
  screenmu.ymin = -screenyoffset_yview;
  screenmu.xsize = xbox-xboxnomu-hacky_subtraction_for_y_mucatch-1;
  screenmu.ysize = yboxnomu;
}

int screen_to_plane_unbounded(const noe_view_t view, const int x)
{
  // Where x would be if not offset
  const int unoffsetx = x + screenxoffset;

  // The number of the first muon catcher plane counting only planes
  // in one view.
  const int halfmucatch = (first_mucatcher)/2 + (view == kY);

  // Account for the plane stagger and border width.
  const int effx = unoffsetx - 2 -
    (unoffsetx-2 >= halfmucatch*pixx)*(pixx/2);

  // Half the plane number, as long as we're not in the muon catcher
  int halfp = view == kX? (effx-pixx/2)/pixx
                        :(    effx   )/pixx;

  // Fix up the case of being in the muon catcher
  if(halfp > halfmucatch) halfp = halfmucatch +
                                  (halfp - halfmucatch)/2;

  // The plane number, except it might be out of range
  return halfp*2 + (view == kX);
}

int screen_to_plane(const noe_view_t view, const int x)
{
  const int p = screen_to_plane_unbounded(view, x);
  if(p < (view == kX)) return -1; // XXX too clever
  if(p >= nplanes) return -1;
  return p;
}

int screen_to_cell_unbounded(const noe_view_t view, const int x, const int y)
{
  // Where y would be if not offset.  Do not pass into functions.
  const int unoffsety = y + (view == kX?screenyoffset_xview:screenyoffset_yview);

  const int plane = screen_to_plane(view, x);
  const bool celldown = !((plane/2)%2 ^ (plane%2));
  const int effy = unoffsety - celldown*(pixy/2) - 2;

  return ncells_perplane - effy/pixy - 1;
}

int screen_to_cell(const noe_view_t view, const int x, const int y)
{
  const int c = screen_to_cell_unbounded(view, x, y);
  const int plane = screen_to_plane(view, x);
  if(c < 0) return -1;
  if(c >= ncells_perplane) return -1;
  if(plane >= first_mucatcher && view == kY && c >= 2*ncells_perplane/3) return -1;
  return c;
}
