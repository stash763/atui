/* pmhello.h -- resource identifiers for the PM scaffold probe, shared by
   pmhello.cpp and pmhello.rc.  The numbering follows the toolkit stdwnd
   sample's hello.h (ID_WINDOW 256 and up) so the two can be diffed by eye.

   Throwaway by design: answers belong in TESTING.md, files go away. */
#ifndef PMHELLO_H
#define PMHELLO_H

#define ID_WINDOW       256
#define ID_OPTIONS      257
#define ID_EXITPROG     258

#define IDS_HELLO       259

/* hello.rc's ACCELTABLE needs VK_F3; C gets it from <os2.h>, but the
   resource compiler only sees this file, so supply it when absent. */
#ifndef VK_F3
#define VK_F3           0x001D
#endif

#endif /* PMHELLO_H */
