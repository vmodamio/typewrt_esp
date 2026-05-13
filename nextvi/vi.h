/* vi.h: shared definitions across files */

/* helper macros */
#define LEN(a)		(int)(sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) < (b) ? (b) : (a))
#define HWBRK		"\342\200\213"
#define HWBRK_LEN	3
/* for debug; printf() but to file */
#define p(s, ...)\
	{FILE *f = fopen("file", "a");\
	fprintf(f, s, ##__VA_ARGS__);\
	fclose(f);}\

/* ease up ridiculous global stuffing */
#define preserve(type, name, value) \
type tmp##name = name; \
value \

#define restore(name) \
name = tmp##name; \

/* utility funcs */
void *emalloc(size_t size);
void *erealloc(void *p, size_t size);
int dstrlen(const char *s, char delim);
char *nextvi_itoa(int n, char s[]);
static int nextvi_itoalen(int n) { char s[32]; return nextvi_itoa(n, s) - s; }
static void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }

/* sbuf: variable-sized buffer/string */
#define NEXTSZ(o, r)	o + r + ((o + r) >> 1)
typedef struct sbuf {
	char *s;	/* allocated buffer */
	int s_n;	/* length of the string stored in s[] */
	int s_sz;	/* size of memory allocated for s[] */
} sbuf;

#define _sbuf_make(sb, newsz, alloc) \
{ \
	alloc \
	sb->s_sz = newsz; \
	sb->s = emalloc(newsz); \
	sb->s_n = 0; \
} \

#define sbuf_chr(sb, c) \
{ \
	if (sb->s_n + 1 >= sb->s_sz) { \
		sb->s_sz = NEXTSZ(sb->s_n, 1) + 1; \
		sb->s = erealloc(sb->s, sb->s_sz); \
	} \
	sb->s[sb->s_n++] = c; \
} \

#define sbuf_(sb, x, len, func) \
if (sb->s_n + len >= sb->s_sz) { \
	if (sb->s_n < sb->s_sz >> 2) { \
		char *__s_ = sb->s; \
		sb->s_sz = NEXTSZ(sb->s_n, len) + 1; \
		sb->s = emalloc(sb->s_sz); \
		memcpy(sb->s, __s_, sb->s_n); \
		free(__s_); \
	} else { \
		sb->s_sz = NEXTSZ(sb->s_n, len) + 1; \
		sb->s = erealloc(sb->s, sb->s_sz); \
	} \
} \
mem##func(sb->s + sb->s_n, x, len); \

#define sbuf_smake(sb, newsz) sbuf _##sb, *sb = &_##sb; _sbuf_make(sb, newsz,)
#define sbuf_make(sb, newsz) { _sbuf_make(sb, newsz, sb = emalloc(sizeof(*sb));) }
#define sbuf_free(sb) { free(sb->s); free(sb); }
#define sbuf_set(sb, ch, len) { int __l_ = len; sbuf_(sb, ch, __l_, set) sb->s_n += __l_; }
#define sbuf_mem(sb, s, len) { int __l_ = len; sbuf_(sb, s, __l_, cpy) sb->s_n += __l_; }
#define sbuf_str(sb, s) { sbuf_mem(sb, s, strlen(s)) }
#define sbuf_cut(sb, len) { sb->s_n = len; }
/* sbuf functions that null-terminate strings */
#define sbuf_null(sb) { sb->s[sb->s_n] = '\0'; }
#define sbufn_null(sb) { sbuf_(sb, '\0', 4, set) }
#define sbufn_set(sb, ch, len) { sbuf_set(sb, ch, len) sbuf_null(sb) }
#define sbufn_mem(sb, s, len) { sbuf_mem(sb, s, len) sbuf_null(sb) }
#define sbufn_str(sb, s) { sbuf_str(sb, s) sbuf_null(sb) }
#define sbufn_cut(sb, len) { sbuf_cut(sb, len) sbuf_null(sb) }
#define sbufn_chr(sb, c) { sbuf_chr(sb, c) sbuf_null(sb) }
#define sbufn_ret(sb, str) { sbuf_null(sb) return str; }

