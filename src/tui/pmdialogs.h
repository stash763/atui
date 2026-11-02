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

    /* The two the window's chrome asks of the driver -- not dialogs, but
       the same one-pointer seam and the same reason: the implementation
       needs PM, and the caller is compiled for every front.

       status: replaces the tvision status line on the windowed front. The
       application composes the whole line (" provider | model  perm: ...")
       and the driver hands it to the native status window; every other
       front keeps drawing its own.

       keysToPage: the keyboard's traffic light. With the native entry
       field holding focus, a tvision modal dialog (the approval box, the
       providers editor while its kit field is still null) would starve --
       its keys belong to the page window then. on=1 parks focus on the
       page, on=0 hands it back to the entry field; nested modals nest the
       call, the driver counts. */
    void (*status)( const char *text );
    void (*keysToPage)( int on );
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

/* prompt: the native entry field's Enter key. The driver reads the
   field, clears it, and hands the text here -- pmPromptSubmit is what
   the tvision prompt's Enter did (submitMessage_), on the pump's own
   thread, while the application waits for its next event. */
void pmPromptSubmit( const char *text );

/* transcript: PgUp/PgDn rolled or typed while the entry field holds the
   keyboard belong to the transcript; direction -1 pages up, +1 down. */
void pmTranscriptPage( int direction );

/* Ctrl-T with the entry field focused: the thinking blocks toggle, the
   way the tvision intercept did while the prompt held the keys. */
void pmToggleThinking( void );

} /* extern "C" */

/* pmDialogsInstall() is defined by the driver's dialog unit (pmdlg.cpp) and
   called from hardwspm.cpp once the window exists; console builds neither
   compile nor call it -- its absence from a link is the point. */
void pmDialogsInstall( void );

#endif /* ATUI_TUI_PMDIALOGS_H */
