Put your own copies of MAME's Primal Rage files in this folder, as zips,
the way MAME keeps them (no unpacking, no renaming). MAME's .7z sets
(primrage.7z, tms32031.7z, ...) work too, mixed with zips or not:

  primrage.zip     Primal Rage, version 2.3 (Jan 1995): the main set
  tms32031.zip     the TMS320C31 boot ROM (c31boot.bin): the sound DSP
                   needs it; without it the game runs silent. Newer MAME
                   versions call this set tms320c31.zip: either name works

Optional, for the older versions (Menu > Game & system > Game version):

  primrageo.zip    version 2.3 (Dec 1994)
  primrage20.zip   version 2.0

They take their shared graphics and sound from primrage.zip, so keep that
one here too. A merged set (all three inside primrage.zip) works as well.

At startup the program checks every file against MAME's CRCs and says
exactly which ones are missing or different.
