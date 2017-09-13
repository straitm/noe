struct hit{
  int32_t cell, plane, adc, tdc;
};

struct nevent{
  std::vector<hit> hits;
  uint32_t nevent, nrun, nsubrun;

  int32_t mintick = 0x7fffffff, maxtick = 0;

  void addhit(const hit & h)
  {
    hits.push_back(h);
    if(h.tdc < mintick) mintick = h.tdc;
    if(h.tdc > maxtick) maxtick = h.tdc;
  }
};
