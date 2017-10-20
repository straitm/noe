/* absgeo.cxx: This is the abstract geometry file which contains functions
 * that are fully divorced from cells and planes. */

#include <vector>
#include <cfloat>
#include <math.h>

// Return the distance from a point to a line segment if the point on
// the segment closest to the point is not one of the ends. Otherwise
// return FLT_MAX.
static float point_to_line(const int x, const int y,
                           const std::pair<int, int> & p1,
                           const std::pair<int, int> & p2)
{
  std::pair<float, float> eten;
  {
    std::pair<float, float> ete;
    ete.first = p1.first - p2.first;
    ete.second = p1.second - p2.second;

    eten.first  = ete.first /sqrt(ete.first*ete.first + ete.second*ete.second);
    eten.second = ete.second/sqrt(ete.first*ete.first + ete.second*ete.second);
  }

  std::pair<float, float> etp;
  etp.first  = p1.first  - x;
  etp.second = p1.second - y;

  const double detca = fabs(eten.first*etp.first + eten.second*etp.second);

  std::pair<float, float> closest_app;

  closest_app.first  = p1.first  - eten.first *detca;
  closest_app.second = p1.second - eten.second*detca;

  const bool between =
   (p1.first < closest_app.first && closest_app.first < p2.first) ||
   (p1.first > closest_app.first && closest_app.first > p2.first);

  if(between) return sqrt(pow(closest_app.first  - x, 2) +
                          pow(closest_app.second - y, 2));
  else return FLT_MAX;
}

static float min(const float a, const float b, const float c)
{
  if(a < b && a < c) return a;
  if(b < a && b < c) return b;
  return c;
}

static float point_to_line_segment(const int x, const int y,
                                   const std::pair<int, int> & p1,
                                   const std::pair<int, int> & p2)
{
  const float dist_to_inf_line = point_to_line(x, y, p1, p2);
  const float dist_to_p1 = sqrt(pow(x - p1.first, 2) + pow(y - p1.second, 2));
  const float dist_to_p2 = sqrt(pow(x - p2.first, 2) + pow(y - p2.second, 2));
  return min(dist_to_inf_line, dist_to_p1, dist_to_p2);
}

float screen_dist_to_track(const int x, const int y,
                           const std::vector< std::pair<int, int> > & track)
{
  float mindist = FLT_MAX;
  for(unsigned int i = 0; i < track.size()-1; i++){
    const float dist = point_to_line_segment(x, y, track[i], track[i+1]);
    if(dist < mindist) mindist = dist;
  }
  return mindist;
}