/* regex.c: regular expressions */
#define REG_ICASE	0x01
#define REG_NEWLINE	0x02	/* unlike posix, controls termination by '\n' */
#define REG_NOTBOL	0x04
#define REG_NOTEOL	0x08
#define REG_NOCAP	0x10	/* computes only the default match group */
typedef struct rcode rcode;
struct rcode {
	rcode **la;		/* lookahead expressions */
	int laidx;		/* lookahead index */
	int unilen;		/* number of integers in insts */
	int len;		/* number of atoms/instructions */
	int sub;		/* interim val = save count; final val = nsubs size */
	int presub;		/* interim val = save count; final val = 1 rsub size */
	int splits;		/* number of split insts */
	int sparsesz;		/* sdense size */
	int flg;		/* stored flags */
	int insts[];		/* re code */
};
/* regular expression set */
typedef struct {
	rcode *regex;		/* the combined regular expression */
	int *grp;		/* subgroup index */
	int *grpnsubc;		/* sub count in each subgroup */
	int nsubc;		/* total sub count */
	int n;			/* number of regular expressions in this set */
} rset;
rset *rset_make(int n, char **pat, int flg);
static rset *rset_smake(char *pat, int flg)
	{ char *ss[1] = {pat}; return rset_make(1, ss, flg); }
int rset_find(rset *re, char *s, int *grps, int flg);
int rset_match(rset *rs, char *s, int flg);
void rset_free(rset *re);
char *re_read(char **src, int delim);

/* lbuf.c: line buffer */
#define NMARKS_BASE		28	/* ('z' - 'a' + 2) */
#define NMARKS			30	/* adj: '`* nonadj: [] */
struct lopt {
	char **ins;		/* inserted lines */
	char **del;		/* deleted lines */
	int *mark;		/* saved marks */
	int pos, pos_off;	/* modification location */
	int n_ins, n_del;	/* modification range */
	int seq;		/* operation number */
	int ref;		/* ins/del ref exists on lbuf */
};
struct linfo {
	int len;
	int grec;
};
struct lbuf {
	char **ln;			/* buffer lines */
	struct lopt *hist;		/* buffer history */
	int mark[NMARKS * 2];		/* mark rows & offs */
	int tmp_mark[4];		/* aux mark state */
	int ln_n;			/* number of lines in ln[] */
	int ln_sz;			/* size of ln[] */
	int useq;			/* current operation sequence */
	int modified;			/* modification state */
	int saved;			/* save state */
	int hist_sz;			/* size of hist[] */
	int hist_n;			/* current history head in hist[] */
	int hist_u;			/* current undo head in hist[] */
};
#define lbuf_len(lb) lb->ln_n
#define lbuf_s(ln) ((struct linfo*)(ln - sizeof(struct linfo)))
#define lbuf_i(lb, pos) ((struct linfo*)(lb->ln[pos] - sizeof(struct linfo)))
struct lbuf *lbuf_make(void);
void lbuf_free(struct lbuf *lb);
int lbuf_rd(struct lbuf *lb, int fd, int beg, int end);
int lbuf_wr(struct lbuf *lb, int fd, int beg, int end);
void lbuf_edit(struct lbuf *lb, char *s, int beg, int end, int o1, int o2);
void lbuf_region(struct lbuf *lb, sbuf *sb, int r1, int o1, int r2, int o2);
int lbuf_pos2off(struct lbuf *lb, int r1, int o1, int r2, int o2, int row, int off);
int lbuf_off2pos(struct lbuf *lb, int r1, int o1, int r2, int o2, int boff, int *row, int *off);
char *lbuf_joinsb(struct lbuf *lb, int r1, int r2, sbuf *i, int *o1, int *o2);
int lbuf_join(struct lbuf *lb, int beg, int end, int o1, int *o2, int flg);
char *lbuf_get(struct lbuf *lb, int pos);
void lbuf_smark(struct lbuf *lb, struct lopt *lo, int beg, int o1);
void lbuf_emark(struct lbuf *lb, struct lopt *lo, int end, int o2);
struct lopt *lbuf_opt(struct lbuf *lb, int beg, int o1, int n_del);
void lbuf_mark(struct lbuf *lb, int mark, int pos, int off);
int lbuf_jump(struct lbuf *lb, int mark, int *pos, int *off);
int lbuf_undo(struct lbuf *lb, int *row, int *off);
int lbuf_redo(struct lbuf *lb, int *row, int *off);
void lbuf_saved(struct lbuf *lb, int clear);
int lbuf_indents(struct lbuf *lb, int r);
int lbuf_eol(struct lbuf *lb, int r, int state);
int lbuf_next(struct lbuf *lb, int dir, int *r, int *o);
int lbuf_findchar(struct lbuf *lb, char *cs, int cmd, int n, int *r, int *o);
int lbuf_search(struct lbuf *lb, rset *re, int dir, int beg, int end, int pskip,
		int nskip, int *r, int *o);
