#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#ifndef NEXTVI_NOTERM
#include <signal.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#endif
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include "vi.h"

#if defined(NEXTVI_NOTERM) && !defined(lstat)
#define lstat(path, st) stat(path, st)
#endif

#include "conf.c"
#ifdef NEXTVI_EMBEDDED
#include "typewrt_power.h"
#endif

#define VI_GMARKS	10
#ifdef NEXTVI_EMBEDDED
#define VI_GMARKS_FILE	"/sdcard/.nextvi-gmarks"
#else
#define VI_GMARKS_FILE	".nextvi-gmarks"
#endif

static sbuf *vi_gmark_path[VI_GMARKS];
static int vi_gmark_row[VI_GMARKS], vi_gmark_off[VI_GMARKS];
static int vi_gmarks_loaded;

static void vi_hardwrap_all(void);
#ifdef NEXTVI_NOTERM
static void vi_keyboard_layout_changed(const char *layout);
#endif
void led_prompt_width(int width);

static int vi_gmark_slot(int mark, int version)
{
	if (version < 2)
		return mark >= 0 && mark < VI_GMARKS * 2 && mark % 2 == 0
			? mark / 2 : -1;
	return mark >= 0 && mark < VI_GMARKS ? mark : -1;
}

static void vi_gmark_set(int slot, const char *path, int row, int off, int save)
{
	if (slot < 0 || slot >= VI_GMARKS)
		return;
	if (!vi_gmark_path[slot])
		sbuf_make(vi_gmark_path[slot], 128)
	sbuf_cut(vi_gmark_path[slot], 0)
	sbufn_str(vi_gmark_path[slot], path ? path : "")
	vi_gmark_row[slot] = row;
	vi_gmark_off[slot] = off;
	if (save) {
		FILE *f;
#ifdef NEXTVI_EMBEDDED
		typewrt_sd_write_begin();
#endif
		f = fopen(VI_GMARKS_FILE, "w");
		if (f) {
			fprintf(f, "nextvi-gmarks 2\n");
			for (int i = 0; i < VI_GMARKS; i++)
				if (vi_gmark_path[i])
					fprintf(f, "%d\t%d\t%d\t%s\n", i,
						vi_gmark_row[i], vi_gmark_off[i],
						vi_gmark_path[i]->s);
			fclose(f);
		}
#ifdef NEXTVI_EMBEDDED
		typewrt_sd_write_end();
#endif
	}
}

static void vi_gmarks_load(void)
{
	char line[4096];
	FILE *f;
	int version = 1;
	if (vi_gmarks_loaded)
		return;
	vi_gmarks_loaded = 1;
	f = fopen(VI_GMARKS_FILE, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *p = line, *end, *path;
		long mark, row, off;
		int slot;
		if (!strncmp(line, "nextvi-gmarks", 13)) {
			p += 13;
			while (*p && isspace((unsigned char)*p))
				p++;
			if (isdigit((unsigned char)*p))
				version = strtol(p, NULL, 10);
			continue;
		}
		mark = strtol(p, &end, 10);
		if (end == p || *end++ != '\t')
			continue;
		p = end;
		row = strtol(p, &end, 10);
		if (end == p || *end++ != '\t')
			continue;
		p = end;
		off = strtol(p, &end, 10);
		if (end == p || *end++ != '\t')
			continue;
		path = end;
		path[strcspn(path, "\r\n")] = '\0';
		slot = vi_gmark_slot(mark, version);
		if (slot >= 0)
			vi_gmark_set(slot, path, row, off, 0);
	}
	fclose(f);
}

static int vi_gmark_open(int slot, int *row, int *off)
{
	int target_row, target_off;
	vi_gmarks_load();
	if (slot < 0 || slot >= VI_GMARKS || !vi_gmark_path[slot])
		return 0;
	target_row = vi_gmark_row[slot];
	target_off = vi_gmark_off[slot];
	ex_edit(vi_gmark_path[slot]->s, vi_gmark_path[slot]->s_n);
	*row = MIN(MAX(target_row, 0), lbuf_len(xb) ? lbuf_len(xb) - 1 : 0);
	*off = MAX(target_off, 0);
	return 1;
}

static void *ec_gmarks(char *loc, char *cmd, char *arg)
{
	char msg[512];
	int any = 0;
	vi_gmarks_load();
	for (int i = 0; i < VI_GMARKS; i++) {
		if (!vi_gmark_path[i])
			continue;
		snprintf(msg, sizeof(msg), "%d %d;%d %s", i,
			vi_gmark_row[i] + 1, vi_gmark_off[i] + 1,
			vi_gmark_path[i]->s);
		ex_print(msg)
		any = 1;
	}
	if (!any)
		ex_print("no global marks")
	return NULL;
}

#include "ex.c"
#include "lbuf.c"
#include "led.c"
#include "regex.c"
#include "ren.c"
#include "term.c"
#include "menu.c"
#include "uc.c"

int vi_hidch;			/* show hidden chars */
/* screen redraw - bit 1: whole screen, bit 2: current line, bit 3: update vi_col */
static int vi_mod;
static int vi_smart_insert;
static int vi_smart_insert_reenter = 'a';
static char vi_word_m[] = "\0leEwW";	/* line word navigation */
static char *vi_word = vi_word_m;
static char *_vi_word = vi_word_m;
static int vi_wsel = 1;
static int vi_rshift;			/* row shift for vi_word */
static int vi_arg;			/* numeric argument */
static char vi_charlast[5];		/* the last character searched via f, t, F, or T */
static int vi_charcmd;			/* the character finding command */
static int vi_ybuf;			/* current yank buffer */
static int vi_col;			/* the column requested by | command */
static int vi_scrollud;			/* scroll amount for ^u and ^d */
static int vi_scrolley;			/* scroll amount for ^e and ^y */
static int vi_cndir = 1;		/* ^n direction */
static int vi_status;			/* permanent status bar */
static int vi_tsm;			/* type of the status message */
static int vi_nlmode;			/* new line mode for vi regions */
static int vi_visual;			/* visual mode */
static int vi_vrow;			/* visual selection anchor row */
static int vi_voff;			/* visual selection anchor offset */
static int vi_defer_status = -1;	/* status line to draw after full redraw */
static int vi_backspace_reenter;	/* keep backspace smooth after re-entry */
#ifdef NEXTVI_NOTERM
static int vi_insert_saved_xrows;	/* editor rows before hiding status */
static int vi_insert_status_dirty;	/* status row needs repaint after insert */
#endif

void *emalloc(size_t size)
{
	void *p;
	if (!(p = malloc(size))) {
		fprintf(stderr, "\nmalloc: out of memory\n");
		exit(EXIT_FAILURE);
	}
	return p;
}

void *erealloc(void *p, size_t size)
{
	if (!(p = realloc(p, size))) {
		fprintf(stderr, "\nrealloc: out of memory\n");
		exit(EXIT_FAILURE);
	}
	return p;
}

static void reverse_in_place(char *str, int len)
{
	char *p1 = str;
	char *p2 = str + len - 1;
	while (p1 < p2) {
		char tmp = *p1;
		*p1++ = *p2;
		*p2-- = tmp;
	}
}

char *nextvi_itoa(int n, char s[])
{
	int i = 0, sign;
	if ((sign = n) < 0)		/* record sign */
		n = -n;			/* make n positive */
	do {				/* generate digits in reverse order */
		s[i++] = n % 10 + '0';	/* get next digit */
	} while ((n /= 10) > 0);	/* delete it */
	if (sign < 0)
		s[i++] = '-';
	s[i] = '\0';
	reverse_in_place(s, i);
	return &s[i];
}

static void vi_drawmsg(char *msg)
{
	RS(2, led_crender(msg, xrows, 0, 0, xcols))
}
#define vi_drawmsg_mpt(msg) { vi_drawmsg(msg); if (!xmpt) xmpt = 1; }

#ifdef NEXTVI_NOTERM
static void vi_keyboard_layout_changed(const char *layout)
{
	char msg[64];

	snprintf(msg, sizeof(msg), "Keyboard [%s]", layout);
	vi_drawmsg_mpt(msg)
	term_commit();
}
#endif

static int vi_scycle_valid;
static int vi_scycle_done;
static int vi_scycle_dir;
static int vi_scycle_kwdcnt;
static int vi_scycle_total;
static int vi_scycle_steps;
static int vi_scycle_row;
static int vi_scycle_off;
static int vi_scycle_currow;
static int vi_scycle_curoff;

static void vi_msg_right(char *msg, int msg_len, const char *right)
{
	char out[512];
	int width = MIN(xcols > 0 ? xcols : 80, msg_len - 1);
	int rlen, mlen, left_cols;

	if (!right || !*right || width <= 0)
		return;
	rlen = strlen(right);
	if (rlen >= width || width >= (int)sizeof(out))
		return;
	mlen = strlen(msg);
	left_cols = width - rlen - 1;
	if (mlen > left_cols)
		mlen = left_cols;
	memset(out, ' ', width);
	out[width] = '\0';
	if (mlen > 0)
		memcpy(out, msg, mlen);
	memcpy(out + width - rlen, right, rlen);
	snprintf(msg, msg_len, "%s", out);
}

static void vi_abbrev_path(char *out, int out_len, const char *path, int modified,
	int max_cols)
{
	const char *name = path && path[0] ? path : "unnamed";
	const char *tail = name;
	int suffix = modified ? 1 : 0;
	int name_len = strlen(name);
	int path_cols;

	if (out_len <= 0)
		return;
	out[0] = '\0';
	if (max_cols <= 0)
		return;
	path_cols = max_cols - 2 - suffix;
	if (path_cols <= 0) {
		snprintf(out, out_len, "%.*s", max_cols, "\"");
		return;
	}
	if (name_len <= path_cols) {
		snprintf(out, out_len, "\"%s\"%s", name, modified ? "*" : "");
		return;
	}
	if (path_cols > 2) {
		tail = name + name_len - (path_cols - 2);
		snprintf(out, out_len, "\"..%s\"%s", tail, modified ? "*" : "");
	} else {
		tail = name + name_len - path_cols;
		snprintf(out, out_len, "\"%s\"%s", tail, modified ? "*" : "");
	}
}

static void vi_status_path(char *msg, int msg_len, const char *path, int modified,
	const char *right)
{
	char out[512], left[512];
	int width = MIN(xcols > 0 ? xcols : 80, msg_len - 1);
	int rlen, left_cols;

	if (msg_len <= 0)
		return;
	msg[0] = '\0';
	if (width <= 0)
		return;
	rlen = strlen(right);
	if (rlen >= width) {
		snprintf(msg, msg_len, "%.*s", width, right);
		return;
	}
	left_cols = width - rlen - 1;
	vi_abbrev_path(left, sizeof(left), path, modified, left_cols);
	memset(out, ' ', width);
	out[width] = '\0';
	memcpy(out, left, MIN((int)strlen(left), left_cols));
	memcpy(out + width - rlen, right, rlen);
	snprintf(msg, msg_len, "%s", out);
}

