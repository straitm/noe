#include <gtk/gtk.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include "event.h"
#include "status.h"

GtkTextBuffer * stattext[NSTATBOXES];
GtkWidget * statbox[NSTATBOXES];

extern std::vector<noeevent> theevents;
extern int gevi;
extern int active_plane, active_cell;

extern bool ghave_read_all;

// Maximum length of any string being printed to a status bar
static const int MAXSTATUS = 1024;

// This hack allows us to get proper minus signs that look a lot better
// than the hyphens that printf puts out.  I'm sure there's a better
// way to do this.
#define BOTANY_BAY_OH_NO(x) x < 0?"−":"", fabs(x)
#define BOTANY_BAY_OH_INT(x) x < 0?"−":"", abs(x)

void set_status(const int boxn, const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  static char buf[MAXSTATUS];
  vsnprintf(buf, MAXSTATUS-1, format, ap);

  gtk_text_buffer_set_text(stattext[boxn], buf, strlen(buf));
  gtk_text_view_set_buffer(GTK_TEXT_VIEW(statbox[boxn]), stattext[boxn]);
  gtk_widget_draw(statbox[boxn], NULL);
}

void set_eventn_status0()
{
  if(theevents.empty()){
    set_status(0, "No events in file");
    return;
  }

  set_status(0, "Run %'d, subrun %d, event %'d "
                "(%'d/%'d in the file, %.0f%% loaded)",
    theevents[gevi].nrun, theevents[gevi].nsubrun,
    theevents[gevi].nevent, gevi+1,
    (int)theevents.capacity(), 100*float(theevents.size())/theevents.capacity());
}

void set_eventn_status1()
{
  noeevent & E = theevents[gevi];

  char status1[MAXSTATUS];

  int pos = snprintf(status1, MAXSTATUS, "Ticks %s%'d through %'d.  ",
             BOTANY_BAY_OH_INT(E.mintick), E.maxtick);
  if(E.current_mintick != E.current_maxtick)
    pos += snprintf(status1+pos, MAXSTATUS-pos,
      "Showing ticks %s%d through %s%d (%s%.3f through %s%.3f μs)",
      BOTANY_BAY_OH_INT(E.current_mintick),
      BOTANY_BAY_OH_INT(E.current_maxtick),
      BOTANY_BAY_OH_NO( E.current_mintick/64.),
      BOTANY_BAY_OH_NO( E.current_maxtick/64.));
  else
    pos += snprintf(status1+pos, MAXSTATUS-pos,
      "Showing tick %s%d (%s%.3f μs)",
      BOTANY_BAY_OH_INT(E.current_maxtick),
      BOTANY_BAY_OH_NO( E.current_maxtick/64.));

  set_status(1, status1);
}

void set_eventn_status2()
{
  if(active_plane < 0 || active_cell < 0){
    set_status(2, "Mouse over a cell for more information");
    return;
  }

  char status2[MAXSTATUS];
  int pos = snprintf(status2, MAXSTATUS, "Plane %d, cell %d: ",
                     active_plane, active_cell);

  // TODO: display calibrated energies when possible
  std::vector<hit> & THEhits = theevents[gevi].hits;
  bool needseparator = false;

  // TODO: make this more flexible.
  const int maxmatches = 2;
  int matches = 0;
  for(unsigned int i = 0; i < THEhits.size(); i++){
    if(THEhits[i].plane == active_plane &&
       THEhits[i].cell  == active_cell){
      matches++;
      if(matches <= maxmatches){
        pos += pos >= MAXSTATUS?0:snprintf(status2+pos, MAXSTATUS-pos,
            "%sTDC = %s%d (%s%.3f μs), TNS = %s%.3f μs%s, ADC = %s%d",
            needseparator?"; ":"",
            BOTANY_BAY_OH_INT(THEhits[i].tdc),
            BOTANY_BAY_OH_NO (THEhits[i].tdc/64.),
            BOTANY_BAY_OH_NO (THEhits[i].tns/1000),
            THEhits[i].good_tns?"":"(bad)",
            BOTANY_BAY_OH_INT(THEhits[i].adc));
        needseparator = true;
      }
      else if(matches == maxmatches+1){
        pos += pos >= MAXSTATUS?0:snprintf(status2+pos, MAXSTATUS-pos,
            "; and more...");
      }
    }
  }
  set_status(2, status2);
}

void set_eventn_status2progress(const int nhit, const int tothits)
{
  set_status(2, "Processing big event, %d/%d hits", nhit, tothits);
}

void set_eventn_status()
{
  set_eventn_status0();

  if(theevents.empty()) return;

  set_eventn_status1();
  set_eventn_status2();
}

