#include <vector>

#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"

#include "RecoBase/CellHit.h"
#include "RecoBase/Track.h"

#include "func/main.h"
#include "func/event.h"

std::vector<nevent> theevents;

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

void noe::produce(art::Event& evt)
{
  art::Handle< std::vector<rb::CellHit> > cellhits;

  evt.getByLabel("calhit", cellhits);

  nevent ev;
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
  realmain(false);
}

DEFINE_ART_MODULE(noe);

}
