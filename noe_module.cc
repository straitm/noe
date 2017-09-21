#include <signal.h>

#include <vector>

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"

#include "RecoBase/CellHit.h"
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

  art::Handle< std::vector<rb::CellHit> > cellhits;

  evt.getByLabel("calhit", cellhits);

#if 0
  if(theevents.empty()) add_test_nd_event();
#endif

  noeevent ev;
  ev.nevent = evt.event();
  ev.nrun = evt.run();
  ev.nsubrun = evt.subRun();
  for(unsigned int i = 0; i < cellhits->size(); i++){
    const rb::CellHit & c = (*cellhits)[i];
    hit thehit;
    thehit.cell = c.Cell();
    thehit.plane = c.Plane();
    thehit.adc = c.ADC();
    thehit.tdc = c.TDC();
    ev.addhit(thehit);
  }
  theevents.push_back(ev);

  // Buffer 25 events before popping up the GUI so that the chance of getting
  // stuck in some funny case while we are fetching more events is reduced.
  // This takes about 1 second for far detector ddenergy events, which are
  // about as heavy as they come.
  if(theevents.size() >= 25) realmain(false);
}

DEFINE_ART_MODULE(noe);

}