#define lbuf_dedup(lb, str, n) \
{ for (int i = 0; i < lbuf_len(lb);) { \
	char *s = lbuf_get(lb, i); \
	if (n == lbuf_s(s)->len && !memcmp(str, s, n)) \
		lbuf_edit(lb, NULL, i, i + 1, 0, 0); \
	else \
		i++; \
}} \

/* regions */
int lbuf_sectionbeg(struct lbuf *lb, int dir, int *row, int *off, int ch);
int lbuf_wordbeg(struct lbuf *lb, int big, int dir, int *row, int *off);
int lbuf_wordend(struct lbuf *lb, int big, int dir, int *row, int *off);
int lbuf_pair(struct lbuf *lb, char *pairs, int plen, int *row, int *off);

/* ren.c: rendering lines */
typedef struct {
	char **chrs;
	char *s;	/* to prevent redundant computations, ensure pointer uniqueness */
	int *wid;
	int *col;
	int *pos;
	int n;
	int cmax;
	int holelen;
	char nullhole[4];
} ren_state;
extern ren_state rstates[3];
extern ren_state *rstate;
#define RS(n, func) { rstate = rstates+n; rstate->s = NULL; func; rstate -= n; }
ren_state *ren_position(char *s);
int ren_next(char *s, int p, int dir);
int ren_eol(char *s, int dir);
int ren_pos(char *s, int off);
int ren_cursor(char *s, int pos);
int ren_noeol(char *s, int p);
int ren_off(char *s, int p);
char *ren_translate(char *s, char *ln);
/* uc.c: utf-8 helper functions */
extern unsigned char utf8_length[256];
extern int zwlen, def_zwlen;
extern int bclen, def_bclen;
/* the length of a given utf-8 character */
#define uc_len(s) utf8_length[(unsigned char)s[0]]
/* the unicode codepoint of a given utf-8 character */
#define uc_code(dst, s, l) \
dst = (unsigned char)s[0]; \
l = utf8_length[dst]; \
if (l == 1); \
else if (l == 2) \
	dst = ((dst & 0x1f) << 6) | (s[1] & 0x3f); \