static int vi_search_count_at(int row, int off, int *idx, int *total)
{
	if (!xkwdrs || !lbuf_len(xb))
		return 0;
	*idx = 0;
	*total = 0;
	for (int r = 0; r < lbuf_len(xb); r++) {
		char *s = lbuf_get(xb, r);
		int offs[xkwdrs->nsubc], boff = 0, flg = REG_NEWLINE;
		if (!s)
			continue;
		while (rset_find(xkwdrs, s + boff, offs, flg) >= 0) {
			int g1 = offs[xgrp], g2 = offs[xgrp + 1];
			flg |= REG_NOTBOL;
			if (g1 < 0) {
				boff += offs[1] > 0 ? offs[1] : 1;
				continue;
			}
			int moff = uc_off(s, boff + g1);
			(*total)++;
			if (r < row || (r == row && moff <= off))
				*idx = *total;
			boff += g2 > 0 ? g2 : 1;
		}
	}
	if (*total && !*idx)
		*idx = 1;
	return *total > 0;
}

static int vi_search_exact_at(int row, int off, int *idx, int *total)
{
	int exact = 0;

	if (!xkwdrs || !lbuf_len(xb))
		return 0;
	*idx = 0;
	*total = 0;
	for (int r = 0; r < lbuf_len(xb); r++) {
		char *s = lbuf_get(xb, r);
		int offs[xkwdrs->nsubc], boff = 0, flg = REG_NEWLINE;
		if (!s)
			continue;
		while (rset_find(xkwdrs, s + boff, offs, flg) >= 0) {
			int g1 = offs[xgrp], g2 = offs[xgrp + 1];
			flg |= REG_NOTBOL;
			if (g1 < 0) {
				boff += offs[1] > 0 ? offs[1] : 1;
				continue;
			}
			int moff = uc_off(s, boff + g1);
			(*total)++;
			if (r == row && moff == off) {
				*idx = *total;
				exact = 1;
			}
			boff += g2 > 0 ? g2 : 1;
		}
	}
	return exact;
}

static void vi_search_counter(char *out, int out_len, int row, int off)
{
	int idx, total;

	out[0] = '\0';
	if (vi_search_count_at(row, off, &idx, &total)) {
		if (vi_scycle_done && vi_scycle_kwdcnt == xkwdcnt &&
				row == vi_scycle_row && off == vi_scycle_off)
			snprintf(out, out_len, "%d/+", idx);
		else
			snprintf(out, out_len, "%d/%d", idx, total);
	}
}

static void vi_draw_search_status(int row, int off)
{
	char msg[512], counter[32];
	const char *pat = xregs['/'] ? xregs['/']->s : "";

	snprintf(msg, sizeof(msg), "%c%s", xkwddir < 0 ? '?' : '/', pat);
	vi_search_counter(counter, sizeof(counter), row, off);
	vi_msg_right(msg, sizeof(msg), counter);
	vi_drawmsg_mpt(msg)
}

static int vi_lbuf_search_wrap(int dir, int pskip, int *row, int *off)
{
	int sdir = dir < 0 ? -1 : 1;

	if (!lbuf_search(xb, xkwdrs, sdir, 0, lbuf_len(xb),
			pskip, 1, row, off))
		return 0;
	if (sdir > 0) {
		*row = 0;
		*off = 0;
	} else {
		*row = lbuf_len(xb) - 1;
		*off = lbuf_eol(xb, *row, 1) + 1;
	}
	return lbuf_search(xb, xkwdrs, sdir, 0, lbuf_len(xb),
			-1, 1, row, off);
}

static void vi_search_cycle_clear(void)
{
	vi_scycle_valid = 0;
	vi_scycle_done = 0;
}

static void vi_search_cycle_begin(int dir, int row, int off, int total)
{
	vi_scycle_valid = 1;
	vi_scycle_done = 0;
	vi_scycle_dir = dir < 0 ? -1 : 1;
	vi_scycle_kwdcnt = xkwdcnt;
	vi_scycle_total = total;
	vi_scycle_steps = 0;
	vi_scycle_row = row;
	vi_scycle_off = off;
	vi_scycle_currow = row;
	vi_scycle_curoff = off;
}

static int vi_search_cycle_current(int dir, int row, int off)
{
	return vi_scycle_valid && vi_scycle_kwdcnt == xkwdcnt &&
		vi_scycle_dir == (dir < 0 ? -1 : 1) &&
		vi_scycle_currow == row && vi_scycle_curoff == off;
}

static void vi_search_cycle_step(int *row, int *off, int total)
{
	if (total != vi_scycle_total) {
		vi_search_cycle_begin(vi_scycle_dir, *row, *off, total);
		return;
	}
	vi_scycle_steps++;
	vi_scycle_currow = *row;
	vi_scycle_curoff = *off;
	if (vi_scycle_steps >= vi_scycle_total) {
		vi_scycle_done = 1;
		*row = vi_scycle_row;
		*off = vi_scycle_off;
		vi_scycle_currow = *row;
		vi_scycle_curoff = *off;
	}
}

static int vi_nextcol(char *ln, int dir, int *off)
{
	int o = ren_off(ln, ren_next(ln, ren_pos(ln, *off), dir));
	if (*rstate->chrs[o] == '\n')
		return -1;
	*off = o;
	return 0;
}

#define vi_drawnum(func) \
{ \
nrow = xrow; \
noff = xoff; \
for (i = 0, ret = 0;; i++) { \
	l1 = ren_next(c, ren_pos(c, noff), 1)-1-xleft; \
	if (l1 > xcols || l1 < 0 || ret || l1 >= rstate->cmax) \
		break; \
	i = i > 99 ? i % 100 : i; \
	nextvi_itoa(i%10 ? i%10 : i, snum); \
	tmp[l1] = *snum; \
	ret = func; \
} } \

static void vi_drawrow(int row)
{
	int l1, i, i1;
	char *c, *s;
	static char ch[5] = "~";
	if (xmpt == 1 && !vi_status && row == xtop + xrows - 1)
		return;
	if (*vi_word && xled) {
		int noff, nrow, ret;
		c = lbuf_get(xb, xrow);
		if (row != xrow+1 || !c || *c == '\n') {
			vi_rshift = (row > xrow+1 && c && *c != '\n');
			s = lbuf_get(xb, row - vi_rshift);
			goto skip;
		}
		char tmp[xcols+3], snum[32];
		memset(tmp, ' ', xcols+1);
		tmp[xcols+1] = '\n';
		tmp[xcols+2] = '\0';
		i1 = isupper((unsigned char)*vi_word);
		if (*vi_word == 'e' || *vi_word == 'E')
			vi_drawnum(lbuf_wordend(xb, i1, 2, &nrow, &noff))
		else if (*vi_word == 'w' || *vi_word == 'W')
			vi_drawnum(lbuf_wordbeg(xb, i1, 2, &nrow, &noff))
		if (*vi_word == 'l') {
			vi_drawnum(vi_nextcol(c, 1, &noff))
			vi_drawnum(vi_nextcol(c, -1, &noff))
		} else
			vi_drawnum(lbuf_wordend(xb, i1, -2, &nrow, &noff))
		tmp[ren_next(c, ren_pos(c, xoff), 1)-1-xleft] = *vi_word;
		RS(2, led_crender(tmp, row - xtop, 0, 0, xcols))
		return;
	}
	s = lbuf_get(xb, row);
	skip:
	rstate += row != xrow;
	if (!s)
		s = row ? ch : ch+1;
	led_select(NULL, 0, -1);
	if (vi_visual && s != ch && s != ch+1) {
		int ar = vi_vrow, ao = vi_voff, cr = xrow, co = xoff;
		if (ar > cr || (ar == cr && ao > co)) {
			swap(&ar, &cr);
			swap(&ao, &co);
		}
		if (row >= ar && row <= cr) {
			int beg, end;
			if (vi_visual == 'V') {
				beg = 0;
				end = lbuf_eol(xb, row, 1);
			} else if (ar == cr) {
				beg = ao;
				end = co;
			} else if (row == ar) {
				beg = ao;
				end = lbuf_eol(xb, row, 1);
			} else if (row == cr) {
				beg = 0;
				end = co;
			} else {
				beg = 0;
				end = lbuf_eol(xb, row, 1);
			}
			ren_state *r = ren_position(s);
			if (r->n) {
				beg = MAX(0, MIN(beg, r->n - 1));
				end = MAX(0, MIN(end, r->n - 1));
				led_select(s, beg, end);
			}
		}
	}
	led_crender(s, row - xtop, 0, xleft, xleft + xcols)
	led_select(NULL, 0, -1);
	rstate = rstates;
}

#ifdef NEXTVI_NOTERM
static void vi_insert_screen_enter(void)
{
	vi_insert_status_dirty = 0;
	if (vi_insert_saved_xrows)
		return;
	vi_insert_saved_xrows = xrows;
	xrows = MIN(xrows + 1, NEXTVI_DISPLAY_ROWS + 1);
	xmpt = 0;
	vi_drawrow(xtop + xrows - 1);
}

static void vi_insert_screen_leave(void)
{
	if (!vi_insert_saved_xrows)
		return;
	xrows = vi_insert_saved_xrows;
	vi_insert_saved_xrows = 0;
	vi_insert_status_dirty = 1;
}
#else
static void vi_insert_screen_enter(void) {}
static void vi_insert_screen_leave(void) {}
#endif

/* redraw the screen */
static void vi_drawagain(int i)
{
	for (; i < xtop + xrows; i++)
		vi_drawrow(i);
}

/* update the screen */
static void vi_drawupdate(int i)
{
	int n;
	term_pos(0, 0);
	term_room(i);
	if (i < 0) {
		n = MIN(-i, xrows);
		for (i = 0; i < n; i++)
			vi_drawrow(xtop + xrows - n + i);
	} else {
		n = MIN(i, xrows);
		for (i = n-1; i >= 0; i--)
			vi_drawrow(xtop + i);
	}
}

static char *vi_prompt(char *msg, char *insert, int *ret, int *kmap, int *mlen)
{
	sbuf_smake(sb, xcols)
	sbuf_str(sb, msg)
	*mlen = sb->s_n;
	term_pos(xrows, 0);
	if (msg[0] == ':' && !msg[1])
		led_prompt_width(conf_hwwidth);
	*ret = led_prompt(sb, insert, kmap, NULL, 0, 1) == '\n';
	led_prompt_width(0);
	term_cursor(1);
	return sb->s;
}

static char *vi_enprompt(char *msg, char *insert, int *ret, int *mlen)
{
	int kmap = 0;
	return vi_prompt(msg, insert, ret, &kmap, mlen);
}

static int vi_yankbuf(void)
{
	int c = term_read(TK_CTL('l'));
	if (c == '"')
		return term_read(TK_CTL('l'));
	term_dec()
	return 0;
}

