/* src/tui/pmdialogs.h -- the seam between the application and the native
   dialogs of a windowed front.

   Why a pointer and not a call: the dialog code can only live in a
   translation unit that knows it runs under PM -- the one the driver build
   marks with ATUI_PM -- while the caller (showTheme and friends) is compiled
   once for every front and linked into all of them.  A direct call would be
   an undefined symbol in the VIO link; a second object tree for the app
   would be a stale-object trap with a build system inside it.  So the
   application owns one pointer, null as defined here and everywhere, and
   the PM driver installs a kit into it when its window is born.  Every
   other front reads null forever and draws its tvision dialog, which
   remains the reference implementation, not a fallback.

   The kit entries return non-zero when they showed their dialog and are
   done -- the tvision path is skipped -- and zero to ask for it.  Adding a
   dialog is adding a field; callers test the field for null, so the kit can
   grow one dialog at a time.

   The other direction is the little C interface below: the dialog lives in
   the driver's tree and must stay UI, every fact it shows or changes is
   app knowledge fetched through these.  Plain C types because the two sides
   meet across a library boundary they do not share headers for anything
   else. */
#ifndef ATUI_TUI_PMDIALOGS_H
#define ATUI_TUI_PMDIALOGS_H

struct PmDialogs
{
    int (*about)( void );
    int (*theme)( void );
    int (*switchModel)( void );
    int (*settings)( void );
    int (*providers)( void );
    int (*undo)( void );
};

extern const PmDialogs *gPmDialogs;   // null until the PM driver installs one

/* Implemented by the application (app.cpp), called by the dialogs. */
extern "C" {

/* about: the same words the tvision About box shows. */
const char *pmAboutText( void );

/* theme: 0 = classic, 1 = dark.  pmThemeApply() sets it, repaints the
   screen and saves the setting -- the whole of what the tvision dialog's
   OK button did. */
int  pmThemeCurrent( void );
void pmThemeApply( int which );

/* switch model: the provider list, labelled and indexed exactly as
   buildSwitchModelMenu_() labels it ("* " marks the active one);
   pmModelChosen() is pickSwitchModel_() -- the switch itself, the save
   and the status line. */
int          pmModelCount( void );
const char  *pmModelLabel( int index );
int          pmModelActive( void );
void         pmModelChosen( int index );

} /* extern "C" */

/* pmDialogsInstall() is defined by the driver's dialog unit (pmdlg.cpp) and
   called from hardwspm.cpp once the window exists; console builds neither
   compile nor call it -- its absence from a link is the point. */
void pmDialogsInstall( void );

#endif /* ATUI_TUI_PMDIALOGS_H */
