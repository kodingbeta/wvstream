========================================================================
    Application : wvstream Project Overview
========================================================================

This application acts as an lightweight backend for applications wich
want to play widevine decrypted media streams.

- rudimentary HTTP web server
- gzip encoded mpd file retrieval
- fast mpd file parser using expat
- automatic bandwidth detection (once when loading mpd file)
- muxing of selected audio + video stream

What you need to use it:

- widevine .dll / .so
- URL to mpd file
- URL for license request
- a suitable backend

Future plans:

- adaptive bandwith logic (play highest possible quality)
- linux Makefile / CMake files (code is mostly platform independent)

/////////////////////////////////////////////////////////////////////////////
Other notes:

none

/////////////////////////////////////////////////////////////////////////////
