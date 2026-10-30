// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * os4c_dos.c - dos.library calls the games make differently on AmigaOS 4.
 *
 * Open: the games load their music and keep their high scores in
 * "DH2:Dev/", the AmiKit shared folder they were developed in.  On
 * AmigaOS 4 a name there is looked for beside the program first
 * (PROGDIR:), so the files go next to it; a file being written goes
 * there.  If PROGDIR: doesn't have it, the original name is tried with
 * the "insert volume" requester turned off.
 *
 * Seek: dos.library's Seek is OBSOLETESeek on AmigaOS 4.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#define DEV_DIR "DH2:Dev/"

static BPTR open_quiet(CONST_STRPTR name, LONG mode)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    BPTR fh;
    me->pr_WindowPtr = (APTR)-1;
    fh = Open(name, mode);
    me->pr_WindowPtr = old;
    return fh;
}

BPTR oc_Open(CONST_STRPTR name, LONG mode)
{
    char alt[256];
    size_t n = strlen(DEV_DIR);
    BPTR fh;
    if (!name || strncmp((const char *)name, DEV_DIR, n) != 0 || strlen((const char *)name) - n + 9 >= sizeof(alt))
        return Open(name, mode);
    strcpy(alt, "PROGDIR:");
    strcat(alt, (const char *)name + n);
    if (mode == MODE_NEWFILE) return Open((CONST_STRPTR)alt, mode);
    fh = open_quiet((CONST_STRPTR)alt, mode);
    if (!fh) fh = open_quiet(name, mode);
    return fh;
}

/* Seek as on AmigaOS 3: the old position, or -1 */
LONG oc_Seek(BPTR file, LONG pos, LONG mode)
{
    int64 old = GetFilePosition(file);
    if (old < 0 || !ChangeFilePosition(file, pos, mode)) return -1;
    return (LONG)old;
}