static int vi_prefix(void)
{
	int n = 0;
	int c = term_read(TK_CTL('l'));
	if (c >= '1' && c <= '9') {
		while (c >= '0' && c <= '9') {
			n = n * 10 + c - '0';
			c = term_read(TK_CTL('l'));
		}
	}
	return n;
}

static int vi_digit(void)
{
	int c = term_read(0);
	if (c >= '0' && c <= '9')
		return c - '0';
	return -1;
}

static int vi_off2col(struct lbuf *lb, int row, int off)
{
	char *ln = lbuf_get(lb, row);
	return ln ? ren_pos(ln, off) : 0;
}

static int vi_col2off(struct lbuf *lb, int row, int col)
{
	char *ln = lbuf_get(lb, row);
	if (!ln)
		return 0;
	ren_state *r = ren_position(ln);
	if (col >= r->cmax)
		return r->col[r->cmax - 1];
	return r->col[col];
}

static int vi_hardwrap_break(char *ln, int width, int *end, int *next)
{
	ren_state *r = ren_position(ln);
	int n = r->n && *r->chrs[r->n - 1] == '\n' ? r->n - 1 : r->n;
	int cut = 0, br = -1;
	if (width <= 0 || !n || r->pos[n] <= width)
		return 0;
	while (cut < n && r->pos[cut] + r->wid[cut] <= width)
		cut++;
	if (cut < n && uc_isspace(r->chrs[cut]))
		br = cut + 1;
	else {
		for (int i = cut; i > 0; i--) {
			char *ch = r->chrs[i - 1];
			if (uc_isspace(ch) || ((unsigned char)*ch < 0x7f &&
						strchr(",.;:!?)]}>/-", *ch))) {
				br = i;
				break;
			}
		}
	}
	if (br > 0) {
		*end = br;
		*next = br;
		if (uc_isspace(r->chrs[br - 1])) {
			*end = br - 1;
			while (*next < n && uc_isspace(r->chrs[*next]))
				(*next)++;
		}
	} else {
		*end = cut;
		*next = cut;
	}
	if (*end <= 0)
		*end = cut > 0 ? cut : 1;
	if (*next <= *end)
		*next = *end;
	return 1;
}

static int vi_forced_line(char *ln)
{
	return HWBRK_IS(ln);
}

static void vi_hardwrap_skip_marker(int *row, int *off)
{
	if (!vi_forced_line(lbuf_get(xb, *row)) || *off > 0)
		return;
	*off = MIN(1, lbuf_eol(xb, *row, 1));
}

static void vi_hardwrap_emit(sbuf *out, char *txt, int cursor,
	int row, int *nrow, int *noff)
{
	int first = 1, chars = 0, marker_sep = 0, seg = 0;
	while (*txt) {
		int end, next, len;
		ren_state *r = ren_position(txt);
		if (!vi_hardwrap_break(txt, conf_hwwidth, &end, &next)) {
			end = next = r->n;
			if (end && *r->chrs[end - 1] == '\n')
				end = next = end - 1;
		}
		if (!first)
			sbuf_str(out, marker_sep ? HWBRK : HWBRK_NOSPACE)
		sbuf_mem(out, txt, r->chrs[end] - txt)
		sbuf_chr(out, '\n')
		len = end;
		if (*nrow < 0 && (cursor < chars + len ||
					(!txt[next] && cursor <= chars + len))) {
			*nrow = row + seg;
			*noff = (first ? 0 : 1) + MAX(0, cursor - chars);
		}
		if (!txt[next])
			break;
		chars += next;
		marker_sep = next > end;
		txt = r->chrs[next];
		first = 0;
		seg++;
	}
	if (*nrow < 0) {
		*nrow = row + seg;
		*noff = (first ? 0 : 1) + MAX(0, cursor - chars);
	}
	sbuf_null(out)
}

static int vi_hardwrap_reflow(int row)
{
	int beg = row, end, cur = 0, cursor = 0, nrow = -1, noff = 0;
	int has_cursor = 0;
	char *ln;
	if (conf_hwwidth <= 0 || !lbuf_get(xb, row))
		return 0;
	while (beg > 0 && vi_forced_line(lbuf_get(xb, beg)))
		beg--;
	for (end = beg + 1; end < lbuf_len(xb) &&
			vi_forced_line(lbuf_get(xb, end)); end++);
	ln = lbuf_get(xb, beg);
	if (end == beg + 1 && vi_off2col(xb, beg, lbuf_eol(xb, beg, 1)) <=
			conf_hwwidth)
		return 0;
	sbuf_smake(txt, lbuf_s(ln)->len + 1)
	for (int i = beg; i < end; i++) {
		ln = lbuf_get(xb, i);
		int body = vi_forced_line(ln) ? HWBRK_LEN : 0;
		int force_sep = HWBRK_SEP(ln);
		int mark = !!body, skip = 0;
		ren_state *r = ren_position(ln + body);
		int n = r->n && *r->chrs[r->n - 1] == '\n' ? r->n - 1 : r->n;
		while (force_sep && skip < n && uc_isspace(r->chrs[skip]))
			skip++;
		int sep = force_sep && i > beg && txt->s_n &&
			txt->s[txt->s_n - 1] != ' ' && skip < n;
		if (i == xrow) {
			has_cursor = 1;
			cursor = cur + sep + MAX(0, xoff - mark - skip);
			cursor = MIN(cursor, cur + sep + n - skip);
		}
		if (sep) {
			sbuf_chr(txt, ' ')
			cur++;
		}
		sbuf_mem(txt, r->chrs[skip], r->chrs[n] - r->chrs[skip])
		cur += n - skip;
	}
	sbufn_null(txt)
	sbuf_smake(out, txt->s_n + 8)
	vi_hardwrap_emit(out, txt->s, cursor, beg, &nrow, &noff);
	int old_lines = end - beg, new_lines = 0;
	for (char *s = out->s; *s;)
		if (*s++ == '\n')
			new_lines++;
	new_lines = MAX(new_lines, 1);
	lbuf_edit(xb, out->s, beg, end, 0, noff);
	if (has_cursor) {
		xrow = nrow;
		xoff = noff;
	} else if (xrow >= end)
		xrow += new_lines - old_lines;
	free(out->s);
	free(txt->s);
	return 1;
}

static void vi_hardwrap_range(int row, int lines)
{
	int end = row + MAX(lines, 1) - 1;
	if (conf_hwwidth <= 0)
		return;
	for (int r = row; r <= end && r < lbuf_len(xb); r++)
		vi_hardwrap_reflow(r);
}

static void vi_hardwrap_all(void)
{
	if (conf_hwwidth <= 0)
		return;
	for (int r = 0; r < lbuf_len(xb); r++)
		vi_hardwrap_reflow(r);
}

static int vi_linecount(char *s)
{
	int n = 0;
	while (*s)
		if (*s++ == '\n')
			n++;
	return MAX(n, 1);
}

static int vi_search(int cmd, int cnt, int *row, int *off, int msg)
{
	int i, dir, sdir, ret, explicit;
	char vi_msg[512];
	explicit = cmd == '/' || cmd == '?';
	if (explicit) {
		char sign[4] = {cmd};
		char *kw = vi_prompt(sign, NULL, &ret, &xkmap, &i);
		vi_drawmsg_mpt(kw)
		if (!ret) {
			free(kw);
			return 1;
		}
		ex_krsset(kw + i, cmd == '/' ? +2 : -2);
		if (!xkwdrs)
			vi_drawmsg_mpt("syntax error")
		free(kw);
		vi_search_cycle_clear();
	} else if (msg)
		ex_krsset(xregs['/'] ? xregs['/']->s : NULL, xkwddir);
	if (!lbuf_len(xb) || (!xkwddir || !xkwdrs))
		return 1;
	dir = cmd == 'N' ? -xkwddir : xkwddir;
	sdir = dir < 0 ? -1 : 1;
	if (msg && !explicit && vi_search_cycle_current(sdir, *row, *off) &&
			vi_scycle_done) {
		if (vi_status)
			xmpt = 0;
		else
			vi_draw_search_status(*row, *off);
		return 0;
	}
	if (msg && !explicit && !vi_search_cycle_current(sdir, *row, *off)) {
		int idx, total;
		vi_search_cycle_clear();
		if (vi_search_exact_at(*row, *off, &idx, &total))
			vi_search_cycle_begin(sdir, *row, *off, total);
	}
	for (i = 0; i < cnt; i++) {
		int idx, total, matched, in_cycle;
		int pskip = msg ? (dir > 0 ? 1 : -1) : -1;
		in_cycle = msg && !explicit &&
			vi_search_cycle_current(sdir, *row, *off);
		matched = msg ? !vi_lbuf_search_wrap(sdir, pskip, row, off) :
			!lbuf_search(xb, xkwdrs, sdir, 0, lbuf_len(xb),
				pskip, 1, row, off);
		if (!matched) {
			if (msg) {
				snprintf(vi_msg, sizeof(vi_msg), "\"%s\" not found %d/%d",
						xregs['/'] ? xregs['/']->s : "", i, cnt);
				vi_drawmsg_mpt(vi_msg)
			}
			return 1;
		}
		if (msg && vi_search_count_at(*row, *off, &idx, &total)) {
			if (explicit || !in_cycle)
				vi_search_cycle_begin(sdir, *row, *off, total);
			else
				vi_search_cycle_step(row, off, total);
			if (vi_scycle_done)
				break;
		}
	}
	if (msg) {
		if (vi_status)
			xmpt = 0;
		else
			vi_draw_search_status(*row, *off);
	}
	return 0;
}

static int vi_gmark_key(int key, int *row, int *off);

/* read a line motion */
static int vi_motionln(int *row, int cmd, int cnt)
{
	int var, off, ret, c = term_read(TK_CTL('l'));
	switch (c) {
	case '\n':
	case '+':
	case 'j':
		*row = MIN(*row + cnt, lbuf_len(xb) - 1);
		break;
	case 'k':
	case '-':
		*row = MAX(*row - cnt, 0);
		break;
	case '\'':
		var = term_read(0);
		if ((ret = vi_gmark_key(var, row, &off)))
			return ret < 0 ? -1 : c;
		if (lbuf_jump(xb, var, row, &off))
			return -1;
		break;
	case 'G':
		*row = vi_arg ? cnt - 1 : lbuf_len(xb) - 1;
		break;
	case 'H':
		*row = MIN(xtop + cnt - 1, lbuf_len(xb) - 1);
		break;
	case 'L':
		*row = MIN(xtop + xrows - 1 - cnt + 1, lbuf_len(xb) - 1);
		break;
	case 'M':
		*row = MIN(xtop + xrows / 2, lbuf_len(xb) - 1);
		break;
	default:
		if (c == cmd) {
			*row = MIN(*row + cnt - 1, lbuf_len(xb) - 1);
			break;
		}
		if (c == '%' && vi_arg) {
			if (cnt > 100)
				return -1;
			*row = lbuf_len(xb) * cnt / 100;
			break;
		}
		term_dec()
		return 0;
	}
	if (*row < 0)
		*row = 0;
	return c;
}

