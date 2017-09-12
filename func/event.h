struct hit{
  int32_t cell, plane, adc, tdc;
};

struct nevent{
  std::vector<hit> hits;
};

