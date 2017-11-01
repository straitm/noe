enum statcontents {
  statrunevent,
  stattiming,
  stathit,
  stattrack,
  staterror,
  NSTATBOXES = 5
};


// Set the 'boxn'th status line to the given text, counted from zero.
void set_status(const int boxn, const char * format, ...);

// Set the zeroth status line to its standard contents -- run number, subrun
// number, event number, number of events in the file.
void set_eventn_status0();

// Set the first status line to its standard contents -- timing info
void set_eventn_status1();

// Set the second status line to its standard contents -- hit info or
// instructions to the user for obtaining same.
void set_eventn_status2();

// Set the third status line to its standard contents -- track info or
// instructions to the user for obtaining same.
void set_eventn_status3();

// Set the send status line to its alternative contents -- status bar
// while doing a long computation.
void set_eventn_status2progress(const int nhit, const int tothits);

// Set all status lines to their standard contents.
void set_eventn_status();