static char *vi_curword(struct lbuf *lb, int row, int off, int n, int ex)
{
	char *ln = lbuf_get(lb, row);
	if (!ln || !n)
		return NULL;
	off = ren_noeol(ln, off);
	char **chrs = rstate->chrs;
	int cap = rstate->n;
	int end = off;
	for (int i = 0; i < n && end < cap; i++)
		while (uc_kind(chrs[end++]) == 1);
	for (; off > 0 && uc_kind(chrs[off - 1]) == 1; off--);
	if (!end || --end == off)
		return NULL;
	sbuf_smake(sb, 64)
	if (n <= 1) {
		sbuf_str(sb, "\\<")
		sbuf_mem(sb, chrs[off], chrs[end] - chrs[off])
		sbuf_str(sb, "\\>")
	} else
		ex_regesc(sb, chrs[off], chrs[end], ex);
	sbufn_ret(sb, sb->s)
}

static void vi_regput(int c, const char *s, int lnmode)
{
	if (lnmode) {
		sbuf *i_s;
		for (int i = 8; i > 0; i--)
			if ((i_s = xregs['0'+i]))
				ex_regput('0' + i + 1, i_s->s, 0);
		ex_regput('1', s, 0);
	} else if (xregs[c])
		ex_regput('0', xregs[c]->s, 0);
	ex_regput(tolower(c), s, isupper(c));
}

rset *fsincl;
static int fspos;
static int fsdir;

void dir_calc(char *path)
{
	struct dirent *dirp;
	struct stat statbuf;
	int i = 0, ret;
	char *cpath, *ptrs[1024];
	int plen[1024];
	DIR *dp, *sdp, *dps[1024];
	unsigned int pathlen = strlen(path), len;
	if (!(dp = opendir(path)))
		return;
	cpath = emalloc(pathlen + 1024);
	strcpy(cpath, path);
	sbuf_smake(sb, 1024)
	temp_pos(1, -1, 0, 0);
	fspos = 0;
	for (;;) {
		while ((dirp = readdir(dp))) {
			len = strlen(dirp->d_name)+1;
			if (strcmp(dirp->d_name, ".") == 0 ||
				strcmp(dirp->d_name, "..") == 0 ||
				len > 1023)
				continue;
			cpath[pathlen] = '/';
			memcpy(&cpath[pathlen+1], dirp->d_name, len);
			ret = lstat(cpath, &statbuf);
			if (ret >= 0 && S_ISDIR(statbuf.st_mode)) {
				if (!(sdp = opendir(cpath)) || i >= LEN(ptrs))
					break;
				dps[i] = sdp;
				ptrs[i] = cpath;
				cpath = emalloc(pathlen + 1024);
				memcpy(cpath, ptrs[i], pathlen + len);
				plen[i++] = pathlen + len;
			} else if (ret >= 0 && S_ISREG(statbuf.st_mode))
				if (!fsincl || rset_match(fsincl, cpath, 0)) {
					sbuf_mem(sb, cpath, (int)(pathlen + len))
					sbuf_chr(sb, '\n')
				}
		}
		closedir(dp);
		free(cpath);
		if (i > 0) {
			dp = dps[--i];
			pathlen = plen[i];
			cpath = ptrs[i];
		} else
			break;
	}
	sbuf_null(sb)
	if (sb->s_n > 1)
		temp_write(1, sb->s);
	free(sb->s);
}

#define fssearch() \
len = lbuf_s(path)->len; \
path[len] = '\0'; \
ret = ex_edit(path, len); \
path[len] = '\n'; \
if (ret < 0) { \
	*row = 0; *off = 0; \
} else if (ret && xrow) { \
	*row = xrow; *off = xoff; /* short circuit */ \
	if (!vi_search('n', cnt, row, off, 0)) \
		return 1; \
	++*off; \
} else { \
	*row = 0; *off = 0; \
} \
if (!vi_search(*row ? 'N' : 'n', cnt, row, off, 0)) \
	return 1; \

static int fs_search(int cnt, int *row, int *off)
{
	char *path;
	int again = 0, ret, len;
	wrap:
	while (fspos < lbuf_len(tempbufs[1].lb)) {
		path = tempbufs[1].lb->ln[fspos++];
		fssearch()
	}
	if (fspos == lbuf_len(tempbufs[1].lb) && !again) {
		fspos = 0;
		again = 1;
		goto wrap;
	}
	return 0;
}

static int fs_searchback(int cnt, int *row, int *off)
{
	char *path;
	int ret, len;
	while (--fspos >= 0) {
		path = tempbufs[1].lb->ln[fspos];
		fssearch()
	}
	return 0;
}

static void vc_status(int type)
{
	int l, col;
	unsigned int cp;
	char cbuf[8] = "", vi_msg[512], *c;
	col = vi_off2col(xb, xrow, xoff);
	col = ren_cursor(lbuf_get(xb, xrow), col) + 1;
	if (type && lbuf_get(xb, xrow)) {
		c = rstate->chrs[xoff];
		uc_code(cp, c, l)
		memcpy(cbuf, c, l);
		snprintf(vi_msg, sizeof(vi_msg), "<%s> 0x%x 0%o %u %dL %dW S%td O%d C%d",
			cbuf, cp, cp, cp, l, rstate->wid[xoff], c - lbuf_get(xb, xrow),
			xoff, col);
	} else {
		char counter[32], right[128];
		vi_search_counter(counter, sizeof(counter), xrow, xoff);
		snprintf(right, sizeof(right), "%dL %d%% L%d C%d B%td%s%s",
			lbuf_len(xb), xrow * 100 / MAX(1, lbuf_len(xb)-1),
			xrow+1, col,
			istempbuf(ex_buf) ? tempbufs - ex_buf - 1 : ex_buf - bufs,
			counter[0] ? " " : "", counter);
		if ((int)strlen(right) >= (xcols > 0 ? xcols : 80))
			snprintf(right, sizeof(right), "L%d C%d", xrow+1, col);
		vi_status_path(vi_msg, sizeof(vi_msg), xb_path, xb->modified, right);
	}
	vi_drawmsg_mpt(vi_msg)
}

static void vc_status_defer(int type)
{
	vi_defer_status = type;
}

static void vi_gmark_sync_after_open(int row)
{
	for (int i = xbufcur - 1; i >= 0 && bufs[i].mtime == -1; i--)
		ex_bufpostfix(&bufs[i], 1);
	vc_status_defer(0);
	xtop = MAX(0, row - xrows / 2);
	vi_mod |= 1;
}

static int vi_gmark_key(int key, int *row, int *off)
{
	if (!isdigit((unsigned char)key))
		return 0;
	if (!vi_gmark_open(key - '0', row, off))
		return -1;
	vi_gmark_sync_after_open(*row);
	return 1;
}

/* read a motion */
static int vi_motion(int vc, int *row, int *off)
{
	static rset *bre;
	static int lkwdcnt;
	static int cadir = 1;
	char *cs;
	int cnt = vi_arg ? vi_arg : 1;
	int mv, i, dir, var;

	if ((mv = vi_motionln(row, 0, cnt))) {
		*off = -1;
		return mv;
	}
	mv = term_read(TK_CTL('l'));
	switch (mv) {
	case ',':
	case ';':
		if (!vi_charlast[0])
			return -1;
		if (mv == ',')
			mv = vi_charcmd == 'F' || vi_charcmd == 'T'
				? tolower(vi_charcmd) : toupper(vi_charcmd);
		else
			mv = vi_charcmd;
		if (lbuf_findchar(xb, vi_charlast, mv, cnt, row, off))
			return -1;
		break;
	case 'h':
	case 'l':
		if (!(cs = lbuf_get(xb, *row)))
			return -1;
		dir = mv == 'h' ? -1 : +1;
		for (i = 0; i < cnt; i++)
			if (vi_nextcol(cs, dir, off))
				break;
		break;
	case ' ':
	case 127:
	case TK_CTL('h'):
		dir = mv == ' ' ? +1 : -1;
		cs = lbuf_get(xb, *row);
		var = cs ? ren_position(cs)->n : 0;
		i = *off;
		*off += cnt * dir;
		if (vi_nlmode) {
			*off = *off < 0 ? 0 : *off;
			break;
		}
		if (*off < 0 || *off >= var) {
			cnt -= dir > 0 ? var - i : i;
			*off = dir > 0 ? var : 0;
			while ((cs = lbuf_get(xb, *row + dir))) {
				*row += dir;
				var = uc_slen(cs);
				if (cnt - var <= 0) {
					*off = dir < 0 ? var - cnt : cnt;
					break;
				}
				cnt -= var;
			}
		}
		if (!vc && dir > 0 && lbuf_get(xb, *row + dir)
				&& (var < 2 || *off >= var - 1)) {
			*row += dir;
			*off = 0;
		}
		if (!vi_nlmode)
			vi_hardwrap_skip_marker(row, off);
		break;
	case 'f':
	case 'F':
	case 't':
	case 'T':
		if (!(cs = led_read(&xkmap, term_read(0))))
			return -1;
		strcpy(vi_charlast, cs);
		vi_charcmd = mv;
		if (lbuf_findchar(xb, cs, mv, cnt, row, off))
			return -1;
		break;
	case 'b':
	case 'B':
		var = mv == 'B';
		for (i = 0; i < cnt; i++)
			if (lbuf_wordend(xb, var, -(vi_nlmode+1), row, off))
				break;
		if (!vi_nlmode)
			vi_hardwrap_skip_marker(row, off);
		break;
	case 'e':
	case 'E':
		var = mv == 'E';
		for (i = 0; i < cnt; i++)
			if (lbuf_wordend(xb, var, vi_nlmode+1, row, off))
				break;
		if (!vi_nlmode)
			vi_hardwrap_skip_marker(row, off);
		break;
	case 'w':
	case 'W':
		var = mv == 'W';
		for (i = 0; i < cnt; i++)
			if (lbuf_wordbeg(xb, var, vi_nlmode+1, row, off))
				break;
		if (!vi_nlmode)
			vi_hardwrap_skip_marker(row, off);
		break;
	case '(':
	case ')':
		dir = mv == '(' ? 1 : -1;
		if (!bre)
			bre = rset_smake("^[.?!]+['\\])]*(?:[ \t]+\n?|\n)", 0);
		int subs[2], org;
		for (i = 0; i < cnt; i++) {
			var = *row;
			org = *off;
			for (; (cs = lbuf_get(xb, *row)) && *cs == '\n'; *row += dir);
			if (*row != var) {
				*off = MAX(0, lbuf_indents(xb, *row));
				if (dir > 0)
					continue;
				*off = lbuf_eol(xb, *row, 1);
			}
			while (!lbuf_next(xb, dir, row, off)) {
				cs = rstate->chrs[*off];
				if (*off == 0 && *cs == '\n') {
					if (dir < 0 && (var - *row) > 1)
						*row += 1;
					*off = MAX(0, lbuf_indents(xb, *row));
					break;
				} else if (rset_find(bre, cs, subs, 0) >= 0) {
					if (var == *row && rstate->chrs[org] == cs + subs[1])
						continue;
					if (!cs[subs[1]]) {
						if (dir < 0 && *row + 1 == var)
							continue;
						*row += 1;
						*off = MAX(0, lbuf_indents(xb, *row));
					} else
						*off += uc_off(cs, subs[1]);
					break;
				}
			}
		}
		return mv;
	case '{':
	case '}':
	case '[':
	case ']':
		dir = mv == '{' || mv == '[' ? 1 : -1;
		var = mv == '[' || mv == ']' ? '\n' : '{';
		for (i = 0; i < cnt; i++)
			if (lbuf_sectionbeg(xb, dir, row, off, var))
				break;
		break;
	case TK_CTL(']'):	/* this is also ^5 on some systems */
	case TK_CTL('p'):
		#define open_saved(n) \
		vi_gmark_open(n, row, off); \

		if (vi_arg && (cs = vi_curword(xb, *row, *off, cnt, 0))) {
			ex_krsset(cs, +1);
			free(cs);
		}
		struct buf* tmpex_buf = istempbuf(ex_buf) ? ex_pbuf : ex_buf;
		if (mv == TK_CTL(']')) {
			if (vi_arg || lkwdcnt != xkwdcnt)
				term_exec("", 1, '&')
			lkwdcnt = xkwdcnt;
			fspos += fsdir < 0 ? 1 : 0;
			fspos = MIN(fspos, lbuf_len(tempbufs[1].lb));
			fs_search(1, row, off);
			fsdir = 1;
		} else {
			fspos -= fsdir > 0 ? 1 : 0;
			if (!fs_searchback(1, row, off)) {
				open_saved(0)
				fsdir = 0;
			} else
				fsdir = -1;
			fspos = MAX(fspos, 0);
		}
		if (tmpex_buf != ex_buf)
			ex_pbuf = tmpex_buf;
		for (i = xbufcur-1; i >= 0 && bufs[i].mtime == -1; i--)
			ex_bufpostfix(&bufs[i], 1);
		vc_status_defer(0);
		xtop = MAX(0, *row - xrows / 2);
		vi_mod |= 1;
		break;
	case TK_CTL('t'):
		if (vi_arg >= VI_GMARKS)
			break;
		vi_gmarks_load();
		vi_gmark_set(vi_arg, xb_path, *row, *off, 1);
		break;
	case '0':
		*off = 0;
		break;
	case '^':
		*off = lbuf_indents(xb, *row);
		break;
	case '$':
		*off = lbuf_eol(xb, *row, 1);
		break;
	case '|':
		vi_col = cnt - 1;
		break;
	case '/':
	case '?':
	case 'n':
	case 'N':
		if (vi_search(mv, cnt, row, off, 1))
			return -1;
		xtop = MAX(0, *row - xrows / 2);
		vi_mod |= mv == '/' || mv == '?';
		break;
	case '*':
	case TK_CTL('a'):
		if (mv == TK_CTL('a') || vi_arg) {
			if (!(cs = vi_curword(xb, *row, *off, cnt, 0)))
				return -1;
			ex_krsset(cs, +1);
			free(cs);
		}
		if (vi_search(cadir < 0 ? 'N' : 'n', 1, row, off, 1))
			cadir = -cadir;
		else if (*row < xtop || *row >= xtop + xrows - 1)
			xtop = MAX(0, *row - xrows / 2);
		break;
	case '`':
		var = term_read(0);
		if ((dir = vi_gmark_key(var, row, off)))
			return dir < 0 ? -1 : mv;
		if (lbuf_jump(xb, var, row, off))
			return -1;
		break;
	case '%':
		if (lbuf_pair(xb, "()[]{}", 6, row, off))
			return -1;
		break;
	default:
		return 0;
	}
	return mv;
}

