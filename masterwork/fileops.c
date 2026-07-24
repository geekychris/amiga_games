/*
 * fileops.c - file operations.
 *
 * Most ops shell out to the AmigaDOS Copy / Delete / Rename / MakeDir /
 * More binaries via Execute("C:X arg arg"). Two reasons:
 *   - Copy ALL and Delete ALL FORCE handle recursion + special-case
 *     protection bits for free; reimplementing them here would double
 *     the source size without adding anything.
 *   - Shell commands are the "official" AmigaDOS semantics - nobody
 *     will be surprised by how our copy behaves because it IS the
 *     Copy they've been using in the Shell for 30 years.
 *
 * The downside is per-op fork overhead + no progress reporting for
 * long operations. Both acceptable for the MVP; a Phase 4-8 iteration
 * can add a native chunked copy with a progress dialog.
 */

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>

#include "masterwork.h"
#include "bridge_client.h"

/* --- helpers ----------------------------------------------------- */

/* Run a shell command line synchronously with stdin/out swallowed.
 * Returns 1 on success, 0 on failure. */
static int run_shell(const char *cmd)
{
    if (bridge_ok) AB_I("shell: %s", cmd);
    BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    LONG ok = 0;
    if (nil) {
        ok = Execute((STRPTR)cmd, (BPTR)0, nil);
        Close(nil);
    }
    return (int)ok;
}

/* Build "path/name" quoted for the shell (in case it has spaces). */
static void quote_path(char *out, size_t osz, const char *path, const char *name)
{
    char full[MAX_PATH];
    join_path(full, sizeof(full), path, name);
    snprintf(out, osz, "\"%s\"", full);
}

/* Iterate the effective selection in a pane. If any entries are ticked
 * we visit those; otherwise we visit just the cursor entry. Skips ".."
 * so the user can't nuke their parent by accident.
 *
 * Returns count actually iterated. Callback receives each entry. */
typedef void (*entry_cb)(Pane *p, Entry *e, void *ctx);
static int for_each_target(Pane *p, entry_cb cb, void *ctx)
{
    int sel = selected_count(p);
    int seen = 0;
    if (sel > 0) {
        for (int i = 0; i < p->count; i++) {
            if (!p->entries[i].selected) continue;
            if (strcmp(p->entries[i].name, "..") == 0) continue;
            cb(p, &p->entries[i], ctx);
            seen++;
        }
    } else if (p->cursor >= 0 && p->cursor < p->count) {
        Entry *e = &p->entries[p->cursor];
        if (strcmp(e->name, "..") != 0) {
            cb(p, e, ctx);
            seen++;
        }
    }
    return seen;
}

/* --- operations -------------------------------------------------- */

typedef struct {
    Pane *src, *dst;
    int   recursive;
    int   move;         /* copy+delete-src if 1 */
    int   errors;
    int   success;
} CopyCtx;

static void copy_one(Pane *sp, Entry *e, void *ctx_)
{
    CopyCtx *ctx = ctx_;
    char sq[MAX_PATH + 4], dq[MAX_PATH + 4];
    quote_path(sq, sizeof(sq), sp->path,      e->name);
    quote_path(dq, sizeof(dq), ctx->dst->path, e->name);

    /* Directories always need ALL (recursive). Files respect
     * ctx->recursive (harmless on plain files, adds CLONE which
     * preserves date/protect). */
    char cmd[MAX_PATH * 2 + 64];
    if (e->is_dir) {
        snprintf(cmd, sizeof(cmd), "C:Copy %s %s ALL CLONE", sq, dq);
    } else {
        snprintf(cmd, sizeof(cmd), "C:Copy %s %s CLONE", sq, dq);
    }

    if (!run_shell(cmd)) { ctx->errors++; return; }

    if (ctx->move) {
        /* Only remove source after a confirmed successful copy. */
        char rmcmd[MAX_PATH + 32];
        snprintf(rmcmd, sizeof(rmcmd),
                 e->is_dir ? "C:Delete %s ALL FORCE QUIET"
                           : "C:Delete %s QUIET",
                 sq);
        if (!run_shell(rmcmd)) { ctx->errors++; return; }
    }
    ctx->success++;
}

int op_copy(int recursive)
{
    Pane *src = &panes[active_pane];
    Pane *dst = &panes[active_pane ^ 1];
    if (strcmp(src->path, dst->path) == 0) {
        snprintf(status_msg, sizeof(status_msg),
                 "source and dest are the same pane; move cursor there first");
        return 1;
    }
    CopyCtx ctx = { src, dst, recursive, 0, 0, 0 };
    int n = for_each_target(src, copy_one, &ctx);
    if (n == 0) {
        snprintf(status_msg, sizeof(status_msg), "nothing to copy");
        return 1;
    }
    snprintf(status_msg, sizeof(status_msg),
             "copied %d/%d to %s", ctx.success, n, dst->path);
    clear_selection(src);
    refresh_pane(dst);
    return (ctx.errors == 0) ? 0 : 1;
}

