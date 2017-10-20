#include <signal.h>

#include <vector>

// For getting the event count when the file is opened
#include "TTree.h"
#include "art/Framework/Core/FileBlock.h"

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"

#include "RecoBase/CellHit.h"

// For tracks
#include "Geometry/Geometry.h"
#include "RecoBase/Track.h"

#include "func/main.h"
#include "func/event.h"

using std::vector;

vector<noeevent> theevents;

namespace noe {
class noe : public art::EDProducer {
  public:
  noe(fhicl::ParameterSet const& pset);
  ~noe();
  void produce(art::Event& evt);
  void endJob();

  // Used to get the number of events in the file
  void respondToOpenInputFile(art::FileBlock const &fb);

  // The art label for tracks that we are going to display, or the
  // empty string to display no tracks.
  std::string fTrackLabel;
};

noe::noe(fhicl::ParameterSet const & pset)
{
  fTrackLabel = pset.get< std::string >("track_label");
}

noe::~noe() { }

void noe::respondToOpenInputFile(art::FileBlock const &fb)
{
  // Get the number of events as soon as the file opens. This looks
  // really fragile. It gets the number of entries in *some* tree, which
  // at the moment that I'm testing this turns out to be the right one,
  // but EDProducer::respondToOpenInputFile is totally undocumented as
  // far as I can see.
  //
  // Anyway, if this is the wrong number, it just means that the status
  // display will be wrong about what fraction of the file is loaded.
  //
  // If the job has more than one file, we don't know that until the
  // second one triggers this function. This means the user is going to
  // be disappointed when the percent-loaded is rewound. Oh well.
  theevents.reserve(theevents.capacity() + fb.tree()->GetEntries());
}

void noe::endJob()
{
  realmain(true);
}

// Inject a test event with all FD cells hit
__attribute__((unused)) static void add_test_fd_event()
{
  noeevent ev;
  ev.nevent = 0;
  ev.nrun = 0;
  ev.nsubrun = 0;

  for(unsigned int c = 0; c < 12 * 32; c++){
    for(unsigned int p = 0; p < 32 * 28; p++){
      hit thehit;
      thehit.cell = c;
      thehit.plane = p;
      thehit.adc =  (c*p)%124;
      thehit.tdc = ((c*p)%432)*4;
      ev.addhit(thehit);
    }
  }
  theevents.push_back(ev);
}

// Inject a test event with all ND cells hit
__attribute__((unused)) static void add_test_nd_event()
{
  noeevent ev;
  ev.nevent = 0;
  ev.nrun = 0;
  ev.nsubrun = 0;

  for(unsigned int c = 0; c < 3 * 32; c++){
    for(unsigned int p = 0; p < 2 * (8 * 12 + 11); p++){
      if(p >= 2 * 8 * 12 && c >= 2 * 32 && p%2 == 0) continue;
      hit thehit;
      thehit.cell = c;
      thehit.plane = p;
      thehit.adc = (c*p)%1234;
      thehit.tdc = ((c*p)% 234)*4;
      ev.addhit(thehit);
    }
  }
  theevents.push_back(ev);
}

static vector< vector<float> > xcell_z;
static vector< vector<float> > ycell_z;
static vector<float> xplane_z;
static vector<float> yplane_z;
static vector< vector<float> > xcell_x;
static vector< vector<float> > ycell_y;

// Given a position in 3-space, return a plane and cell that is reasonably
// close in the given view assuming a regular detector geometry.
static trackpoint get_int_plane_and_cell(
  const double x, const double y, const double z, const geo::View_t view)
{
  trackpoint ans;

  // (1) First the plane
  {
    vector<float> & tplane_z = view == geo::kX?xplane_z:yplane_z;

    const vector<float>::iterator pi
      = std::upper_bound(tplane_z.begin(), tplane_z.end(), z);

    const int add = (view == geo::kX);

    if(pi == tplane_z.end()){
      // No plane had as big a 'z' as this, so use the last plane.
      ans.plane = (tplane_z.size()-1) * 2 + add;
    }
    else if(pi == tplane_z.begin()){
      // 'z' was smaller than the first plane, so that's the closest.
      ans.plane = add;
    }
    else{
      // Check which is closer, the first plane bigger than 'z', or the
      // previous one.
      const float thisone  = fabs( *pi - z);
      const float previous = fabs( *(pi - 1) - z);
      if(thisone < previous) ans.plane = (pi - tplane_z.begin())*2 + add;
      else                   ans.plane = (pi - tplane_z.begin())*2 + add - 2;
    }
  }

  // (2) Now find the cell
  {
    vector<float> & cells = (view == geo::kX? xcell_x:ycell_y)[ans.plane/2];

    const float t = (view == geo::kX? x: y);
    const vector<float>::iterator ci
      = std::upper_bound(cells.begin(), cells.end(), t);

    if(ci == cells.end()){
      // No cell had as big a 't' as this, so use the last cell.
      ans.cell = cells.size()-1;
    }
    else if(ci == cells.begin()){
      // 't' was smaller than the first cell, so that's the closest.
      ans.cell = 0;
    }
    else{
      // Check which is closer
      const float thisone  = fabs( *ci - t);
      const float previous = fabs( *(ci - 1) - t);
      if(thisone < previous) ans.cell = (ci - cells.begin());
      else                   ans.cell = (ci - cells.begin()) - 1;
    }
  }

  return ans;
}

static void build_cell_lookup_table(art::ServiceHandle<geo::Geometry> & geo)
{
  for(unsigned int pl = 0; pl < geo->NPlanes(); pl++){
    const geo::PlaneGeo * const plane = geo->Plane(pl);
    geo::View_t view;
    vector<float> cell_z, cell_t;
    for(unsigned int ce = 0; ce < plane->Ncells(); ce++){
      double cellcenter[3], dum[3];
      geo->CellInfo(pl, ce, &view, cellcenter, dum);
      cell_z.push_back(cellcenter[2]);
      cell_t.push_back(geo::kX?cellcenter[0]:cellcenter[1]);
    }

    (view == geo::kX?xcell_z:ycell_z).push_back(cell_z);
    (view == geo::kX?xcell_x:ycell_y).push_back(cell_t);

    vector<float> & pz = (view == geo::kX?xplane_z:yplane_z);
    pz.push_back(cell_z[cell_z.size()/2]);
  }
}

// Given a Cartesian position, tp, representing a track point, return the
// position in floating-point plane and cell number for both views where an
// integer means the cell center.
static std::pair<trackpoint, trackpoint> cart_to_cp(
  art::ServiceHandle<geo::Geometry> & geo, const TVector3 &tp)
{
  // With this lookup table (which isn't really a lookup table), finding
  // track point uses ~15% of the time spent loading events.  Probably
  // could go faster, although that's not too bad.
  {
    static int first = true;
    if(first) build_cell_lookup_table(geo);
    first = false;
  }

  // For each view, first find a plane and cell which is probably the closest
  // one, or maybe one of the several closest. Then ask the geometry where that
  // cell is and store the difference in the fractional part of the trackpoint.
  // This is right up to the difference between the mean plane and cell
  // spacings and the actual spacing near the requested point.  For purposes of
  // the event display, it's fine.

  // Exact values are not very important
  const double meanplanesep = 6.6681604;
  const double meancellsep  = 3.9674375;

  std::pair<trackpoint, trackpoint> answer;
  answer.first = get_int_plane_and_cell(tp.X(), tp.Y(), tp.Z(), geo::kX);

  double cellcenter[3], dum[3];
  geo::View_t dumv;

  geo->CellInfo(answer.first.plane, answer.first.cell, &dumv, cellcenter, dum);
  answer.first.fcell  = (tp.X() - cellcenter[0])/meancellsep;
  answer.first.fplane = (tp.Z() - cellcenter[2])/meanplanesep;

  // Could optimize this by only checking the nearest two planes to the one
  // found above, but I bet that geo::CellInfo is the hot spot, not
  // std::upper_bound.
  answer.second = get_int_plane_and_cell(tp.X(), tp.Y(), tp.Z(), geo::kY);

  geo->CellInfo(answer.second.plane, answer.second.cell, &dumv, cellcenter, dum);
  answer.second.fcell =  (tp.Y() - cellcenter[1])/meancellsep;
  answer.second.fplane = (tp.Z() - cellcenter[2])/meanplanesep;

  return answer;
}

void noe::produce(art::Event& evt)
{
  signal(SIGINT, SIG_DFL); // just exit on Ctrl-C

  art::Handle< vector<rb::CellHit> > cellhits;

  if(!evt.getByLabel("calhit", cellhits)){
    fprintf(stderr, "NOE needs CellHits with label \"calhit\".\n");
    _exit(0);
  }

  art::Handle< vector<rb::Track> > tracks;
  if(fTrackLabel != ""){
    if(!evt.getByLabel(fTrackLabel, tracks)){
      fprintf(stderr,
        "Warning: No tracks found with label \"%s\"\n", fTrackLabel.c_str());
      fTrackLabel = "";
    }
  }

  // Not needed for hits, just for tracks.  Aggressively don't load the
  // Geometry if it isn't needed.
  static art::ServiceHandle<geo::Geometry> * geo =
    fTrackLabel == ""? NULL: new art::ServiceHandle<geo::Geometry>;

#if 0
  if(theevents.empty()) add_test_nd_event();
#endif

  noeevent ev;
  ev.nevent = evt.event();
  ev.nrun = evt.run();
  ev.nsubrun = evt.subRun();

  // When we're reading in an event, the GUI is unresponsive. This is
  // a consequence of how we're working around art's design choices.
  // But this loop is not the bottleneck. The delay is inside art, so
  // there's no way to put hooks in the middle of it to keep the GUI
  // responsive.
  for(unsigned int i = 0; i < cellhits->size(); i++){
    const rb::CellHit & c = (*cellhits)[i];
    hit thehit;
    thehit.cell = c.Cell();
    thehit.plane = c.Plane();
    thehit.adc = c.ADC();
    thehit.tdc = c.TDC();
    thehit.tns = c.TNS();
    thehit.good_tns = c.GoodTiming();
    ev.addhit(thehit);
  }

  for(unsigned int i = 0; tracks.isValid() && i < tracks->size(); i++){
    track thetrack;
    for(unsigned int c = 0; c < (*tracks)[i].NCell(); c++){
      hit thehit;
      thehit.cell = (*tracks)[i].Cell(c)->Cell();
      thehit.plane = (*tracks)[i].Cell(c)->Plane();
      thetrack.hits.push_back(thehit);
    }
    for(unsigned int p = 0; p < (*tracks)[i].NTrajectoryPoints(); p++){
      const TVector3 & tp = (*tracks)[i].TrajectoryPoint(p);
      const std::pair<trackpoint, trackpoint> tps = cart_to_cp(*geo, tp);
      thetrack.trajx.push_back(tps.first);
      thetrack.trajy.push_back(tps.second);
    }
    ev.addtrack(thetrack);
  }

  theevents.push_back(ev);

  realmain(false);
}

DEFINE_ART_MODULE(noe);

}
