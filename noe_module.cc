#include "func/main.h"

#include "art/Framework/Core/EDProducer.h"

#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Core/ModuleMacros.h"

#include "RecoBase/CellHit.h"
#include "RecoBase/RecoHit.h"
#include "RecoBase/Track.h"

#include <signal.h>

static FILE * TEMP = NULL;

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
  fclose(TEMP);
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

  static bool first = true;
  if(first){
    TEMP = fopen("temp", "w");
    first = false;
  }   

  fprintf(TEMP, "%u\n", cellhits->size());
  for(unsigned int i = 0; i < cellhits->size(); i++){
    const rb::CellHit & c = (*cellhits)[i];
    fprintf(TEMP, "%d %d\n", c.Plane(), c.Cell());
  }   
}

DEFINE_ART_MODULE(noe);

}
//////////////////////////////////////////////////////////////////////////
