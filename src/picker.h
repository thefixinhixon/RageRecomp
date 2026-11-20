// Windows of their own, before (or instead of) the game's: the first-run
// welcome screen and a plain message window.
#pragma once
#include <stdbool.h>

#ifndef PRODUCT
#define PRODUCT "RageRecomp"
#endif

// The first screen when the game's files are missing: what to add and where
// (folder; alt "" or a second place). Enter checks again with found().
// 1: the files are there; 0: the window was closed; -1: no window could open.
int picker_welcome(const char *folder, const char *alt, bool (*found)(void));
// A plain window showing text until closed, for when SDL's message box can't
// open (no zenity and no X core fonts). False if no window could be made.
bool picker_message(const char *title, const char *text);
