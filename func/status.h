enum statcontents {
  statrunevent,
  stattiming,
  stathit,
  staterror,
  stattrack,
  statvertex,
  NSTATBOXES
};


// Set the 'boxn'th status line to the given text, counted from zero.
void set_status(const int boxn, const char * format, ...);

// Set the zeroth status line to its standard contents -- run number, subrun
// number, event number, number of events in the file.
void set_eventn_status_runevent();

// Set the first status line to its standard contents -- timing info
void set_eventn_status_timing();

// Set the second status line to its standard contents -- hit info or
// instructions to the user for obtaining same.
void set_eventn_status_hit();

// Set the track info status line
void set_eventn_status_track();

// Set the vertex info status line
void set_eventn_status_vertex();

// Set the second status line to its alternative contents -- status bar
// while doing a long computation.
void set_eventn_status_progress(const int nhit, const int tothits);

// Set all status lines to their standard contents.
void set_eventn_status();