int op_move(int recursive)
{
    Pane *src = &panes[active_pane];
    Pane *dst = &panes[active_pane ^ 1];
    if (strcmp(src->path, dst->path) == 0) {
        snprintf(status_msg, sizeof(status_msg),
                 "source and dest are the same pane");
        return 1;
    }
    CopyCtx ctx = { src, dst, recursive, 1, 0, 0 };
    int n = for_each_target(src, copy_one, &ctx);
    if (n == 0) {
        snprintf(status_msg, sizeof(status_msg), "nothing to move");
        return 1;
    }
    snprintf(status_msg, sizeof(status_msg),
             "moved %d/%d to %s", ctx.success, n, dst->path);
    clear_selection(src);
    refresh_pane(src);
    refresh_pane(dst);
    return (ctx.errors == 0) ? 0 : 1;
}

typedef struct { int recursive_and_force; int errors, success; } DelCtx;
static void del_one(Pane *p, Entry *e, void *ctx_)
{
    DelCtx *ctx = ctx_;
    char sq[MAX_PATH + 4];
    quote_path(sq, sizeof(sq), p->path, e->name);
    char cmd[MAX_PATH + 32];
    if (e->is_dir || ctx->recursive_and_force) {
        snprintf(cmd, sizeof(cmd), "C:Delete %s ALL FORCE QUIET", sq);
    } else {
        snprintf(cmd, sizeof(cmd), "C:Delete %s QUIET", sq);
    }
    if (run_shell(cmd)) ctx->success++;
    else                ctx->errors++;
}

int op_delete(int recursive_and_force)
{
    Pane *p = &panes[active_pane];
    int n = selected_count(p);
    if (n == 0 && p->cursor >= 0 && p->cursor < p->count &&
        strcmp(p->entries[p->cursor].name, "..") != 0) {
        n = 1;
    }
    if (n == 0) {
        snprintf(status_msg, sizeof(status_msg), "nothing to delete");
        return 1;
    }

    /* Yes/no confirm with count in the message. */
    char msg[128];
    snprintf(msg, sizeof(msg),
             "Delete %d item%s from\n%%s ?", n, n == 1 ? "" : "s");
    if (!confirm(msg, p->path)) {
        snprintf(status_msg, sizeof(status_msg), "delete cancelled");
        return 1;
    }

    DelCtx ctx = { recursive_and_force, 0, 0 };
    int actual = for_each_target(p, del_one, &ctx);
    snprintf(status_msg, sizeof(status_msg),
             "deleted %d/%d", ctx.success, actual);
    clear_selection(p);
    refresh_pane(p);
    return (ctx.errors == 0) ? 0 : 1;
}

int op_rename(void)
{
    Pane *p = &panes[active_pane];
    if (p->cursor < 0 || p->cursor >= p->count) return 1;
    Entry *e = &p->entries[p->cursor];
    if (strcmp(e->name, "..") == 0) {
        snprintf(status_msg, sizeof(status_msg), "can't rename \"..\"");
        return 1;
    }

    char newname[MAX_NAME];
    if (prompt_string("Masterwork - Rename to?", e->name, newname, sizeof(newname))) {
        return 1;
    }
    if (strcmp(newname, e->name) == 0) {
        snprintf(status_msg, sizeof(status_msg), "same name, no rename");
        return 1;
    }

    char oldq[MAX_PATH + 4], newq[MAX_PATH + 4];
    quote_path(oldq, sizeof(oldq), p->path, e->name);
    quote_path(newq, sizeof(newq), p->path, newname);
    char cmd[MAX_PATH * 2 + 32];
    snprintf(cmd, sizeof(cmd), "C:Rename %s %s QUIET", oldq, newq);
    if (!run_shell(cmd)) {
        snprintf(status_msg, sizeof(status_msg),
                 "rename failed: %s → %s", e->name, newname);
        return 1;
    }
    snprintf(status_msg, sizeof(status_msg), "renamed → %s", newname);
    refresh_pane(p);
    return 0;
}

int op_mkdir(void)
{
    Pane *p = &panes[active_pane];
    char name[MAX_NAME];
    if (prompt_string("Masterwork - New directory name?", "",
                      name, sizeof(name))) {
        return 1;
    }
    char q[MAX_PATH + 4];
    quote_path(q, sizeof(q), p->path, name);
    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "C:MakeDir %s", q);
    if (!run_shell(cmd)) {
        snprintf(status_msg, sizeof(status_msg),
                 "mkdir failed: %s", name);
        return 1;
    }
    snprintf(status_msg, sizeof(status_msg), "created %s", name);
    refresh_pane(p);
    return 0;
}

int op_view(void)
{
    Pane *p = &panes[active_pane];
    if (p->cursor < 0 || p->cursor >= p->count) return 1;
    Entry *e = &p->entries[p->cursor];
    if (e->is_dir) {
        snprintf(status_msg, sizeof(status_msg),
                 "can't view a directory - use Enter to descend");
        return 1;
    }

    char q[MAX_PATH + 4];
    quote_path(q, sizeof(q), p->path, e->name);
    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "C:More %s", q);
    /* Async - More opens its own window; we don't want to block. */
    if (bridge_ok) AB_I("view: %s", cmd);
    BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (nil) {
        Execute((STRPTR)cmd, (BPTR)0, nil);
        Close(nil);
    }
    snprintf(status_msg, sizeof(status_msg), "opened %s in More", e->name);
    return 0;
}