else if (l == 3) \
	dst = ((dst & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f); \
else if (l == 4) \
	dst = ((dst & 0x07) << 18) | ((s[1] & 0x3f) << 12) | \
		((s[2] & 0x3f) << 6) | (s[3] & 0x3f); \
else \
	dst = 0; \

int uc_wid(int c);
int uc_slen(char *s);
char *uc_chrn(char *s, int off, int *n);
static char *uc_chr(char *s, int off) { int n; return uc_chrn(s, off, &n); }
int uc_off(char *s, int off);
char *uc_subl(char *s, int beg, int end, int *rlen);
static char *uc_sub(char *s, int beg, int end)
	{ int l; return uc_subl(s, beg, end, &l); }
char *uc_dup(const char *s);
#define uc_isspace(s) ((unsigned char)*s < 0x7f && isspace((unsigned char)*s))
#define uc_isprint(s) ((unsigned char)*s > 0x7f || isprint((unsigned char)*s))
#define uc_isdigit(s) ((unsigned char)*s < 0x7f && isdigit((unsigned char)*s))
#define uc_isalpha(s) ((unsigned char)*s > 0x7f || isalpha((unsigned char)*s))
int uc_kind(char *c);
int uc_isbell(int c);
int uc_acomb(int c);
char *uc_beg(char *beg, char *s);

/* term.c: managing the terminal or a custom keyboard/display backend */
extern sbuf *term_sbuf;
extern int term_record;
extern int term_winch;
extern int term_resized;
extern int xrows, xcols;
extern unsigned int ibuf_pos, ibuf_cnt, ibuf_sz, icmd_pos;
extern unsigned char *ibuf, icmd[4096];
extern unsigned int texec, tn;
#ifndef NEXTVI_DISPLAY_COLS
#define NEXTVI_DISPLAY_COLS	40
#endif
#ifndef NEXTVI_DISPLAY_ROWS
#define NEXTVI_DISPLAY_ROWS	25
#endif
#ifndef NEXTVI_FONT_WIDTH
#define NEXTVI_FONT_WIDTH	8
#endif
#ifndef NEXTVI_FONT_HEIGHT
#define NEXTVI_FONT_HEIGHT	16
#endif
#define NEXTVI_KEY_PRESS	0x80
#define NEXTVI_KEY_MODIFIER	0x40
#define NEXTVI_KEY_SIDE		0x20
#define NEXTVI_KEY_CODE_MASK	0x3f
#define NEXTVI_MOD_CODE_MASK	0x1f
#define NEXTVI_MOD_SHIFT	0x01
#define NEXTVI_MOD_CTRL		0x02
#define NEXTVI_MOD_ALT		0x04
#define NEXTVI_MOD_WIN		0x08
#define NEXTVI_MOD_CAPS		0x10
#ifdef NEXTVI_NOTERM
#define term_write(s, n)
int nextvi_keyboard_queue_push(unsigned char event);
int nextvi_keyboard_queue_pop(unsigned char *event);
int nextvi_keyboard_read(unsigned char *event);
int nextvi_keyboard_read_timeout(unsigned char *event, int timeout_ms);
void nextvi_display_refresh_line(int row, const char *text, int cols);
void nextvi_display_refresh_line_inverted(int row, const char *text, int cols);
void nextvi_display_draw_hline(int y, int color);
void nextvi_display_refresh_cursor(int row, int col, int on);
void nextvi_display_move_cursor(int old_row, int old_col,
	int new_row, int new_col, int on);
void nextvi_display_note_insert(void);
#else
#define term_write(s, n) if (xled) write(1, s, n);
#endif
void nextvi_main(int argc, char *argv[]);
void term_init(void);
void term_done(void);
void term_clean(void);
#define term_scrl	term_write("\033[?1049l", 8)
#define term_scrh	term_write("\033[?1049h", 8)
void term_chr(int ch);
void term_pos(int r, int c);
void term_cursor(int on);
void term_cursor_suspend(int on);
void term_kill(void);
void term_room(int n);
int term_read(int winch);
int term_read_timeout(int winch, int timeout_ms);
void term_commit(void);
void term_push(char *s, unsigned int n);
void term_back(int c);
#define term_dec() ibuf_pos--; icmd_pos--;
#define term_exec(s, n, type) \
{ \
	preserve(int, ibuf_cnt,) \
	preserve(int, ibuf_pos, ibuf_pos = ibuf_cnt;) \
	term_push(s, n); \
	preserve(int, texec, texec = type;) \
	tn = 0; \
	vi(0); \
	tn = 0; \
	restore(texec) \
	if (xquit > 0) \
		xquit = 0; \
	restore(ibuf_pos) \
	restore(ibuf_cnt) \
} \

/* process management */
#define TK_ESC		TK_CTL('[')
#define TK_CTL(x)	(x & 037)
#define TK_INT(c)	(!c || c == TK_ESC || c == TK_CTL('c'))

/* led.c: line-oriented input and output */
void led_modeswap(void);
typedef struct {
	int t_row;
	int p_reg;
	int lsug;
	int sug_pt;
	char *sug;
	char *_sug;
} ins_state;
#define ins_init(is) \
is.t_row = -2; \
is.p_reg = 0; \
is.lsug = 0; \
is.sug_pt = -1; \
is.sug = NULL; \
is._sug = NULL; \

int led_prompt(sbuf *sb, char *insert, int *kmap, ins_state *is, int ps, int flg);
int led_input(sbuf *sb, char *post, int postn, int row, int flg, int *pren);
void led_render(char *s0, int cbeg, int cend);
#define _led_render(msg, row, col, beg, end, kill) \
{ \
	int record = term_record; \
	term_record = 1; \
	term_cursor_suspend(1); \
	term_pos(row, col); \
	kill \
	led_render(msg, beg, end); \
	if (!record) \
		term_commit(); \
	term_cursor_suspend(0); \
} \

#define led_prender(msg, row, col, beg, end) _led_render(msg, row, col, beg, end,)
#define led_crender(msg, row, col, beg, end) _led_render(msg, row, col, beg, end, term_kill();)
char *led_read(int *kmap, int c);
int led_pos(char *s, int pos);
void led_done(void);

/* ex.c: command mode */
struct buf {
	char *path;			/* file path */
	struct lbuf *lb;
	int plen, row, off, top;
	long mtime;			/* modification time */
};
/* ex options */
extern int xleft;
extern int xvis;
extern int xai;
extern int xic;
extern int xled;
extern int xts;
extern int xgrp;
extern int xpac;
extern int xmpt;
extern int xpr;
extern int xlim;
extern int xseq;
extern int xerr;
/* global variables */
extern int xquit;
extern int xrow, xoff, xtop;
extern int xbufcur;
extern int xgrec;
extern int xkmap;
extern int xkmap_alt;
extern int xkwddir;
extern int xkwdcnt;
extern int xpln;
extern int xsep;
extern int xesc;
extern int xexec_dep;
extern sbuf *xacreg;
extern rset *xkwdrs;
extern sbuf *xregs[256];
extern struct buf *bufs;
extern struct buf tempbufs[3];
extern struct buf *ex_buf;
extern struct buf *ex_pbuf;
#define istempbuf(buf) (buf >= tempbufs && buf < tempbufs + LEN(tempbufs))
#define xb_path ex_buf->path
#define xb ex_buf->lb
#define exbuf_load(buf) \
	xrow = buf->row; \
	xoff = buf->off; \
	xtop = buf->top; \

#define exbuf_save(buf) \
	buf->row = xrow; \
	buf->off = xoff; \
	buf->top = xtop; \

#define bufs_switchwft(idx) \
{ if (&bufs[idx] != ex_buf) bufs_switch(idx); } \

void bufs_switch(int idx);
void temp_open(int i, char *name);
void temp_switch(int i, int swap);
void temp_write(int i, char *str);
void temp_pos(int i, int row, int off, int top);
void ex(void);
void *ex_exec(const char *ln);
#define ex_command(ln) { ex_exec(ln); ex_regput(':', ln, 0); }
void ex_cprint(char *line, int r, int c, int left, int flg);
#define ex_cprint2(line, r, c, left, flg) { RS(2, ex_cprint(line, r, c, left, flg)); }
#define ex_print(line) { RS(2, ex_cprint(line, -1, 0, 0, 1)); }
void ex_init(char **files, int n);
void ex_bufpostfix(struct buf *p, int clear);
int ex_krs(rset **krs, int *dir);
void ex_krsset(char *kwd, int dir);
void ex_regesc(sbuf *sb, char *beg, char *end, int ex);
int ex_edit(const char *path, int len);
void ex_regput(unsigned char c, const char *s, int append);
char *ex_pathresolve(const char *path);
#ifdef NEXTVI_EMBEDDED
int nextvi_menu_run(void);
void nextvi_menu_mark_synced(const char *path);
#endif

/* conf.c: configuration variables */
extern const int conf_mode;
extern const int conf_hwwidth;
/* character placeholders */
struct placeholder {
	int cp[2];	/* the source character codepoint */
	char d[8];	/* the placeholder */
	int wid;	/* the width of the placeholder */
	int l;		/* the length of the codepoint */
};
extern struct placeholder _ph[];
extern struct placeholder *ph;
extern int phlen;
extern const int conf_hlrev;
char **conf_kmap(int id);
int conf_kmapfind(char *name);
char *conf_digraph(int c1, int c2);

/* vi.c: main */
void vi(int init);
extern int vi_hidch;
/* filesystem */
extern rset *fsincl;
void dir_calc(char *path);