static void vi_yank(int r1, int o1, int r2, int o2, int lnmode)
{
	sbuf rsb;
	lbuf_region(xb, &rsb, r1, lnmode ? 0 : o1, r2, lnmode ? -1 : o2);
	vi_regput(vi_ybuf, rsb.s, lnmode);
	free(rsb.s);
	xrow = r1;
	xoff = lnmode ? xoff : o1;
}

static void vi_delete(int r1, int o1, int r2, int o2, int lnmode)
{
	sbuf rsb;
	if (!lnmode && r1 == r2 && vi_forced_line(lbuf_get(xb, r1)) && o1 <= 0)
		o1 = MIN(o2, 1);
	lbuf_region(xb, &rsb, r1, lnmode ? 0 : o1, r2, lnmode ? -1 : o2);
	vi_regput(vi_ybuf, rsb.s, lnmode);
	free(rsb.s);
	if (lnmode)
		lbuf_edit(xb, NULL, r1, r2 + 1, 0, 0);
	else {
		rsb.s = "";
		rsb.s_n = 0;
		char *s = lbuf_joinsb(xb, r1, r2, &rsb, &o1, &o2);
		lbuf_edit(xb, s, r1, r2 + 1, o1, o1);
		free(s);
	}
	xrow = r1;
	xoff = lnmode ? lbuf_indents(xb, xrow) : o1;
	/* Let insert-mode backspace consume an emptied hard-wrap marker first. */
	if (!(r1 == r2 && !lnmode && vi_forced_line(lbuf_get(xb, r1)) &&
				lbuf_eol(xb, r1, 1) <= 1))
		vi_hardwrap_range(r1, r2 - r1 + 1);
}

static int vi_hardwrap_backspace_boundary(void)
{
	char *ln = lbuf_get(xb, xrow);
	int eol = lbuf_eol(xb, xrow, 1);
	if (!vi_forced_line(ln) || xrow <= 0)
		return 0;
	if (eol <= 1) {
		if (xoff > 2)
			return 0;
		lbuf_edit(xb, "\n", xrow, xrow + 1, 0, 0);
		xoff = 0;
		return 1;
	}
	if (xoff <= 1) {
		vi_delete(xrow, 1, xrow, 2, 0);
		return 1;
	}
	return 0;
}

static void vi_indents(char *ln, int *l)
{
	if (!xai || !ln)
		ln = "";
	char *pln = ln;
	for (; *ln == ' ' || *ln == '\t'; ln++);
	*l = ln - pln;
}

static int vi_change(int r1, int o1, int r2, int o2, int lnmode)
{
	char *post, *ln = lbuf_get(xb, r1);
	sbuf rsb;
	int key, tlen, l1, l2 = 1, postn = 1;
	sbuf_smake(sb, xcols)
	if (lnmode || !ln) {
		vi_indents(ln, &l1);
		o1 = l1;
		post = "\n";
		tlen = -1;
		lbuf_region(xb, &rsb, r1, 0, r2, -1);
	} else {
		l1 = uc_chr(ln, o1) - ln;
		post = uc_chr(lbuf_get(xb, r2), o2);
		l2 = uc_chrn(post, -1, &postn) - post;
		tlen = lbuf_s(ln)->len+1;
		lbuf_region(xb, &rsb, r1, o1, r2, o2);
	}
	vi_regput(vi_ybuf, rsb.s, lnmode);
	free(rsb.s);
	vi_insert_screen_enter();
	term_pos(r1 - xtop < 0 ? 0 : r1 - xtop, 0);
	term_room(r1 < xtop ? xtop - xrow : r1 - r2 -
			(*vi_word && ln && *ln != '\n' && r1 != r2));
	xrow = r1;
	if (r1 < xtop)
		xtop = r1;
	sbuf_mem(sb, ln, l1)
	key = led_input(sb, post, postn, r1 - (r1 - r2), 0, &postn);
	if (postn + l2 != tlen || memcmp(ln + l1, sb->s + l1, tlen - l2 - l1))
		lbuf_edit(xb, sb->s, r1, r2 + 1, o1, xoff);
	free(sb->s);
	vi_insert_screen_leave();
	vi_mod |= 1;
	return key;
}

static void vi_case(int r1, int o1, int r2, int o2, int lnmode, int cmd)
{
	sbuf rsb;
	lbuf_region(xb, &rsb, r1, lnmode ? 0 : o1, r2, lnmode ? -1 : o2);
	char *s = rsb.s;
	while (uc_len(s)) {
		int c = (unsigned char) s[0];
		if (c <= 0x7f) {
			if (cmd == 'u')
				s[0] = tolower(c);
			if (cmd == 'U')
				s[0] = toupper(c);
			if (cmd == '~')
				s[0] = islower(c) ? toupper(c) : tolower(c);
		}
		s += uc_len(s);
	}
	if (lnmode) {
		lbuf_edit(xb, rsb.s, r1, r2 + 1, 0, 0);
		free(rsb.s);
	} else {
		s = lbuf_joinsb(xb, r1, r2, &rsb, &o1, &o2);
		free(rsb.s);
		lbuf_edit(xb, s, r1, r2 + 1, o1, o2);
		free(s);
	}
	xrow = r2;
	xoff = lnmode ? lbuf_indents(xb, r2) : o2;
}

static void vi_shift(int r1, int r2, int dir, int count)
{
	sbuf_smake(sb, 1024)
	char *ln;
	int i, c;
	for (i = r1; i <= r2; i++) {
		if (!(ln = lbuf_get(xb, i)))
			continue;
		for (c = 0; c < count; c++) {
			if (dir < 0) {
				if (*ln != ' ' && *ln != '\t')
					break;
				ln++;
			} else if (*ln != '\n' || r1 == r2)
				sbuf_chr(sb, '\t')
		}
		sbufn_str(sb, ln)
		lbuf_edit(xb, sb->s, i, i + 1, 0, 0);
		sbuf_cut(sb, 0)
	}
	xoff = lbuf_indents(xb, r1);
	free(sb->s);
}

static int vi_backward_word_includes_cursor(int mv, int row, int off)
{
	char *ln = lbuf_get(xb, row);
	ren_state *r = ln ? ren_position(ln) : NULL;

	if (!r || off <= 0 || off >= r->n ||
			uc_isspace(r->chrs[off]) || uc_isspace(r->chrs[off - 1]))
		return 0;
	return mv == 'B' || uc_kind(r->chrs[off]) == uc_kind(r->chrs[off - 1]);
}

