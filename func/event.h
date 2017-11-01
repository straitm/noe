struct hit{
  uint16_t cell, plane;
  int16_t adc;
  bool good_tns;
  int32_t tdc;
  float tns;
};

struct cppoint{
  // The integer parts of the positions.
  uint16_t cell, plane;

  // The fractional part of the positions.  Half-precision would be sufficient,
  // but is awkward to implement.  Integer representation is also difficult,
  // since we cannot assume that these are within [-1, 1].
  float fcell, fplane;
};

struct track{
  // integer mm to save a little memory
  short startx, starty, startz, stopx, stopy, stopz;
  int32_t time; // mean time in TDC ticks
  float tns; // time in ns.  Copied from a double.

  std::vector<hit> hits;
  std::vector<cppoint> traj[2 /* x and y */];
};

struct vertex{
  cppoint pos[2 /* x and y */]; // Positions in plane/cell space
  short posx, posy, posz; // Positions in real space, in integer mm
  int32_t time; // time in TDC ticks
  float tns; // time in ns.  Copied from a double.
};

struct noeevent{
  std::vector<hit> hits;
  std::vector<track> tracks;
  std::vector<vertex> vertices;
  uint32_t nevent, nrun, nsubrun;

  // The first and last hits physically in the event
  int32_t mintick = 0x7fffffff, maxtick = 0;

  // The user-input minimum and maximum, which must be a subset of the
  // above. These are here so they can be remembered even if the user
  // navigates to another event and then back to this one.
  int32_t user_mintick = 0x7fffffff, user_maxtick = 0;

  // The actual range being displayed right now (or the last time the
  // event was drawn), which is a subset of the user_ times during an
  // animation. There is yet another relevant range of times, which
  // is the range that needs to be drawn. This is some subset of the
  // current_ times, depending on whether some hits are already on the
  // screen. Those don't make much sense to store with the event. At the
  // moment, the current_ times don't need to be stored with the event
  // either, since they are always reset upon switch to the event, but
  // it's easier to imagine that one might want to remember them so as
  // to be able to animate multiple events "simultaneously".
  //
  // Also as it stands, current_mintick is always either user_mintick or
  // current_maxtick, depending on whether the antimation is cumulative
  // or not.
  int32_t current_mintick = 0x7fffffff, current_maxtick = 0;

  bool fdlike = false;

  void addtrack(const track & t)
  {
    tracks.push_back(t);
  }

  void addvertex(const vertex & v)
  {
    vertices.push_back(v);
  }

  void addhit(const hit & h)
  {
    hits.push_back(h);

    // I don't want to use any art services unless it's really necessary, so
    // autodetect when we are in the FD.  This should fail very very rarely
    // since the FD is noisy.
    if(!fdlike && (h.cell >= 3 * 32 || h.plane >= 8 * 24 + 22)) fdlike = true;

    if(h.tdc < mintick) current_mintick = user_mintick = mintick = h.tdc;
    if(h.tdc > maxtick) current_maxtick = user_maxtick = maxtick = h.tdc;
  }
};
