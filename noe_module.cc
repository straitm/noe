#include <vector>

#include "art/Framework/Core/EDProducer.h"

#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Core/ModuleMacros.h"

#include "RecoBase/CellHit.h"
#include "RecoBase/RecoHit.h"
#include "RecoBase/Track.h"

#include "func/main.h"
#include "func/event.h"

#include <signal.h>

std::vector<nevent> theevents;

namespace noe {
class noe : public art::EDProducer {
  public:
  explicit noe(fhicl::ParameterSet const& pset);
  virtual ~noe();
  void produce(art::Event& evt);
  void endJob();
};

noe::noe(fhicl::ParameterSet const & pset): EDProducer()
{
}

void noe::endJob()
{
  realmain();
}

noe::~noe() { }

void noe::produce(art::Event& evt)
{
  // I'm so sorry that I have to do this.  And, my goodness, doing
  // it in the constructor isn't sufficient.  If this isn't done,
  // it responds to PIPE by going into an endless loop.
  signal(SIGPIPE, SIG_DFL);

  art::Handle< std::vector<rb::CellHit> > cellhits;

  evt.getByLabel("calhit", cellhits);

  printf("Event %lu\n", cellhits->size());

  nevent ev;
  for(unsigned int i = 0; i < cellhits->size(); i++){
    const rb::CellHit & c = (*cellhits)[i];
    hit thehit;
    thehit.cell = c.Cell();
    thehit.plane = c.Plane();
    thehit.adc = c.ADC();
    thehit.tdc = c.TDC();
    ev.hits.push_back(thehit);
  }   
  theevents.push_back(ev);
}

DEFINE_ART_MODULE(noe);

}
//////////////////////////////////////////////////////////////////////////