static int vc_motion(int cmd)
{
	int r1 = xrow, r2 = xrow;	/* region rows */
	int o1 = xoff, o2;		/* visual region columns */
	int cr, co;
	int lnmode = 0;			/* line-based region */
	int mv = vi_prefix();
	term_dec()
	if (mv)
		vi_arg = mv;
	o1 = ren_noeol(lbuf_get(xb, r1), o1);
	cr = r1;
	co = o1;
	o2 = o1;
	if ((mv = vi_motionln(&r2, cmd, vi_arg ? vi_arg : 1)))
		o2 = -1;
	else if (!(mv = vi_motion(1, &r2, &o2)))
		return 0;
	if (mv < 0)
		return 0;
	lnmode = o2 < 0;
	if (lnmode) {
		o1 = 0;
		o2 = lbuf_eol(xb, r2, r1 >= r2);
	}
	if (r1 > r2) {
		swap(&r1, &r2);
		swap(&o1, &o2);
	} else if (r1 == r2 && o1 > o2)
		swap(&o1, &o2);
	ren_state *r = (ren_state*)lbuf_get(xb, r1);
	r = r ? ren_position((char*)r) : NULL;
	o1 = r ? MAX(0, MIN(o1, r->n)) : 0;
	if (!lnmode && strchr("fFtTeE%", mv))
		if (o2 < lbuf_eol(xb, r2, 2))
			o2++;
	if (!lnmode && (cmd == 'd' || cmd == 'c') && strchr("bB", mv) &&
			r2 == cr && o2 == co &&
			vi_backward_word_includes_cursor(mv, cr, co) &&
			o2 < lbuf_eol(xb, r2, 2))
		o2++;
	if (cmd == 'y') {
		vi_yank(r1, o1, r2, o2, lnmode);
		return 0;
	}
	mv = lbuf_len(xb);
	if (cmd == 'd')
		vi_delete(r1, o1, r2, o2, lnmode);
	else if (cmd == 'c')
		return vi_change(r1, o1, r2, o2, lnmode);
	else if (cmd == '~' || cmd == 'u' || cmd == 'U')
		vi_case(r1, o1, r2, o2, lnmode, cmd);
	else if (cmd == '>' || cmd == '<')
		vi_shift(r1, r2, cmd == '>' ? +1 : -1,
			lnmode ? 1 : vi_arg ? vi_arg : 1);
	else if (cmd == TK_CTL('w'))
		vi_shift(r1, r2, -1, INT_MAX / 2);
	vi_mod |= r1 != r2 || mv != lbuf_len(xb) ? 1 : 2;
	return 0;
}

static int vc_visual_op(int cmd)
{
	int r1 = vi_vrow, o1 = vi_voff;
	int r2 = xrow, o2 = xoff;
	int lnmode = vi_visual == 'V';
	if (r1 > r2 || (r1 == r2 && o1 > o2)) {
		swap(&r1, &r2);
		swap(&o1, &o2);
	}
	if (lnmode) {
		o1 = 0;
		o2 = lbuf_eol(xb, r2, 1);
	} else if (o2 < lbuf_eol(xb, r2, 2))
		o2++;
	ren_state *r = (ren_state*)lbuf_get(xb, r1);
	r = r ? ren_position((char*)r) : NULL;
	o1 = r ? MAX(0, MIN(o1, r->n)) : 0;
	vi_visual = 0;
	vi_mod |= 1;
	int mv = lbuf_len(xb), key = 0;
	if (cmd == 'y')
		vi_yank(r1, o1, r2, o2, lnmode);
	else if (cmd == 'd')
		vi_delete(r1, o1, r2, o2, lnmode);
	else if (cmd == 'c')
		key = vi_change(r1, o1, r2, o2, lnmode);
	else if (cmd == '~' || cmd == 'u' || cmd == 'U')
		vi_case(r1, o1, r2, o2, lnmode, cmd);
	else if (cmd == '>' || cmd == '<')
		vi_shift(r1, r2, cmd == '>' ? +1 : -1, 1);
	vi_mod |= r1 != r2 || mv != lbuf_len(xb) ? 1 : 2;
	return key;
}

static int vc_insert(int cmd)
{
	char *post, *ln = lbuf_get(xb, xrow);
	int row, cmdo, forced, ipre, ips = 0, l1, off, key, postn = 1;
	sbuf_smake(sb, xcols)
	vi_insert_screen_enter();
	ipre = vi_backspace_reenter ? 0 : -1;
	vi_backspace_reenter = 0;
	if (cmd == 'I')
		xoff = lbuf_indents(xb, xrow);
	else if (cmd == 'A')
		xoff = lbuf_eol(xb, xrow, 1);
	else if (cmd == 'o') {
		xrow++;
		if (xrow - xtop == xrows)
			vi_drawagain(++xtop);
	}
	xoff = ren_noeol(ln, xoff);
	forced = vi_forced_line(ln);
	if (forced && xoff < 1)
		xoff = MIN(1, lbuf_eol(xb, xrow, 1));
	row = xrow;
	if (cmd == 'a' || cmd == 'A')
		xoff++;
	if (ln && ln[0] == '\n')
		xoff = 0;
	cmdo = cmd == 'o' || cmd == 'O';
	if (cmdo || !ln) {
		if (cmdo && !lbuf_len(xb))
			lbuf_edit(xb, "\n", 0, 0, 0, 0);
		vi_indents(ln, &l1);
		off = l1;
		post = "\n";
	} else {
		off = xoff;
		l1 = rstate->chrs[off] - ln;
		postn = rstate->n - off;
		post = ln + l1;
		if (forced)
			ips = MIN(l1, HWBRK_LEN);
	}
	term_pos(row - xtop, 0);
	term_room(cmdo);
	if (l1)
		sbuf_mem(sb, ln, l1)
	nextvi_display_note_insert();
	key = led_input_at(sb, post, postn, row, cmdo << 2, &postn, ips, xrow,
		ipre);
	if (postn != l1 || cmdo || !ln || key == LED_REFLOW) {
		int lines = vi_linecount(sb->s);
		lbuf_edit(xb, sb->s, row, row + !cmdo, off, xoff);
		vi_hardwrap_range(row, lines);
	}
	lbuf_mark(xb, '^', xrow, xoff);
	free(sb->s);
	vi_insert_screen_leave();
	return key;
}

static int vc_put(int cmd)
{
	int cnt = MAX(1, vi_arg);
	int i, off;
	sbuf *buf = xregs[vi_ybuf];
	char *ln;
	if (!buf)
		vi_drawmsg_mpt("yank buffer empty")
	if (!buf || !buf->s_n)
		return 0;
	sbuf_smake(sb, 1024)
	if (buf->s[buf->s_n-1] == '\n' || strchr(buf->s, '\n')) {
		for (i = 0; i < cnt; i++)
			sbufn_mem(sb, buf->s, buf->s_n)
		if (!lbuf_len(xb))
			lbuf_edit(xb, "\n", 0, 0, 0, 0);
		if (cmd == 'p')
			xrow++;
		lbuf_edit(xb, sb->s, xrow, xrow, 0, 0);
		xoff = lbuf_indents(xb, xrow);
		free(sb->s);
		return 1;
	}
	if (!(ln = lbuf_get(xb, xrow)))
		ln = "\n";
	off = ren_noeol(ln, xoff) + (ln[0] != '\n' && cmd == 'p');
	sbuf_mem(sb, ln, rstate->chrs[off] - ln)
	for (i = 0; i < cnt; i++)
		sbuf_mem(sb, buf->s, buf->s_n)
	sbufn_str(sb, rstate->chrs[off])
	xoff = off + uc_slen(buf->s) * cnt - 1;
	lbuf_edit(xb, sb->s, xrow, xrow + 1, off, xoff);
	free(sb->s);
	return 1;
}

static void vc_join(int spc, int cnt)
{
	int o2 = 0;
	int forced = !spc && vi_forced_line(lbuf_get(xb, xrow));
	if (lbuf_join(xb, xrow, xrow + cnt, xoff, &o2, spc))
		return;
	xoff = o2;
	if (forced && xoff > 0)
		xoff--;
	vi_mod |= 1;
}

static void vi_scrollforward(int cnt)
{
	xtop = MIN(lbuf_len(xb) - 1, xtop + cnt);
	xrow = MAX(xrow, xtop);
}

static void vi_scrollbackward(int cnt)
{
	xtop = MAX(0, xtop - cnt);
	xrow = MIN(xrow, xtop + xrows - 1);
}

static int vc_replace(void)
{
	int cnt = MAX(1, vi_arg);
	char *cs = led_read(&xkmap, term_read(0));
	char *ln = lbuf_get(xb, xrow);
	int off, i;
	if (!ln || !cs)
		return 0;
	off = ren_noeol(ln, xoff);
	if (off + cnt >= rstate->n)
		return 0;
	sbuf_smake(sb, lbuf_s(ln)->len)
	sbuf_mem(sb, ln, rstate->chrs[off] - ln)
	for (i = 0; i < cnt; i++)
		sbuf_str(sb, cs)
	sbufn_str(sb, rstate->chrs[off+cnt])
	xoff = (off + cnt - 1) * (cs[0] != '\n');
	lbuf_edit(xb, sb->s, xrow, xrow + 1, off, xoff);
	xrow += cnt * (cs[0] == '\n');
	free(sb->s);
	return cs[0] == '\n' ? 1 : 2;
}

static char rep_cmd[sizeof(icmd)];	/* the last command */
static int rep_len;

static void vc_repeat(void)
{
	for (int i = 0; i < MAX(1, vi_arg); i++)
		term_push(rep_cmd, rep_len);
}

static void vc_execute(int cmd)
{
	static int exec_buf = -1;
	int c = term_read(0), i, n = MAX(1, vi_arg);
	sbuf **buf;
	if (TK_INT(c))
		return;
	if (c == cmd && exec_buf >= 0)
		c = exec_buf;
	buf = &xregs[c];
	if (!*buf) {
		vi_drawmsg_mpt("exec buffer empty")
		return;
	}
	exec_buf = c;
	if (c == ':') {
		term_pos(xrows, 0);
		for (i = 0; i < n && *buf; i++)
			ex_exec((*buf)->s);
		vi_mod |= 1;
		return;
	}
	for (i = 0; i < n && *buf; i++)
		term_exec((*buf)->s, (*buf)->s_n, cmd)
}

static void vi_smart_insert_return(void)
{
	char c = vi_smart_insert_reenter;

	vi_smart_insert = 0;
	if (xquit)
		return;
	if (ibuf_pos < ibuf_cnt)
		term_push(&c, 1);
	else
		term_back(c);
}

static void vi_argcmd(int arg, char cmd)
{
	char str[32];
	char *cs = nextvi_itoa(arg, str);
	*cs = cmd;
	term_push(str, cs - str + 1);
}

#define topfix() \
if (xrow < 0 || xrow >= lbuf_len(xb)) \
	xrow = lbuf_len(xb) ? lbuf_len(xb) - 1 : 0; \
if (xtop > xrow) \
	xtop = xrow; \
else if (xtop + xrows <= xrow) \
	xtop = xrow - xrows + 1; \

