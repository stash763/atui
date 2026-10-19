/* Every public tvision header, with every feature turned on.

   The point is the loop.  A full tvision build on the target takes about seven
   minutes over this link, and most of its files fail for the same handful of
   reasons, because almost everything includes the same headers.  Compiling just
   the public headers takes about two seconds and shows those failures once each
   instead of once per file, so a header fix can be checked against the whole
   library as often as it is made.

     wpp386 @defs.rsp -fo=ch.obj check_headers.cpp

   The Uses_ macros have to be defined first, and that is worth knowing the hard
   way: tvision's headers are bodies guarded by `#if defined( Uses_XXX )`, so a
   translation unit that does not define them silently compiles to nothing and
   reports no errors at all.  The first version of this file did exactly that and
   looked clean while covering half the library.  Every Uses_ name in the tree is
   defined here, and the list is generated from the headers rather than typed,
   for the same reason the file itself is: a check that quietly stops covering
   things is worse than no check.

   The order of the includes is the order of the file names, which is to say
   arbitrary.  A failure that only appears in this combination is worth knowing
   about on its own -- a header that needs another one first is a portability
   problem for every compiler, not only this one.
*/

#define Uses_EventCodes
#define Uses_fpbase
#define Uses_fpstream
#define Uses_ifpstream
#define Uses_iopstream
#define Uses_ipstream
#define Uses_MsgBox
#define Uses_ofpstream
#define Uses_opstream
#define Uses_otstream
#define Uses_pstream
#define Uses_TApplication
#define Uses_TBackground
#define Uses_TButton
#define Uses_TChDirDialog
#define Uses_TCheckBoxes
#define Uses_TClipboard
#define Uses_TCluster
#define Uses_TCollection
#define Uses_TColorAttr
#define Uses_TColorDialog
#define Uses_TColorDisplay
#define Uses_TColorGroup
#define Uses_TColorGroupList
#define Uses_TColorItem
#define Uses_TColorItemList
#define Uses_TColorSelector
#define Uses_TCommandSet
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDirCollection
#define Uses_TDirEntry
#define Uses_TDirListBox
#define Uses_TDrawBuffer
#define Uses_TDrawSurface
#define Uses_TEditor
#define Uses_TEditWindow
#define Uses_TEvent
#define Uses_TEventQueue
#define Uses_TFileCollection
#define Uses_TFileDialog
#define Uses_TFileEditor
#define Uses_TFileInfoPane
#define Uses_TFileInputLine
#define Uses_TFileList
#define Uses_TFilterValidator
#define Uses_TFindDialogRec
#define Uses_TFrame
#define Uses_TGroup
#define Uses_THardwareInfo
#define Uses_THistory
#define Uses_THistoryViewer
#define Uses_THistoryWindow
#define Uses_TIndicator
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TListBox
#define Uses_TListViewer
#define Uses_TLookupValidator
#define Uses_TMemo
#define Uses_TMenu
#define Uses_TMenuBar
#define Uses_TMenuBox
#define Uses_TMenuItem
#define Uses_TMenuPopup
#define Uses_TMenuView
#define Uses_TMonoSelector
#define Uses_TMultiCheckBoxes
#define Uses_TNSCollection
#define Uses_TNSSortedCollection
#define Uses_TObject
#define Uses_TOutline
#define Uses_TOutlineViewer
#define Uses_TPalette
#define Uses_TParamText
#define Uses_TPoint
#define Uses_TPReadObjects
#define Uses_TProgram
#define Uses_TPWrittenObjects
#define Uses_TPXPictureValidator
#define Uses_TRadioButtons
#define Uses_TRangeValidator
#define Uses_TRect
#define Uses_TReplaceDialogRec
#define Uses_TResourceCollection
#define Uses_TResourceFile
#define Uses_TResourceItem
#define Uses_TScreen
#define Uses_TScreenCell
#define Uses_TScrollBar
#define Uses_TScroller
#define Uses_TSearchRec
#define Uses_TSItem
#define Uses_TSortedCollection
#define Uses_TSortedListBox
#define Uses_TStaticText
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TStreamable
#define Uses_TStreamableClass
#define Uses_TStreamableTypes
#define Uses_TStrIndexRec
#define Uses_TStringCollection
#define Uses_TStringList
#define Uses_TStringLookupValidator
#define Uses_TStrListMaker
#define Uses_TSubMenu
#define Uses_TSurfaceView
#define Uses_TSystemError
#define Uses_TTerminal
#define Uses_TText
#define Uses_TTextDevice
#define Uses_TTimerQueue
#define Uses_TValidator
#define Uses_TView
#define Uses_TVMemMgr
#define Uses_TWindow
#define Uses_ViewCommands
#define Uses_XXXX

#include <tvision/tv.h>
#include <tvision/app.h>
#include <tvision/buffers.h>
#include <tvision/colors.h>
#include <tvision/colorsel.h>
#include <tvision/config.h>
#include <tvision/dialogs.h>
#include <tvision/drawbuf.h>
#include <tvision/editors.h>
#include <tvision/hardware.h>
#include <tvision/help.h>
#include <tvision/helpbase.h>
#include <tvision/menus.h>
#include <tvision/msgbox.h>
#include <tvision/objects.h>
#include <tvision/outline.h>
#include <tvision/resource.h>
#include <tvision/scrncell.h>
#include <tvision/stddlg.h>
#include <tvision/surface.h>
#include <tvision/system.h>
#include <tvision/textview.h>
#include <tvision/tkeys.h>
#include <tvision/tobjstrm.h>
#include <tvision/tspan.h>
#include <tvision/tstrview.h>
#include <tvision/ttext.h>
#include <tvision/ttypes.h>
#include <tvision/tv.h>
#include <tvision/tvobjs.h>
#include <tvision/util.h>
#include <tvision/validate.h>
#include <tvision/views.h>
