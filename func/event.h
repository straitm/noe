struct hit{
  int32_t cell, plane, adc, tdc;
};

struct nevent{
  std::vector<hit> hits;
  uint32_t nevent, nrun, nsubrun;

  int32_t mintick = 0x7fffffff, maxtick = 0;

  bool fdlike = false;

  void addhit(const hit & h)
  {
    hits.push_back(h);

    // I don't want to use any art services, so autodetect when we are in
    // the FD.  This should fail very very rarely since the FD is noisy.
    if(!fdlike && (h.cell >= 3 * 32 || h.plane >= 8 * 24 + 22)) fdlike = true;

    if(h.tdc < mintick) mintick = h.tdc;
    if(h.tdc > maxtick) maxtick = h.tdc;
  }
};
