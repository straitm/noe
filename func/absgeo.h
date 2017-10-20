// Given a screen position and a track, defined by points in screen 
// coordinates, return how far the screen position is from the track.
float screen_dist_to_track(const int x, const int y,
  const std::vector< std::pair<int, int> > & track);
