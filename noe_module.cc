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
};

noe::noe(fhicl::ParameterSet const & pset) { }

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

void noe::produce(art::Event& evt)
{
  signal(SIGINT, SIG_DFL); // just exit on Ctrl-C

  // Not needed for hits, just for tracks
  art::ServiceHandle<geo::Geometry> geo;

  art::Handle< std::vector<rb::CellHit> > cellhits;

  if(!evt.getByLabel("calhit", cellhits)){
    fprintf(stderr, "NOE needs a file with calhits in it.\n");
    _exit(0);
  }

  art::Handle< std::vector<rb::Track> > tracks;
  try{evt.getByLabel("breakpoint", tracks); }
  catch(...){;}

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
  if(tracks.isValid()){
    for(unsigned int i = 0; i < tracks->size(); i++){
      track thetrack;
      thetrack.startx = (*tracks)[i].Start().X();
      thetrack.starty = (*tracks)[i].Start().Y();
      thetrack.startz = (*tracks)[i].Start().Z();
      thetrack.stopx = (*tracks)[i].Stop().X();
      thetrack.stopy = (*tracks)[i].Stop().Y();
      thetrack.stopz = (*tracks)[i].Stop().Z();
      for(unsigned int c = 0; c < (*tracks)[i].NCell(); c++){
        hit thehit;
        thehit.cell = (*tracks)[i].Cell(c)->Cell();
        thehit.plane = (*tracks)[i].Cell(c)->Plane();
        thetrack.hits.push_back(thehit);
      }
      for(unsigned int p = 0; p < (*tracks)[i].NTrajectoryPoints(); p++){
        const TVector3 & tp = (*tracks)[i].TrajectoryPoint(p);
        int plane, cell;
        try{
          geo->getPlaneAndCellID(tp.X(), tp.Y(), tp.Z(), plane, cell);
          hit thehit;
          thehit.cell = cell;
          thehit.plane = plane;
          if(plane%2 == 1) thetrack.trajx.push_back(thehit);
          else             thetrack.trajy.push_back(thehit);
        }
        catch(cet::exception e){
          // If the unique cell id doesn't decode to a cell and plane, just
          // ignore the point.
          ;
        }
      }
      ev.addtrack(thetrack);
    }
  }

  theevents.push_back(ev);

  realmain(false);
}

DEFINE_ART_MODULE(noe);

}
