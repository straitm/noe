static const int NDnplanes_perview = 8 * 12 + 11,
                 NDfirst_mucatcher = 8 * 24,
                 NDncells_perplane = 3 * 32;
static const int FDnplanes_perview = 16 * 28,
                 FDfirst_mucatcher = 9999, // i.e. no muon catcher
                 FDncells_perplane = 12 * 32;

static const int FDpixy = 1, FDpixx = pixx_from_pixy(FDpixy);
static const int NDpixy = 3, NDpixx = pixx_from_pixy(NDpixy);


// We're going to assume the ND until we see a hit that indicates it's FD
bool isfd = false;

int nplanes_perview = NDnplanes_perview,
    first_mucatcher = NDfirst_mucatcher,
    ncells_perplane = NDncells_perplane;
int nplanes = 2*nplanes_perview;
int pixx = NDpixx, pixy = NDpixy;
int ybox, xboxnomu, yboxnomu, xbox;

// Given 'y', the number of vertical pixels we will use for each cell,
// return the number of horizontal pixels we will use.
int pixx_from_pixy(const int y)
{
  const double ExtruDepth      = 66.1  ; // mm
  const double ModuleGlueThick =  0.379;
  const double FDBlockGap      =  4.5  ;
  const double NDBlockGap      =  6.35 ;

  const double FD_planes_per_block = 32;
  const double ND_planes_per_block = 24;
  const double mean_block_gap_per_plane =
    (FDBlockGap/FD_planes_per_block + NDBlockGap/ND_planes_per_block)/2;

  const double ExtruWidth     = 634.55;
  const double ExtruGlueThick =   0.48;

  const double meancellwidth = (2*ExtruWidth+ExtruGlueThick)/32;
  const double celldepth = ExtruDepth+ModuleGlueThick+mean_block_gap_per_plane;

  // Comes out to 3.36, giving pixel ratios 3:1, 2:7, 3:10, 4:13, 5:17, etc.
  const double planepix_per_cellpix = 2*celldepth/meancellwidth;

  return int(y*planepix_per_cellpix + 0.5);
}

// Calculate the size of the bounding boxes for the detector's x and y
// views, plus the muon catcher cutaway.  Resize the window to match.
void setboxes()
{
  ybox = ncells_perplane*pixy + pixy/2 /* cell stagger */,
  xboxnomu = pixx*(first_mucatcher/2) + pixy/2 /* cell stagger */,

  // muon catcher is 1/3 empty.  Do not include cell stagger here since we want
  // the extra half cells to be inside the active box.
  yboxnomu = (ncells_perplane/3)*pixy,

  xbox = pixx*(nplanes_perview +
               (first_mucatcher < nplanes?
               nplanes_perview - first_mucatcher/2: 0));
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
