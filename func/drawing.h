struct DRAWPARS{
  // The ticks to draw right now, typically just the minimum needed
  // and not the whole range that is visible.
  int32_t firsttick, lasttick;
  bool clear;
};