void vi(int init)
{
	char *ln, *cs;
	int mv, n, k, c;
	xgrec++;
	if (init) {
		topfix()
		vi_col = vi_off2col(xb, xrow, xoff);
		vi_drawagain(xtop);
		if (!xmpt)
			vc_status(0);
		term_pos(xrow - xtop, led_pos(lbuf_get(xb, xrow), vi_col));
		term_cursor(1);
		term_commit();
	}
	while (!xquit) {
		if (vi_smart_insert == 2) {
			vi_smart_insert = 1;
			vi_smart_insert_reenter = 'a';
		}
		int nrow = xrow;
		int noff = xoff;
		int ooff = noff;
			int otop = xtop;
			int oleft = xleft;
			int orow = xrow;
		icmd_pos = 0;
		vi_mod = 0;
		vi_ybuf = vi_yankbuf();
		vi_arg = vi_prefix();
		term_dec()
		if (xmpt == 1) {
			xmpt = 0;
			vi_drawrow(otop + xrows - 1);
		}
		if (!vi_ybuf)
			vi_ybuf = vi_yankbuf();
		mv = vi_motion(0, &nrow, &noff);
		if (mv > 0) {
			if (strchr("|jk", mv)) {
				noff = vi_col2off(xb, nrow, vi_col);
			} else {
				noff = noff < 0 ? lbuf_indents(xb, nrow) : noff;
				vi_mod |= 4;
			}
			if ((xrow != nrow || ooff != noff) &&
					strchr("%'`GHML/?{}[]", mv))
				lbuf_mark(xb, '`', xrow, ooff);
			xrow = nrow;
			xoff = noff;
			if (vi_visual)
				vi_mod |= 1;
		} else if (mv == 0) {
			char *cmd;
			term_dec()
			re_motion:
			c = term_read(TK_CTL('l'));
			switch (c) {
			case TK_MENU:
				xquit = !xquit ? 1 : xquit;
				continue;
			case TK_CTL('b'):
				vi_scrollbackward(MAX(1, vi_arg) * (xrows - 1));
				xoff = lbuf_indents(xb, xrow);
				vi_mod |= 4;
				break;
			case TK_CTL('f'):
				vi_scrollforward(MAX(1, vi_arg) * (xrows - 1));
				xoff = lbuf_indents(xb, xrow);
				vi_mod |= 4;
				break;
			case TK_CTL('e'):
				vi_scrolley = vi_arg ? vi_arg : vi_scrolley;
				vi_scrollforward(MAX(1, vi_scrolley));
				xoff = vi_col2off(xb, xrow, vi_col);
				break;
			case TK_CTL('y'):
				vi_scrolley = vi_arg ? vi_arg : vi_scrolley;
				vi_scrollbackward(MAX(1, vi_scrolley));
				xoff = vi_col2off(xb, xrow, vi_col);
				break;
			case TK_CTL('u'):
				if (xrow == 0)
					break;
				if (vi_arg)
					vi_scrollud = vi_arg;
				n = vi_scrollud ? vi_scrollud : xrows / 2;
				xrow = MAX(0, xrow - n);
				if (xtop > 0)
					xtop = MAX(0, xtop - n);
				xoff = lbuf_indents(xb, xrow);
				vi_mod |= 4;
				break;
			case TK_CTL('d'):
				if (xrow == lbuf_len(xb) - 1)
					break;
				if (vi_arg)
					vi_scrollud = vi_arg;
				n = vi_scrollud ? vi_scrollud : xrows / 2;
				xrow = MIN(MAX(0, lbuf_len(xb) - 1), xrow + n);
				if (xtop < lbuf_len(xb) - xrows)
					xtop = MIN(lbuf_len(xb) - xrows, xtop + n);
				xoff = lbuf_indents(xb, xrow);
				vi_mod |= 4;
				break;
			case TK_CTL('i'): {
				if (!(ln = lbuf_get(xb, xrow)))
					break;
				ln += xoff;
				char buf[strlen(ln)+4];
				strcpy(buf, ":e ");
				strcpy(buf+3, ln);
				term_push(buf, strlen(ln)+3);
				break; }
			case TK_CTL('n'):
			{
				int idx, target;
				if (xbufcur <= 0)
					break;
				vi_cndir = vi_arg ? -vi_cndir : vi_cndir;
				if (ex_buf < bufs || ex_buf >= bufs + xbufcur)
					vi_arg = -1;
				else {
					idx = ex_buf - bufs;
					target = idx + vi_cndir;
					if (target >= xbufcur)
						target = 0;
					if (target >= 0) {
						vi_arg = target;
						goto switchbuf;
					}
					vi_arg = -1;
				}
			}
			/* fall through */
			case TK_CTL('_'):	/* this is also ^7 on some systems */
				if (vi_arg > 0)
					goto switchbuf;
				ex_exec("left0:b:mpt0");
				term_chr('\n');
				vi_arg = vi_digit();
				if (vi_arg > -1 && vi_arg < xbufcur) {
					switchbuf:
					bufs_switchwft(vi_arg < xbufcur ? vi_arg : 0)
					vc_status_defer(0);
				}
				vi_mod |= 1;
				break;
			case 'u':
				if (vi_visual) {
					vc_visual_op('u');
					break;
				}
				undo:
				if (vi_arg >= 0 && !lbuf_undo(xb, &xrow, &xoff)) {
					vi_mod |= 1;
					vi_arg--;
					goto undo;
				} else if (!vi_arg)
					vi_drawmsg_mpt("undo failed")
				break;
			case TK_CTL('r'):
				redo:
				if (vi_arg >= 0 && !lbuf_redo(xb, &xrow, &xoff)) {
					vi_mod |= 1;
					vi_arg--;
					goto redo;
				} else if (!vi_arg)
					vi_drawmsg_mpt("redo failed")
				break;
			case 'U':
				if (vi_visual)
					vc_visual_op('U');
				break;
			case TK_CTL('g'):
				vi_tsm = 0;
				status:
				if (vi_arg) {
					int old_status = vi_status;
					vi_status = vi_arg > 1 ? 0 : term_resized;
					if (!old_status && vi_status)
						xrows--;
					else if (old_status && !vi_status)
						xrows++;
				}
				vc_status(vi_tsm);
				break;
			case TK_CTL('^'):
				if (ex_pbuf >= bufs && ex_pbuf < bufs + xbufcur) {
					bufs_switchwft(ex_pbuf - bufs)
					vc_status_defer(0);
					vi_mod |= 1;
				}
				break;
			case TK_CTL('k'):;
					static struct lbuf *writexb;
					if ((cs = ex_exec("w")) && writexb && xb == writexb)
						cs = ex_exec("mpt0:w!");
					writexb = cs ? xb : NULL;
					vi_mod |= 1;
					break;
				case 'v':
					if (vi_visual) {
						vi_visual = vi_visual == 'v' ? 0 : 'v';
						vi_mod |= 1;
						break;
					}
					vi_mod |= 2;
					k = term_read(0);
					switch (k) {
				case '.':
					while (vi_arg) {
						term_push("j", 1);
						term_push(rep_cmd, rep_len);
						if (strchr("iIoOaAsScC", rep_cmd[0])) {
							term_push("0", 1);
							if (noff)
								vi_argcmd(noff, 'l');
						}
						vi_arg--;
					}
					break;
				case 'w':
					vi_nlmode = !vi_nlmode;
					break;
				case 'o':
					ex_command("%s/\x0d//g:%s/[ \t]+$//g")
					vi_mod |= 1;
					break;
				case 'I':;
				case 'i':;
					char restr[100] = "%s/^\t/";
					vi_arg = MIN(vi_arg ? vi_arg : xts, 80);
					if (k == 'I') {
						cmd = restr+6;
						while (vi_arg--)
							*cmd++ = ' ';
						strcpy(cmd, "/g");
					} else {
						strcpy(restr, "%s/^ {");
						strcpy(nextvi_itoa(vi_arg, restr+6), "}/\t/g");
					}
					ln = vi_enprompt(":", restr, &k, &n);
					goto do_excmd;
				case 'b':
				case 'v':
					term_push(k == 'v' ? ":\x01" : ":\x02", 2); /* ^a : ^b */
					break;
				case ';':
					ln = vi_enprompt(":", "!", &k, &n);
					goto do_excmd;
				case '/': {
					cs = vi_curword(xb, xrow, xoff, vi_arg, 1);
					char buf[cs ? strlen(cs)+30 : 30];
					strcpy(buf, "re ");
					if (cs)
						strcat(buf, cs);
					free(cs);
					ln = vi_enprompt(":", buf, &k, &n);
					goto do_excmd; }
				case 't': {
					vi_drawmsg("arg2:(0|#)");
					cs = vi_curword(xb, xrow, xoff, vi_prefix(), 1);
					char buf[cs ? strlen(cs)+30 : 30];
					strcpy(buf, ".,.+");
					char *buf1 = nextvi_itoa(vi_arg, buf+4);
					strcat(buf1, "s/");
					if (cs) {
						strcat(buf1, cs);
						strcat(buf1, "/");
						free(cs);
					}
					ln = vi_enprompt(":", buf, &k, &n);
					goto do_excmd; }
					case 'r': {
						cs = vi_curword(xb, xrow, xoff, vi_arg, 1);
						char buf[cs ? strlen(cs)+30 : 30];
						strcpy(buf, "%s/");
						if (cs) {
						strcat(buf, cs);
						strcat(buf, "/");
						free(cs);
					}
						ln = vi_enprompt(":", buf, &k, &n);
						goto do_excmd; }
					default:
						term_dec()
					}
					break;
				case 'V':
					if (vi_visual == 'V') {
						vi_visual = 0;
						vi_mod |= 1;
						break;
					}
					if (!vi_visual) {
						vi_vrow = xrow;
						vi_voff = xoff;
					}
					vi_visual = 'V';
					vi_mod |= 1;
					break;
			case TK_CTL('v'):
				vi_arg = (vi_wsel % 5) + !!*vi_word;
			case TK_CTL('c'):
				if (vi_arg && vi_arg <= 5) {
					vi_wsel = vi_arg;
					vi_word = _vi_word + vi_arg;
				} else
					vi_word = _vi_word + (!*vi_word * vi_wsel);
				vi_rshift = 0;
				vi_mod |= 1;
				break;
			case ':':
				ln = vi_enprompt(":", NULL, &k, &n);
				do_excmd:
				if (k && ln[n])
					ex_command(ln + n)
				k = xredraw;
				xredraw = 0;
				if (k)
					xmpt = 0;
				vi_mod |= 1;
				if (!k && !xmpt)
					vi_drawmsg(ln);
				free(ln);
				if (xquit) {
					xmpt = k ? 0 : (xmpt ? xmpt : (xgrec > 1));
					continue;
				} else if (!k && !xmpt)
					xmpt = 1;
				break;
			case 'c':
			case 'd':
				if (vi_visual) {
					k = vc_visual_op(c);
					if (c == 'c')
						goto ins;
					break;
				}
				k = term_read(0);
				if (k == 'i') {
					k = term_read(0);
					char pairs[2];
					switch(k) {
					case ')': case '(': pairs[0]='('; pairs[1]=')'; break;
					case ']': case '[': pairs[0]='['; pairs[1]=']'; break;
					case '}': case '{': pairs[0]='{'; pairs[1]='}'; break;
					case '>': case '<': pairs[0]='<'; pairs[1]='>'; break;
					default: pairs[0] = k; pairs[1] = k; break;
					}
					if (TK_INT(pairs[0]))
						break;
					int r1 = xrow, o1 = xoff, r2, o2;
					int dir = (k == pairs[1] && pairs[0] != pairs[1]) ? -1 : 1;
					int pair_found = 0;
					ren_position(lbuf_get(xb, r1));
					while (*rstate->chrs[o1] != pairs[0])
						if (lbuf_next(xb, dir, &r1, &o1))
							goto out;
					r2 = r1;
					o2 = o1;
					if (pairs[0] == pairs[1]) {
						while (!lbuf_next(xb, 1, &r2, &o2))
							if (*rstate->chrs[o2] == pairs[1]) {
								pair_found = 1;
								break;
							}
					} else
						pair_found = !lbuf_pair(xb, pairs, 2, &r2, &o2);
					if (pair_found && !lbuf_next(xb, 1, &r1, &o1)) {
						vi_delete(r1, o1, r2, o2, 0);
						if (c == 'c')
							term_back('i');
						vi_mod |= 1;
					}
					out:
					break;
				}
				term_dec()
			case 'y':
			case '>':
			case '<':
			case TK_CTL('w'):
				if (vi_visual && c != TK_CTL('w')) {
					vc_visual_op(c);
					break;
				}
				k = vc_motion(c);
				if (c == 'c')
					goto ins;
				break;
			case 'I':
			case 'i':
			case 'a':
			case 'A':
			case 'o':
			case 'O':
				k = vc_insert(c);
				ins:
				vi_mod |= !xpac && xrow == orow ? 8 : 1;
				if (k == LED_SMARTKEY) {
					if (c != 'A' && c != 'C' && xoff > 0)
						xoff--;
					vi_smart_insert = 2;
					xleft = 0;
					vi_mod |= 1;
					break;
				}
				if (k == LED_REFLOW) {
					xleft = 0;
					vi_mod |= 1;
					term_back(xoff != lbuf_eol(xb, xrow, 1) ? 'i' : 'a');
					vi_insert_screen_enter();
					break;
				}
				if (k == 127) {
					xleft = 0;
					vi_mod |= 1;
					if (!vi_hardwrap_backspace_boundary()) {
						if (xrow && !(xoff > 0 && lbuf_eol(xb, xrow, 1))) {
							xrow--;
							vc_join(0, 2);
						} else if (xoff)
							vi_delete(xrow, xoff - 1, xrow, xoff, 0);
					}
					term_back(xoff != lbuf_eol(xb, xrow, 1) ? 'i' : 'a');
					vi_insert_screen_enter();
					vi_backspace_reenter = 1;
					break;
				}
				if (c != 'A' && c != 'C' && xoff > 0)
					xoff--;
				if (TK_INT(k)) {
					xleft = 0;
					vi_mod |= 1;
				}
				if (vi_smart_insert == 1)
					vi_smart_insert = 0;
				break;
			case 'J':
				vc_join(1, vi_arg <= 1 ? 2 : vi_arg);
				break;
			case 'K': {
				preserve(int, xvis, xvis = 1;)
				do {
					ex_exec(";+1c\n:-1");
				} while (vi_arg--);
				restore(xvis)
				vi_mod |= 1;
				break; }
			case TK_CTL('l'):
				term_done();
				term_init();
				vi_mod |= 1;
				break;
			case 'm':
				lbuf_mark(xb, term_read(0), xrow, xoff);
				break;
			case 'p':
			case 'P':
				vi_mod |= vc_put(c);
				break;
			case 'z':
				k = term_read(0);
				switch (k) {
				case '\n':
					xtop = xrow;
					break;
				case '.':
					xtop = MAX(0, xrow - xrows / 2);
					break;
				case '-':
					xtop = MAX(0, xrow - xrows + 1);
					break;
				}
				vi_mod |= 1;
				break;
			case 'g':
				k = term_read(0);
				if (k == 'g')
					term_push("1G", 2);
				else if (k == 'a') {
					vi_tsm = 1;
					goto status;
				} else if (k == 'i') {
					if (!lbuf_jump(xb, '^', &xrow, &xoff)) {
						c = 'i';
						k = vc_insert(c);
						goto ins;
					}
				} else if (k == 'w') {
					preserve(int, xgrp, xgrp = 2;)
					preserve(int, xvis, xvis = 1;)
					n = vi_arg ? vi_arg : 80;
					while (1) {
						xoff = vi_col2off(xb, xrow, n);
						vi_col = vi_off2col(xb, xrow, xoff+1);
						if (vi_col <= n)
							break;
						if (ex_exec("f>[^ \t]*[ \t]+(?\\:.$|(.)):??;c\n"))
							break;
					}
					restore(xgrp)
					restore(xvis)
					vi_mod |= !texec;
				} else if (k == 'q') {
					preserve(int, xled, xled = 0;)
					char cmd[64] = "g/./& ";
					strcpy(nextvi_itoa(vi_arg, cmd+5), "gw");
					ex_command(cmd)
					restore(xled)
					vi_mod |= 1;
				} else if (k == '~' || k == 'u' || k == 'U') {
					vc_motion(k);
					goto rep;
				}
				break;
			case 'x':
				term_push("d ", 2);
				goto motion;
			case 'X':
				term_push("d", 2);
				goto motion;
			case 'D':
				term_push("d$", 2);
				goto motion;
			case 'Y':
				term_push("yy", 2);
				goto motion;
			case '~':
				if (vi_visual) {
					vc_visual_op('~');
					break;
				}
				term_push("g~ ", 3);
				goto motion;
			case 'C':
				term_push("c$", 2);
				goto motion;
			case 's':
				term_push("c ", 2);
				goto motion;
			case 'S':
				term_push("cc", 2);
				motion:
				icmd_pos--;
				goto re_motion;
			case 'r':
				vi_mod |= vc_replace();
				break;
			case 'R':
				ex_exec("left0:reg");
				break;
			case 'Q':
				term_pos(xrow - xtop, 0);
				xleft = vi_arg ? xleft : 0;
				led_modeswap();
				vi_mod |= 1;
				if (xquit)
					continue;
				break;
			case 'Z':
				k = term_read(0);
				if (TK_INT(k))
					continue;
				if (k == 'Z') {
					ex_exec("x");
					continue;
				}
				xquit = texec == '&' ? -1 : 1;
				if (k == 'z')
					term_push("\n", 1);
				else if (xgrec == 1) {
					term_clean();
					xgrec = 0;
				}
				continue;
			case '.':
				vc_repeat();
				break;
			case '@':
			case '&':
				vc_execute(c);
				break;
			case '\\':
				if (!vi_arg)
					ex_exec("b-2");
				else if (xb != tempbufs[1].lb)
					ex_exec("b-2:%d:fd:b-2");
				else
					ex_exec("%d:fd");
				vc_status(0);
				vi_mod |= 1;
				break;
			case TK_ESC:
				if (vi_visual) {
					vi_visual = 0;
					vi_mod |= 1;
					break;
				}
				/*
				On the embedded display, clearing a temporary prompt/status
				message can redraw the row that contains the cursor.  Let ESC
				take the normal tail path so the cursor is restored there.
				*/
				break;
			default:
				if (vi_smart_insert == 1)
					break;
				continue;
			}
			if (vi_smart_insert == 1 && strchr("dDxX", c))
				vi_smart_insert_reenter = 'i';
			if (vi_visual)
				vi_mod |= 1;
			if (strchr("!<>AIJKOPRacdiopry", c)) {
				rep:
				memcpy(rep_cmd, icmd, icmd_pos);
				rep_len = icmd_pos;
			}
			if (vi_smart_insert == 1)
				vi_smart_insert_return();
			}
			topfix()
		ln = lbuf_get(xb, xrow);
		xoff = ren_noeol(ln, xoff);
		if (ln && !rstate->wid[xoff]) {
			for (n = xoff, k = n; k < rstate->n && !rstate->wid[k];) {
				if (!k)
					n = ooff+1;
				k += n > ooff ? 1 : -1;
			}
			if (k < rstate->n)
				xoff = k;
		}
		if (vi_mod)
			vi_col = vi_off2col(xb, xrow, xoff);
		if (vi_col >= xleft + xcols || vi_col < xleft)
			xleft = vi_col < xcols ? 0 : vi_col - xcols / 2;
		n = led_pos(ln, ren_cursor(ln, vi_col));
		if (xmpt > 1) {
			if (!xpln)
				term_chr('\n');
			vi_drawmsg("[any key to continue] ");
			term_read(0);
			xmpt = 0;
			vi_mod |= 1;
		}
		xpln = 0;
		term_record = 1;
		if (vi_mod & 1 || xleft != oleft || (*vi_word && orow != xrow))
			vi_drawagain(xtop);
		else if (*vi_word && (ooff != xoff || vi_mod & 2)
				&& xrow+1 < xtop + xrows)
			vi_drawrow(xrow+1);
		else if (xtop != otop)
			vi_drawupdate(otop - xtop);
		if (vi_mod & 2 && !(vi_mod & 1))
			vi_drawrow(xrow);
		if (vi_defer_status >= 0) {
			k = vi_defer_status;
			vi_defer_status = -1;
			vc_status(k);
			if (vi_status && xmpt > 0)
				xmpt = 0;
		}
		if (vi_status && xmpt < 1) {
			xrows -= term_resized != vi_status;
			vi_status = term_resized;
			vc_status(vi_tsm);
			if (xmpt > 0)
				xmpt = 0;
		}
#ifdef NEXTVI_NOTERM
		if (vi_insert_status_dirty) {
			vi_insert_status_dirty = 0;
			vc_status(vi_tsm);
		}
#endif
		if (!vi_backspace_reenter)
			term_cursor(1);
		term_pos(xrow - xtop, n);
		term_commit();
		xb->useq += xseq;
	}
	if (--xgrec == 0) {
		term_pos(xrows - !vi_status, 0);
		if (xmpt > 0 && !xpln)
			term_chr('\n');
		else
			term_kill();
	}
}

#ifndef NEXTVI_NOTERM
static void sighandler(int signo)
{
	term_winch++;
}

static void setup_signals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;
	sigaction(SIGWINCH, &sa, NULL);
}
#endif

void nextvi_main(int argc, char *argv[])
{
#ifndef NEXTVI_NOTERM
	setup_signals();
#endif
	temp_open(0, "/hist/");
	temp_open(1, "/fm/");
	temp_open(2, "/sc/");
	ibuf = emalloc(ibuf_sz);
	term_init();
	ex_init(argv + 1, argc - 1);
#ifdef NEXTVI_EMBEDDED
	for (;;) {
		vi(1);
		if (!xquit)
			break;
		xquit = 0;
		if (!nextvi_menu_run())
			break;
	}
#else
	vi(1);
#endif
	term_done();
}

#ifndef NEXTVI_EMBEDDED
int main(int argc, char *argv[])
{
	nextvi_main(argc, argv);
	return abs(xquit) - 1;
}
#endif
