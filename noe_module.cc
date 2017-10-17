#include <signal.h>

#include <vector>

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"

#include "RecoBase/CellHit.h"

// For tracks
#include "Geometry/Geometry.h"
#include "RecoBase/Track.h"

#include "func/main.h"
#include "func/event.h"

std::vector<noeevent> theevents;

namespace noe {
class noe : public art::EDProducer {
  public:
  noe(fhicl::ParameterSet const& pset);
  ~noe();
  void produce(art::Event& evt);
  void endJob();

  // The art label for tracks that we are going to display, or the
  // empty string to display no tracks.
  std::string fTrackLabel;
};

noe::noe(fhicl::ParameterSet const & pset)
{
  fTrackLabel = pset.get< std::string >("track_label");
}

noe::~noe() { }

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

// Called by get_int_plane_and_cell() and calls itself recursively until
// it gets an answer.
//
// This is awfully slow, taking like 0.6s per event for FD cosmics
static trackpoint get_int_plane_and_cell_r(const int rdepth,
  art::ServiceHandle<geo::Geometry> & geo,
  const double  x, const double  y, const double  z,
  const geo::View_t view,
  const double dx, const double dy, const double dz)
{
  trackpoint ans;
  const double X = x+dx, Y = y+dy, Z = z+dz;
  bool ok = true;
  try{
    geo->getPlaneAndCellID(X, Y, Z, ans.plane, ans.cell);
  }
  catch(cet::exception e){
    ok = false;
  }

  if(ok){
    if(view == geo::kXorY ||
      (ans.plane%2 == 1 && view == geo::kX) ||
      (ans.plane%2 == 0 && view == geo::kY)){

      return ans;
    }
  }

  if(rdepth > 7*20){
    fprintf(stderr, "Couldn't find the cell/plane for (%.1f, %.1f, %.1f)\n", x, y, z);
    ans.plane = ans.cell = 0;
    return ans;
  }

  // search progressively farther inward
  bool gox, goy, goz;
  switch(rdepth%7){
    case 0: gox = fabs(x)>fabs(y); goy = !gox; goz = false; break;
    case 1: gox = fabs(y)>fabs(x); goy = !gox; goz = false; break;
    case 2: gox = false; goy = false; goz = true ; break;
    case 3: gox = false; goy = true ; goz = true ; break;
    case 4: gox = true ; goy = false; goz = true ; break;
    case 5: gox = true ; goy = true ; goz = false; break;
    default:gox = true ; goy = true ; goz = true ; break;
  }
  const double step = 3.0*pow(1.25, rdepth/7 + 1);
  return get_int_plane_and_cell_r(rdepth+1, geo, x, y, z, view,
    -x/fabs(x)        *gox*step,
    -y/fabs(y)        *goy*step,
    (z < 600.0? 1: -1)*goz*step);
}

// Given a position in 3-space, return a plane and cell that (preferably)
// that position is inside of or (quite often) is somewhere near. If
// view is kXorY, returns whichever view it finds.  Otherwise, requires
// that it's the named view.
static trackpoint get_int_plane_and_cell(
  art::ServiceHandle<geo::Geometry> & geo,
  const double x, const double y, const double z, const geo::View_t view)
{
  return get_int_plane_and_cell_r(0, geo, x, y, z, view, 0, 0, 0);
}

// Given a Cartesian position, tp, representing a track point, return the
// position in floating-point plane and cell number for both views where an
// integer means the cell center.
//
// XXX I don't think it gives the right fractions for the muon catcher
static std::pair<trackpoint, trackpoint> cart_to_cp(
  art::ServiceHandle<geo::Geometry> & geo, const TVector3 &tp)
{
  // fragile
  const double meanplanesep = 6.6681604;
  const double meancellsep = 3.9674375;

  // First find the position for the view that the point is really in.
  trackpoint tp_nativeview = get_int_plane_and_cell(geo, tp.X(), tp.Y(), tp.Z(),
    geo::kXorY);
  double cellcenter[3], cellhalfsize[3] /* includes PVC but not glue or air? */;

  geo::View_t nativeview;
  geo->CellInfo(tp_nativeview.plane, tp_nativeview.cell,
                &nativeview, cellcenter, cellhalfsize);
  tp_nativeview.fcell =
    nativeview == geo::kX? (tp.X() - cellcenter[0])/meancellsep
                         : (tp.Y() - cellcenter[1])/meancellsep;

  tp_nativeview.fplane = (tp.Z() - cellcenter[2])/meanplanesep;

  // Now shift down one plane and find the cell and plane
  trackpoint tp_otherview = get_int_plane_and_cell(geo, tp.X(), tp.Y(),
    tp.Z() + meanplanesep, nativeview == geo::kX?geo::kY:geo::kX);

  geo::View_t otherview;
  geo->CellInfo(tp_otherview.plane, tp_otherview.cell,
                &otherview, cellcenter, cellhalfsize);
  tp_otherview.fcell =
    otherview == geo::kX? (tp.X() - cellcenter[0])/meancellsep
                        : (tp.Y() - cellcenter[1])/meancellsep;

  tp_otherview.fplane = (tp.Z() - cellcenter[2])/meanplanesep;

  // Should not happen (should!)
  if(nativeview == otherview)
    fprintf(stderr, "Oh no, got the same view twice!\n");

  std::pair<trackpoint, trackpoint> answer;
  if(nativeview == geo::kX){
    answer.first  = tp_nativeview;
    answer.second = tp_otherview;
  }
  else{
    answer.first  = tp_otherview;
    answer.second = tp_nativeview;
  }
  return answer;
}


void noe::produce(art::Event& evt)
{
  signal(SIGINT, SIG_DFL); // just exit on Ctrl-C

  // Not needed for hits, just for tracks.  Aggressively don't load the
  // Geometry if it isn't needed.
  art::ServiceHandle<geo::Geometry> * geo =
    fTrackLabel == ""? NULL: new art::ServiceHandle<geo::Geometry>;

  art::Handle< std::vector<rb::CellHit> > cellhits;

  if(!evt.getByLabel("calhit", cellhits)){
    fprintf(stderr, "NOE needs a file with calhits in it.\n");
    _exit(0);
  }

  art::Handle< std::vector<rb::Track> > tracks;
  if(fTrackLabel != ""){
    try{evt.getByLabel(fTrackLabel, tracks); }
    catch(...){
      fprintf(stderr,
        "Warning: No tracks found with label \"%s\"\n", fTrackLabel.c_str());
    }
  }

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
