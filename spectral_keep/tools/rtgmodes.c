/* rtgmodes: list the display modes the system knows, with Picasso96's view
 * of each (a check for amiga68k.c's RTG screen). */
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <libraries/Picasso96.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/Picasso96.h>
#include <stdio.h>

struct Library *P96Base;

int main(void)
{
    ULONG id = INVALID_ID;
    P96Base = OpenLibrary((CONST_STRPTR)"Picasso96API.library", 2);
    printf("Picasso96API.library: %s\n", P96Base ? "open" : "missing");
    while ((id = NextDisplayInfo(id)) != INVALID_ID) {
        struct NameInfo ni;
        struct DimensionInfo di;
        char name[40] = "?";
        if (GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni), DTAG_NAME, id) > 0) snprintf(name, sizeof(name), "%s", ni.Name);
        if (GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof(di), DTAG_DIMS, id) <= 0) continue;
        printf("%08lx %-32s %4ldx%-4ld depth %2ld", (long)id, name,
               (long)(di.Nominal.MaxX - di.Nominal.MinX + 1), (long)(di.Nominal.MaxY - di.Nominal.MinY + 1), (long)di.MaxDepth);
        if (P96Base && p96GetModeIDAttr(id, P96IDA_ISP96))
            printf("  P96 depth %ld fmt %ld", (long)p96GetModeIDAttr(id, P96IDA_DEPTH), (long)p96GetModeIDAttr(id, P96IDA_RGBFORMAT));
        printf("\n");
    }
    if (P96Base) CloseLibrary(P96Base);
    return 0;
}
