int xleft;			/* the first visible column */
int xvis;			/* startup flags */
int xai = 1;			/* autoindent option */
int xic = 1;			/* ignorecase option */
int xled = 1;			/* use the line editor */
int xts = 3;			/* number of spaces for tab */
int xgrp;			/* regex search group */
int xpac;			/* print autocomplete options */
int xmpt;			/* whether to prompt after printing > 1 lines in vi */
int xpr;			/* ex_cprint register */
int xredraw;			/* force a clean vi redraw after an ex command */
int xlim = -1;			/* rendering cutoff for non cursor lines */
int xseq = 1;			/* undo/redo sequence */
int xerr = 1;			/* error handling -
				bit 1: print errors, bit 2: early return, bit 3: ignore errors */

int xquit;			/* exit if positive, force quit if negative */
int xrow, xoff, xtop;		/* current row, column, and top row */
int xbufcur;			/* number of active buffers */
int xgrec;			/* global vi/ex recursion depth */
int xkmap;			/* the current keymap */
int xkmap_alt = 1;		/* the alternate keymap */
int xkwddir;			/* the last search direction */
int xkwdcnt;			/* number of search kwd changes */
int xpln;			/* tracks newline from ex print */
int xsep = ':';			/* ex command separator */
int xesc = '\\';		/* ex command arg escape character */
int xexec_dep;			/* ex_exec recursion depth */
sbuf *xacreg;			/* autocomplete db filter regex */
rset *xkwdrs;			/* the last searched keyword rset */
sbuf *xregs[256];		/* string registers */
struct buf *bufs;		/* main buffers */
struct buf tempbufs[4];		/* temporary buffers, for internal use */
struct buf *ex_buf;		/* current buffer */
struct buf *ex_pbuf;		/* prev buffer */
static struct buf *ex_tpbuf;	/* temp prev buffer */
static int xbufsmax;		/* number of buffers */
static int xbufsalloc = 10;	/* initial number of buffers */
static unsigned long xbufclock;	/* buffer LRU clock */
static int xgdep;		/* global command recursion depth */
static int xexp = '%';		/* ex command internal state expand  */
static char xuerr[] = "unreported error";
#ifdef NEXTVI_EMBEDDED
#include <stdbool.h>
#include "typewrt_ble.h"
#include "typewrt_power.h"
#define NEXTVI_FS_ROOT "/sdcard"
#define NEXTVI_OPEN_FREE_FLOOR (1024UL * 1024UL)
static char ex_vcwd[4096] = NEXTVI_FS_ROOT;
bool typewrt_rtc_get_datetime(char *out, size_t out_len);
const char *typewrt_rtc_set_datetime(const char *datetime, char *out, size_t out_len);
bool typewrt_battery_get_status(char *out, size_t out_len);
#endif
static char xserr[] = "syntax error";
static char xirerr[] = "invalid range";
static char xrnferr[] = "range not found";
static char *xrerr;
static void *xpret;		/* previous ex command return value */
static sbuf *xanchor;		/* anchored error status buffer */
static int xqprop;		/* number of ex_exec levels :q propagates */

#define HELPBUF 3

static int rstrcmp(const char *s1, const char *s2, int l1, int l2)
{
	if (l1 != l2 || !l1)
		return 1;
	for (int i = l1-1; i >= 0; i--)
		if (s1[i] != s2[i])
			return 1;
	return 0;
}

static int bufs_find(const char *path, int len)
{
	for (int i = 0; i < xbufcur; i++)
		if (!rstrcmp(bufs[i].path, path, bufs[i].plen, len))
			return i;
	return -1;
}

static const char *bufs_prepare_open(const char *path);

static void bufs_touch(struct buf *buf)
{
	if (buf)
		buf->lastused = ++xbufclock;
}

static void bufs_make_blank(int idx)
{
	bufs[idx].path = uc_dup("");
	bufs[idx].lb = lbuf_make();
	bufs[idx].plen = 0;
	bufs[idx].row = 0;
	bufs[idx].off = 0;
	bufs[idx].top = 0;
	bufs_touch(&bufs[idx]);
	bufs[idx].mtime = -1;
}

static void bufs_free(int idx)
{
	free(bufs[idx].path);
	lbuf_free(bufs[idx].lb);
}

static char *ex_pathndup(const char *path, int len)
{
	char *copy = emalloc(len + 1);
	memcpy(copy, path, len);
	copy[len] = '\0';
	return copy;
}

#ifdef NEXTVI_EMBEDDED
static int ex_under_fsroot(const char *path)
{
	int rootlen = strlen(NEXTVI_FS_ROOT);
	return path && !strncmp(path, NEXTVI_FS_ROOT, rootlen) &&
		(path[rootlen] == '\0' || path[rootlen] == '/');
}

static char *ex_pathnormalize(const char *path)
{
	char *input, *out;
	const char *p;
	int rootlen = strlen(NEXTVI_FS_ROOT);
	int outlen;

	if (!path || !*path)
		return uc_dup("");
	if (!strcmp(path, "/"))
		input = uc_dup(NEXTVI_FS_ROOT);
	else if (path[0] == '/') {
		if (ex_under_fsroot(path))
			input = uc_dup(path);
		else {
			int len = strlen(path);
			input = emalloc(rootlen + len + 1);
			strcpy(input, NEXTVI_FS_ROOT);
			strcpy(input + rootlen, path);
		}
	} else {
		int cwdlen = strlen(ex_vcwd);
		int pathlen = strlen(path);
		input = emalloc(cwdlen + pathlen + 2);
		strcpy(input, ex_vcwd);
		if (cwdlen && input[cwdlen - 1] != '/')
			input[cwdlen++] = '/';
		strcpy(input + cwdlen, path);
	}

	out = emalloc(strlen(input) + rootlen + 2);
	strcpy(out, NEXTVI_FS_ROOT);
	outlen = rootlen;
	p = ex_under_fsroot(input) ? input + rootlen : input;
	while (*p) {
		const char *seg;
		int len;
		while (*p == '/')
			p++;
		seg = p;
		while (*p && *p != '/')
			p++;
		len = p - seg;
		if (!len || (len == 1 && seg[0] == '.'))
			continue;
		if (len == 2 && seg[0] == '.' && seg[1] == '.') {
			if (outlen > rootlen) {
				while (outlen > rootlen && out[outlen - 1] != '/')
					outlen--;
				if (outlen > rootlen)
					outlen--;
				out[outlen] = '\0';
			}
			continue;
		}
		out[outlen++] = '/';
		memcpy(out + outlen, seg, len);
		outlen += len;
		out[outlen] = '\0';
	}
	free(input);
	return out;
}

static char *ex_pathstore(const char *path, int len)
{
	char *copy, *normalized;

	if (!path || len <= 0)
		return uc_dup("");
	copy = ex_pathndup(path, len);
	normalized = ex_pathnormalize(copy);
	free(copy);
	return normalized;
}
#else
static char *ex_pathstore(const char *path, int len)
{
	return ex_pathndup(path, len);
}
#endif

char *ex_pathresolve(const char *path)
{
#ifdef NEXTVI_EMBEDDED
	return ex_pathnormalize(path);
#else
	return uc_dup(path ? path : "");
#endif
}

static long mtime(char *path)
{
	struct stat st;
	char *fspath = ex_pathresolve(path);
	int ret = stat(fspath, &st);
	free(fspath);
	if (!ret)
		return st.st_mtime;
	return -1;
}

void bufs_switch(int idx)
{
	if (ex_buf != &bufs[idx]) {
		exbuf_save(ex_buf)
		if (istempbuf(ex_buf))
			ex_pbuf = &bufs[idx] == ex_pbuf ? ex_tpbuf : ex_pbuf;
		else
			ex_pbuf = ex_buf;
		ex_buf = &bufs[idx];
	}
	exbuf_load(ex_buf)
	bufs_touch(ex_buf);
}

static int bufs_open(const char *path, int len)
{
	int i = xbufcur;
	if (i >= xbufsmax)
		return -1;
	xbufcur++;
	bufs[i].path = uc_dup(path);
	bufs[i].lb = lbuf_make();
	bufs[i].plen = len;
	bufs[i].row = 0;
	bufs[i].off = 0;
	bufs[i].top = 0;
	bufs_touch(&bufs[i]);
	bufs[i].mtime = -1;
	return i;
}

void temp_open(int i, char *name)
{
	if (tempbufs[i].lb)
		return;
	tempbufs[i].path = uc_dup(name);
	tempbufs[i].lb = lbuf_make();
	tempbufs[i].row = 0;
	tempbufs[i].off = 0;
	tempbufs[i].top = 0;
	bufs_touch(&tempbufs[i]);
	tempbufs[i].mtime = -1;
}

void temp_pos(int i, int row, int off, int top)
{
	if (row < 0)
		row = lbuf_len(tempbufs[i].lb)-1;
	tempbufs[i].row = row < 0 ? 0 : row;
	tempbufs[i].off = off;
	tempbufs[i].top = top;
}

void temp_switch(int i, int swap)
{
	if (ex_buf == &tempbufs[i]) {
		if (swap) {
			exbuf_save(ex_buf)
			ex_buf = ex_pbuf;
			ex_pbuf = ex_tpbuf;
		}
	} else {
		if (!istempbuf(ex_buf)) {
			ex_tpbuf = ex_pbuf;
			ex_pbuf = ex_buf;
		}
		exbuf_save(ex_buf)
		ex_buf = &tempbufs[i];
	}
	exbuf_load(ex_buf)
	bufs_touch(ex_buf);
}

void temp_write(int i, char *str)
{
	if (!*str)
		return;
	struct lbuf *lb = tempbufs[i].lb;
	if (lbuf_get(lb, tempbufs[i].row))
		tempbufs[i].row++;
	lbuf_edit(lb, str, tempbufs[i].row, tempbufs[i].row, 0, 0);
}

static void ex_clean_redraw(void)
{
	xredraw = 1;
	term_clean();
}

static const char help_quick[] =
"NEXTVI HELP\n"
"\n"
"j/k h/l      move\n"
"w b e        words\n"
"0 ^ $        line start/end\n"
"gg G         first/last line\n"
"/ ? n N      search\n"
"i a o        insert\n"
"x d c        delete/change\n"
"u Ctrl-R     undo/redo\n"
"y p          yank/paste\n"
":w :q :wq    save/quit\n"
":menu        file menu\n"
"\n"
"More help:\n"
":help normal\n"
":help insert\n"
":help ex\n"
":help menu\n"
":help markdown\n"
"\n"
"In help, use Ctrl-^ to return.\n";

static const char help_normal[] =
"HELP NORMAL\n"
"\n"
"COUNT\n"
"[n] before most keys repeats or\n"
"moves by n.\n"
"\n"
"MOTION\n"
"[n]j          down n lines\n"
"[n]k          up n lines\n"
"[n]+ Enter    down, after indent\n"
"[n]-          up, after indent\n"
"[n]h          char left\n"
"[n]l          char right\n"
"[n]f{c}       find c forward\n"
"[n]F{c}       find c backward\n"
"[n]t{c}       till c forward\n"
"[n]T{c}       till c backward\n"
"[n];          repeat char find\n"
"[n],          repeat opposite\n"
"[n]w          next word\n"
"[n]W          next WORD\n"
"[n]b          prev word\n"
"[n]B          prev WORD\n"
"[n]e          word end\n"
"[n]E          WORD end\n"
"vw            line word mode\n"
"[n](          next sentence edge\n"
"[n])          prev sentence edge\n"
"[n]{          next { section\n"
"[n]}          prev { section\n"
"[n][          next newline section\n"
"[n]]          prev newline section\n"
"^             first nonblank\n"
"0             line start\n"
"$             line end\n"
"[n]|          goto column n\n"
"[n]Space      char forward\n"
"[n]Backspace  char backward\n"
"%             matching bracket\n"
"[n]%          percent in file\n"
"'mark         jump to mark line\n"
"`mark         jump to mark pos\n"
"gg            first line\n"
"gi            last insert, insert\n"
"[n]G          line n or last\n"
"H M L         top/middle/bottom\n"
"z.            cursor to middle\n"
"zEnter        cursor to top\n"
"z-            cursor to bottom\n"
"Ctrl-E/Y      scroll down/up\n"
"Ctrl-D/U      half page down/up\n"
"Ctrl-F/B      page down/up\n"
"\n"
"EDIT\n"
"i             insert before cursor\n"
"I             insert after indent\n"
"a             append after cursor\n"
"A             append at line end\n"
"o             open line below\n"
"O             open line above\n"
"[n]s          change n chars\n"
"S             change whole line\n"
"[n]c{move}    change region\n"
"C             change to line end\n"
"[n]d{move}    delete region\n"
"D             delete to line end\n"
"[n]x          delete forward\n"
"[n]X          delete backward\n"
"di{pair}      delete inside pair\n"
"ci{pair}      change inside pair\n"
"[n]r{c}       replace chars\n"
"[n]K          split line\n"
"[n]J          join lines\n"
"[n]y{move}    yank region\n"
"[n]Y          yank lines\n"
"[n]p          paste after/below\n"
"[n]P          paste before/above\n"
"u             undo\n"
"Ctrl-R        redo\n"
"[n].          repeat command\n"
"[n]v.         repeat down lines\n"
"\n"
"OPERATORS\n"
"d c y > < Ctrl-W ! g~ gu gU\n"
"Operators take a motion/region.\n"
"Examples: dw, d3w, 3dw, gUU.\n"
"\n"
"VISUAL\n"
"V             visual line mode\n"
"V in visual   show hidden chars\n"
"Esc           leave visual\n"
"visual y/d/c  yank/delete/change\n"
"visual ~ u U  case operations\n"
"\n"
"MARKS\n"
"m{a-z}        set local mark\n"
"Ctrl-T        set global mark 0\n"
"[1-9]Ctrl-T   set global mark n\n"
"Digits are persistent marks.\n"
"\n"
"BUFFERS\n"
"Tab           open path at cursor\n"
"Ctrl-^        previous buffer\n"
"Ctrl-6        previous buffer\n"
"Ctrl-N        next buffer\n"
"Ctrl-7 n      buffer picker\n"
"Ctrl-_ n      buffer picker\n"
"Ctrl-/ n      buffer picker\n"
"\\             file menu buffer\n"
"[n]\\          refresh file menu\n"
"vb            history buffer b-1\n"
"\n"
"SEARCH\n"
"/             search forward\n"
"?             search backward\n"
"n             repeat search\n"
"N             repeat opposite\n"
"*             word search\n"
"Ctrl-A        word regex search\n"
"Ctrl-]        file search forward\n"
"Ctrl-P        file search back\n"
"\n"
"TOOLS\n"
":             ex prompt\n"
"Q             ex mode\n"
"vv            last ex command\n"
"vr vt v/      ex prompt helpers\n"
"Ctrl-C        line motion numbers\n"
"[1-5]Ctrl-C   select line numbers\n"
"Ctrl-V        cycle line numbers\n"
"Ctrl-G        status\n"
"ga            character info\n"
"Ctrl-K        write buffer\n"
"R             show registers\n"
"\"reg op       use register\n"
"@reg          blocking macro\n"
"&reg          nonblocking macro\n"
"@@ &&         repeat last macro\n"
"@: &:         run ex register\n"
"Z ZZ Zz       exit variants\n"
"Ctrl-L        redraw\n";

static const char help_insert[] =
"HELP INSERT\n"
"\n"
"Text keys insert text.  The same\n"
"line editor is used by insert mode\n"
"and by ex/menu prompts.\n"
"\n"
"EDITING\n"
"Backspace     delete char\n"
"Ctrl-H        delete char\n"
"Ctrl-U        delete to mark/start\n"
"Ctrl-W        delete word\n"
"Ctrl-T        increase indent\n"
"Ctrl-D        decrease indent\n"
"Ctrl-V key    insert literal key\n"
"Ctrl-K key    insert digraph\n"
"Enter         newline in insert\n"
"Enter         submit ex prompt\n"
"Esc           leave insert/cancel\n"
"Ctrl-C        leave insert/cancel\n"
"\n"
"REGISTERS\n"
"Ctrl-]        cycle paste register\n"
"Ctrl-\\ key    select register\n"
"Ctrl-P        paste register\n"
"\n"
"COMPLETION\n"
"Ctrl-X        set/reset edit mark\n"
"Ctrl-G        index buffer\n"
"Ctrl-Y        clear index data\n"
"Ctrl-N        next completion\n"
"Ctrl-R        prev completion\n"
"Ctrl-B        show completions\n"
"Ctrl-B ex     edit history buffer\n"
"Ctrl-A        history lines in b-1\n"
"\n"
"MODE / DISPLAY\n"
"Cmd   parse normal cmd, resume\n"
"Ctrl-O        recursive vi/ex\n"
"Ctrl-L        redraw/clean term\n"
"\n"
"KEYMAPS\n"
"Alt-e         English\n"
"Alt-s         Spanish\n"
"Alt-i         Italian\n"
"Alt-n         Norwegian\n"
"Alt-g         German\n"
"Alt-f         French\n"
"Alt-t         Turkish\n"
"Alt-k         Colemak\n";

static const char help_ex[] =
"HELP EX\n"
"\n"
"FORM\n"
":cmd arg      run command\n"
":cmd:cmd      chain commands\n"
"Backslash escapes : and %.\n"
"Use spaces between cmd and arg.\n"
"\n"
"RANGES\n"
"%             whole buffer\n"
".             current line/pos\n"
"$             last line/end line\n"
",             line range sep\n"
";             char range sep\n"
"#             rebase to previous\n"
"+ - * / %     range arithmetic\n"
">pat>         search forward\n"
"<pat<         search backward\n"
"'mark         mark address\n"
"No command with a range moves.\n"
"\n"
"SEARCH / FILTER\n"
"f>pat         find forward\n"
"f<pat         find backward\n"
"f+pat         find next forward\n"
"f-pat         find next backward\n"
"f pat         fuzzy find\n"
"re pat        set regex keyword\n"
"g/pat/cmd     global command\n"
"g!/pat/cmd    inverse global\n"
"? cond ? a ? b while/if command\n"
"??            test last command\n"
"\n"
"EDIT TEXT\n"
"p [text]      print line/text\n"
"=             print range numbers\n"
"i text        insert before\n"
"a text        append after\n"
"c text        change range\n"
"d             delete range\n"
"j [x]         join range\n"
"s/pat/repl/g  substitute\n"
"u             undo\n"
"rd            redo\n"
"\n"
"FILES\n"
"e [path]      edit file\n"
"e! [path]     force edit/reload\n"
"r [path]      read file\n"
"w [path]      write file\n"
"w! [path]     force write\n"
"wq / wq!      write quit\n"
"x / x!        write if changed quit\n"
"q / q!        quit / force quit\n"
"cd [path]     change/show cwd\n"
"fd [path]     fill file list b-2\n"
"fp [path]     set file-list root\n"
"inc [pat]     file-list filter\n"
"ef [pat]      open fuzzy file\n"
"ef! [pat]     force fuzzy open\n"
"\n"
"BUFFERS\n"
"b [n]         buffers/switch\n"
"b-1           history buffer\n"
"b-2           file menu buffer\n"
"b-3           scratch buffer\n"
"b-4           help buffer\n"
"bp [path]     set buffer path\n"
"bs [*]        mark saved\n"
"bw[!] [n]     wipe buffer\n"
"bx [n]        max buffers\n"
"\n"
"MARKS / REGISTERS\n"
"m marks       set marks\n"
"gmarks        global marks\n"
"ya [reg]      yank range\n"
"ya! [reg]     clear register\n"
"pu [reg]      paste register\n"
"reg           show registers\n"
"nreg text     set register n\n"
"nreg+ text    append register n\n"
"\n"
"TYPEWRT\n"
"menu          open file menu\n"
"ble ...       BLE transfer\n"
"rtc [time]    show/set clock\n"
"bat battery   battery status\n"
"power         power command\n"
"off           power off\n"
"about         version/about\n"
"help [topic]  open help\n"
"\n"
"OPTIONS\n"
"ai            auto indent\n"
"ic            ignore case regex\n"
"grp n         regex group\n"
"ts n          tab width\n"
"left n        horizontal scroll\n"
"lim n         render limit\n"
"mpt n         prompt behavior\n"
"pac           completion display\n"
"pr n          print to register\n"
"err n         error behavior\n"
"led           terminal output\n"
"vis n         startup flags\n"
"cm keymap     keymap\n"
"cm! keymap    alt keymap\n"
"ac regex      completion regex\n"
"sc ...        ex special chars\n"
"uc            UTF-8 decoding\n"
"uz            zero-width chars\n"
"ub            multi-codepoint seqs\n"
"ph ...        placeholders\n";

static const char help_menu[] =
"HELP MENU\n"
"\n"
"The menu is the Typewrt file\n"
"browser.  Quit the editor or run\n"
":menu to enter it.\n"
"\n"
"KEYS\n"
"j             move down\n"
"k             move up\n"
"h             parent directory\n"
"l             open entry\n"
"Enter         open file/dir\n"
"o             open file/dir\n"
"Ctrl-D        page down\n"
"Ctrl-U        page up\n"
"g             first entry\n"
"G             last entry\n"
"/             search listing\n"
":             command prompt\n"
"b             send marked files\n"
"d             delete entry\n"
"r             rename entry\n"
"c             copy entry\n"
"R             refresh listing\n"
"P             power off\n"
"Ctrl-N        return, next buffer\n"
"Ctrl-^        return, prev buffer\n"
"Ctrl-_        buffer picker\n"
"q             return to editor\n"
"Esc           back/cancel\n"
"\n"
"BUFFER PICKER\n"
"j/k           move\n"
"Ctrl-D/U      page down/up\n"
"g/G           first/last\n"
"digit         choose buffer\n"
"Enter l o     switch buffer\n"
"q Esc         file browser\n"
"\n"
"GLOBAL MARK PICKER\n"
":gmarks       open picker\n"
"j/k           move\n"
"digit         choose mark\n"
"Enter l o     open mark\n"
"\n"
"COMMANDS\n"
"cd PATH       change directory\n"
"cd ..         parent directory\n"
"cd -          previous directory\n"
"ls            list\n"
"ls -a         show dotfiles\n"
"ls -s         sort by size\n"
"ls -rt        sort by time\n"
"ls *pat*      filter listing\n"
"mkdir PATH    make directory\n"
"open PATH     open file/dir\n"
"gmarks        mark picker\n"
"help [topic]  help topic\n"
"ble send      send current/queued\n"
"ble recv      receive by BLE\n"
"ble selected  send marked files\n"
"ble PATH      send path\n"
"ble status    BLE status\n"
"ble off       cancel BLE\n"
"rtc [time]    show/set clock\n"
"battery       battery status\n"
"rename        rename selected\n"
"copy          copy selected\n"
"delete        delete selected\n"
"off           power off\n"
"ex command    forwarded to Nextvi\n";

static const char help_markdown[] =
"HELP MARKDOWN\n"
"\n"
"HEADINGS\n"
"# Title\n"
"## Chapter\n"
"### Section\n"
"#### Subsection\n"
"\n"
"Title\n"
"=====\n"
"\n"
"Chapter\n"
"-------\n"
"\n"
"# Title {#id}\n"
"\n"
"PARAGRAPHS\n"
"Consecutive text is one paragraph.\n"
"\n"
"Blank line starts a new paragraph.\n"
"\n"
"One newline inside a paragraph\n"
"is usually treated as a space.\n"
"\n"
"Write one sentence per line if useful.\n"
"\n"
"LINE BREAKS\n"
"End line with two spaces\n"
"for a hard line break.\n"
"\n"
"Backslash at line end\\\n"
"also makes a hard break.\n"
"\n"
"<br> is raw HTML break.\n"
"\n"
"POETRY / VERSE\n"
"| First line\n"
"| Second line\n"
"|   Indented line\n"
"\n"
"Line blocks preserve breaks.\n"
"\n"
"EMPHASIS\n"
"*italic*\n"
"_italic_\n"
"**bold**\n"
"__bold__\n"
"***bold italic***\n"
"\n"
"SCENE BREAKS\n"
"* * *\n"
"---\n"
"___\n"
"\n"
"Use alone on a line.\n"
"\n"
"BLOCK QUOTES\n"
"> Quoted passage.\n"
"> Same quote.\n"
"\n"
"> First paragraph.\n"
">\n"
"> Second paragraph.\n"
"\n"
"LISTS\n"
"- item\n"
"* item\n"
"+ item\n"
"\n"
"1. item\n"
"2. item\n"
"\n"
"1) item\n"
"2) item\n"
"\n"
"FOOTNOTES\n"
"Text with note.[^id]\n"
"\n"
"[^id]: Footnote text.\n"
"\n"
"Inline note.^[Short note.]\n"
"\n"
"CITATIONS\n"
"[@key]\n"
"@key\n"
"[@key, p. 42]\n"
"[@a; @b]\n"
"\n"
"REFERENCES\n"
"# References\n"
"\n"
"Use with bibliography metadata.\n"
"\n"
"LINKS\n"
"[text](https://example.com)\n"
"<https://example.com>\n"
"\n"
"IMAGES\n"
"![caption](image.jpg)\n"
"\n"
"TABLES\n"
"| Name | Note |\n"
"|------|------|\n"
"| Ada  | draft |\n"
"\n"
"CODE / VERBATIM\n"
"`inline code`\n"
"\n"
"    indented code block\n"
"\n"
"RAW SPANS\n"
"[small caps]{.smallcaps}\n"
"\n"
"COMMENTS\n"
"<!-- private note -->\n"
"\n"
"ESCAPING\n"
"\\# not heading\n"
"\\*not italic\\*\n"
"\n"
"EPUB METADATA\n"
"---\n"
"title: Book Title\n"
"subtitle: Optional Subtitle\n"
"author: Name\n"
"date: 2026-05-29\n"
"lang: en-US\n"
"BCP 47 languages:\n"
"en-US, es-ES, nb-NO\n"
"description: Short description.\n"
"rights: Copyright 2026 Name\n"
"identifier: urn:isbn:...\n"
"cover-image: cover.jpg\n"
"css: epub.css\n"
"---\n";

struct help_entry {
	const char *name;
	const char *text;
};

static const struct help_entry help_entries[] = {
	{"", help_quick},
	{"quick", help_quick},
	{"normal", help_normal},
	{"vi", help_normal},
	{"insert", help_insert},
	{"prompt", help_insert},
	{"ex", help_ex},
	{"command", help_ex},
	{"commands", help_ex},
	{"options", help_ex},
	{"menu", help_menu},
	{"markdown", help_markdown},
	{"md", help_markdown},
};

static const char *help_text(const char *topic)
{
	const char *end;
	while (*topic == ' ' || *topic == '\t')
		topic++;
	end = topic;
	while (*end && *end != ' ' && *end != '\t')
		end++;
	while (*end == ' ' || *end == '\t')
		end++;
	if (*end)
		return NULL;
	for (int i = 0; i < LEN(help_entries); i++)
		if ((int)(end - topic) == (int)strlen(help_entries[i].name) &&
				!strncmp(topic, help_entries[i].name, end - topic))
			return help_entries[i].text;
	return NULL;
}

static void *ec_help(char *loc, char *cmd, char *arg)
{
	const char *text = help_text(arg);
	struct lbuf *lb = tempbufs[HELPBUF].lb;
	(void)loc;
	(void)cmd;
	if (!text)
		return "unknown help topic";
	lbuf_edit(lb, (char*)text, 0, lbuf_len(lb), 0, 0);
	lbuf_saved(lb, 1);
	temp_pos(HELPBUF, 0, 0, 0);
	temp_switch(HELPBUF, 0);
	ex_clean_redraw();
	return NULL;
}

/* set the current search keyword rset if the kwd or flags changed */
void ex_krsset(char *kwd, int dir)
{
	sbuf *reg = xregs['/'];
	if (kwd && *kwd && ((!reg || !xkwdrs || strcmp(kwd, reg->s))
			|| ((xkwdrs->regex->flg & REG_ICASE) != xic))) {
		rset_free(xkwdrs);
		xkwdrs = rset_smake(kwd, xic ? REG_ICASE : 0);
		xkwdcnt++;
		ex_regput('/', kwd, 0);
		xkwddir = dir;
	}
	if (dir == -2 || dir == 2)
		xkwddir = dir / 2;
}

static int ex_range(char *ploc, char **num, int n, int *row)
{
	int dir, off, beg, end, adj = 0;
	switch ((unsigned char)**num) {
	case '.':
		++*num;
		break;
	case '%':
		if (ploc != *num)
			break;
	case '$':
		n = row ? lbuf_eol(xb, *row, 2) : lbuf_len(xb) - 1;
		++*num;
		break;
	case '\'':
		if (lbuf_jump(xb, (unsigned char)*++(*num),
				&n, row ? &n : &dir))
			return -1;
		++*num;
		break;
	case '>':
	case '<':
		dir = **num == '>' ? 2 : -2;
		off = row ? n : 0;
		beg = row ? *row : n + (dir > 0);
		end = row ? beg+1 : lbuf_len(xb);
		if (off < 0 || beg < 0 || beg >= lbuf_len(xb))
			return -1;
		char *e = re_read(num, 0);
		ex_krsset(e, dir);
		free(e);
		if (!xkwdrs) {
			xrerr = xserr;
			return -1;
		}
		if (lbuf_search(xb, xkwdrs, xkwddir, row ? beg : 0, end,
				MIN(dir, 0), !row, &beg, &off)) {
			xrerr = xrnferr;
			return -1;
		}
		n = row ? off : beg;
		break;
	default:
		if (isdigit((unsigned char)**num)) {
			adj = !row;
			n = atoi(*num);
			while (isdigit((unsigned char)**num))
				++*num;
		}
	}
	while (**num) {
		dir = atoi(*num+1);
		if (**num == '-')
			n -= dir;
		else if (**num == '+')
			n += dir;
		else if (**num == '*')
			n *= dir;
		else if (**num == '/' && dir)
			n /= dir;
		else if (**num == '%' && dir)
			n %= dir;
		else
			break;
		for (++*num; isdigit((unsigned char)**num);)
			++*num;
	}
	return n - adj;
}

/* parse ex command addresses */
#define ex_vregion(loc, beg, end) ex_region(loc, beg, end, &xoff, &xoff)
static int ex_region(char *loc, int *beg, int *end, int *o1, int *o2)
{
	int vaddr = *loc == '%', haddr = 0, update = 0;
	int row = xrow, ooff = xoff, ret = 1;
	char *ploc = loc, *cmd = NULL;
	xrerr = xirerr;
	if (vaddr)
		*beg = 0;
	while (*loc) {
		if (*loc == '|') {
			cmd = re_read(&loc, 0);
			void *err = ex_exec(cmd);
			free(cmd);
			if (err) {
				xrerr = "subcommand error";
				return 1;
			}
			continue;
		} else if (*loc == ';') {
			update = loc[1] == '#';
			loc += 1 + update;
			if ((ooff = ex_range(ploc, &loc, update ? ooff : xoff, &row)) < 0)
				return 1;
			if (haddr++ % 2)
				*o2 = ooff;
			else
				*o1 = ooff;
		} else {
			if (*loc == ',') {
				update = loc[1] == '#';
				loc += 1 + update;
			}
			row = ex_range(ploc, &loc, update ? row : xrow, NULL);
			if (vaddr++ % 2)
				*end = row + 1;
			else
				*beg = row;
		}
		while (*loc && *loc != '|' && *loc != ';' && *loc != ',')
		        loc++;
	}
	if (!vaddr) {
		*beg = xrow;
		*end = MIN(lbuf_len(xb), *beg + 1);
		ret += cmd && !haddr;
	} else if (vaddr == 1)
		*end = *beg + 1;
	return (*beg < 0 || *beg >= lbuf_len(xb) ||
		*end <= *beg || *end > lbuf_len(xb)) * ret;
}

static int ex_read(sbuf *sb, char *msg, ins_state *is, int ps, int flg)
{
	int n = sb->s_n, key;
	if (xvis & 1) {
		while ((key = term_read(0)) != '\n') {
			sbuf_chr(sb, key)
			if (flg & 2 || xquit)
				break;
		}
		sbuf_null(sb)
		return key;
	}
	sbuf_str(sb, msg)
	if (msg[0] == ':' && !msg[1])
		led_prompt_width(conf_hwwidth);
	key = led_prompt(sb, NULL, &xkmap, is, ps, flg);
	led_prompt_width(0);
	if (key == '\n' && (!*msg || strcmp(sb->s + n, msg)))
		term_chr('\n');
	return key;
}

static int ex_readfile(void)
{
	char *fspath = ex_pathresolve(xb_path);
	int fd = open(fspath, O_RDONLY);
	free(fspath);
	if (fd < 0)
		return -1;
	int ret = lbuf_rd(xb, fd, 0, lbuf_len(xb));
	if (!ret)
		vi_hardwrap_all();
	close(fd);
	return ret;
}

int ex_edit(const char *path, int len)
{
	char *stored;
	const char *err;
	int fd, ret, idx;
	if (path[0] == '.' && path[1] == '/') {
		path += 2;
		len -= 2;
	}
	stored = ex_pathstore(path, len);
	len = strlen(stored);
	if (stored[0] && ((fd = bufs_find(stored, len)) >= 0)) {
		bufs_switch(fd);
		ex_clean_redraw();
		free(stored);
		return 1;
	}
	err = bufs_prepare_open(stored);
	if (err) {
		free(stored);
		return -1;
	}
	idx = bufs_open(stored, len);
	if (idx < 0) {
		free(stored);
		return -1;
	}
	bufs_switch(idx);
	ex_clean_redraw();
	free(stored);
	ret = ex_readfile();
	if (ret <= 0)
		ex_bufpostfix(ex_buf, 0);
	return 0;
}

static void *ec_edit(char *loc, char *cmd, char *arg)
{
	char msg[512];
	char *stored;
	const char *err;
	int fd, len, rd = 0, cd = 0, idx;
	if (arg[0] == '.' && arg[1] == '/')
		cd = 2;
	len = strlen(arg+cd);
	stored = ex_pathstore(arg + cd, len);
	len = strlen(stored);
	if (len && ((fd = bufs_find(stored, len)) >= 0)) {
		bufs_switchwft(fd)
		ex_clean_redraw();
		free(stored);
		return NULL;
	} else if (len || !xbufcur || !strchr(cmd, '!')) {
		err = bufs_prepare_open(stored);
		if (err) {
			free(stored);
			return (void*)err;
		}
		idx = bufs_open(stored, len);
		if (idx < 0) {
			free(stored);
			return "buffer list full";
		}
		bufs_switch(idx);
		ex_clean_redraw();
		cd = 3; /* XXX: quick hack to indicate new lbuf */
	}
	free(stored);
	rd = ex_readfile();
	if (cd == 3 || !rd)
		ex_bufpostfix(ex_buf, arg[0]);
	snprintf(msg, sizeof(msg), "\"%s\" %dL [%c]",
			*xb_path ? xb_path : "unnamed", lbuf_len(xb),
			rd < 0 || rd ? 'f' : 'r');
	if (!(xvis & 4))
		ex_print(msg)
	return (rd < 0 || rd) && *arg ? xuerr : NULL;
}

static void *ec_fuzz(char *loc, char *cmd, char *arg)
{
	rset *rs;
	char *path, *p, buf[128], trunc[128], *sret = NULL;
	int c, pos, subs[2], inst = -1, lnum = -1;
	int beg, end, max = INT_MAX, dwid1, dwid2;
	int flg = REG_NEWLINE | REG_NOCAP;
	int pflg = ((xvis & 2) == 0) * 2;
	ins_state is;
	ins_init(is)
	if (*cmd !='f')
		temp_switch(1, 0);
	if (ex_vregion(loc, &beg, &end)) {
		if (*cmd !='f')
			temp_switch(1, 1);
		return xrerr;
	}
	if (!*loc) {
		beg = 0;
		end = lbuf_len(xb);
		max = xrows ? xrows * 3 : end;
	}
	snprintf(trunc, sizeof(trunc), "truncated to %d lines", max);
	dwid1 = nextvi_itoalen(max - 1);
	sbuf_smake(sb, 128)
	sbuf_smake(fuzz, 16)
	sbuf_smake(cmdbuf, 16)
	sbuf_str(fuzz, arg)
	while (1) {
		sbuf_null(fuzz)
		c = 0;
		rs = rset_smake(fuzz->s, xic ? flg | REG_ICASE : flg);
		if (rs) {
			term_record = !!term_sbuf;
			end = MIN(end, lbuf_len(xb));
			dwid2 = nextvi_itoalen(end);
			dwid1 = max == INT_MAX ? dwid2 : MIN(dwid1, dwid2);
			for (pos = beg; c < max && pos < end; pos++) {
				path = xb->ln[pos];
				if (rset_match(rs, path, 0)) {
					sbuf_mem(sb, &pos, (int)sizeof(pos))
					p = nextvi_itoa(c++, buf);
					int z, wid = p - buf;
					for (z = dwid1 + 1 - wid; z; z--)
						*p++ = ' ';
					wid = nextvi_itoalen(pos+1);
					for (z = dwid2 - wid; z; z--)
						*p++ = ' ';
					p = nextvi_itoa(pos+1, p);
					ex_cprint2(buf, -1, 0, 0, pflg)
					ex_cprint2(path, -1, (p - buf) + 1, 0, !pflg)
				}
			}
			if (c == max && c != end)
				ex_cprint2(trunc, -1, 0, 0, 2)
			if (pflg && c)
				term_chr('\n');
			if (term_record)
				term_commit();
		}
		if ((inst = ex_read(fuzz, "", &is, 0, 2)) == '\n' && c) {
			if (c == 1)
				break;
			if ((inst = ex_read(cmdbuf, "", NULL, 0, 0)) == '\n') {
				inst = atoi(cmdbuf->s);
				break;
			}
		}
		if (TK_INT(inst))
			goto ret;
		if (c && c < 11 && isdigit(inst)) {
			inst -= '0';
			if (inst < c) {
				fuzz->s_n--;
				break;
			}
		}
		rset_free(rs);
		sbuf_cut(sb, 0)
		if (pflg) {
			term_clean();
			term_pos(xrows, 0);
		} else if (c)
			ex_print("")
	}
	if ((inst >= 0 && inst < c) || c == 1)
		lnum = *((int*)sb->s + (c == 1 ? 0 : inst));
	ret:
	if (fuzz->s_n > 0) {
		sbuf_cut(cmdbuf, 0)
		sbuf_str(cmdbuf, loc)
		sbuf_str(cmdbuf, cmd)
		sbuf_chr(cmdbuf, ' ')
		sbufn_mem(cmdbuf, fuzz->s, fuzz->s_n)
		lbuf_dedup(tempbufs[0].lb, cmdbuf->s, cmdbuf->s_n)
		temp_pos(0, -1, 0, 0);
		temp_write(0, cmdbuf->s);
	}
	free(cmdbuf->s);
	free(fuzz->s);
	free(sb->s);
	path = lbuf_get(xb, lnum);
	if (*cmd == 'f' && path) {
		rset_find(rs, path, subs, 0);
		xrow = lnum;
		xoff = uc_off(path, subs[0]);
	} else if (path) {
		path[lbuf_s(path)->len] = '\0';
		sret = ec_edit(loc, cmd, path);
		path[lbuf_s(path)->len] = '\n';
	} else if (*cmd != 'f')
		temp_switch(1, 1);
	rset_free(rs);
	return sret;
}

static void *ec_find(char *loc, char *cmd, char *arg)
{
	int pskip, nskip, dir, off, nbeg, beg, end, o1 = -1, o2 = -1;
	if (ex_region(loc, &beg, &end, &o1, &o2))
		return xrerr;
	dir = cmd[1] == '+' || cmd[1] == '>' ? 2 : -2;
	ex_krsset(arg, dir);
	if (!xkwdrs)
		return xserr;
	if (o1 >= 0 && dir > 0) {
		sbuf sb;
		int offs[xkwdrs->nsubc], flg = 0, soff = 0;
		int r2 = end - 1;
		int skip = cmd[1] == '+' ? 1 : 0;
		void *ret = NULL;
		lbuf_region(xb, &sb, beg, o1, r2, o2);
		if (xrow >= beg && xrow <= r2 && (xrow > beg || xoff >= o1))
			soff = lbuf_pos2off(xb, beg, o1, r2, o2, xrow, xoff + skip);
		if (soff < 0)
			soff = 0;
		if (rset_find(xkwdrs, sb.s + soff, offs, flg) < 0 || offs[xgrp] < 0
				|| lbuf_off2pos(xb, beg, o1, r2, o2,
						soff + offs[xgrp], &xrow, &xoff))
			ret = xuerr;
		free(sb.s);
		return ret;
	}
	off = xoff;
	if (xrow < beg || xrow >= end) {
		off = 0;
		end--;
		nbeg = dir > 0 ? beg : end;
		end += dir < 0;
		pskip = -1;
		nskip = 0;
	} else {
		nbeg = xrow;
		pskip = cmd[1] == '+' ? 1 : MIN(dir, 0);
		nskip = cmd[1] == '-';
	}
	if (lbuf_search(xb, xkwdrs, xkwddir, beg, end,
			pskip, nskip, &nbeg, &off))
		return xuerr;
	xrow = nbeg;
	xoff = off;
	return NULL;
}

static void *ec_buffer(char *loc, char *cmd, char *arg)
{
	if (!arg[0]) {
		char ln[512];
		for (int i = 0; i < xbufcur; i++) {
			char c = ex_buf == bufs+i ? '%' : ' ';
			c = ex_pbuf == bufs+i ? '#' : c;
			snprintf(ln, LEN(ln), "%d %c %s", i,
				c + (char)bufs[i].lb->modified, bufs[i].path);
			ex_print(ln)
		}
		return NULL;
	} else if (atoi(arg) < 0) {
		if (abs(atoi(arg)) <= LEN(tempbufs)) {
			temp_switch(abs(atoi(arg))-1, 1);
			return NULL;
		}
	} else if (atoi(arg) < xbufcur) {
		bufs_switchwft(atoi(arg))
		return NULL;
	}
	return "no such buffer";
}

static struct buf *bufs_after_wipe(struct buf *p, int idx, int fallback,
	int old_count)
{
	if (!p)
		return p;
	for (int i = 0; i < LEN(tempbufs); i++)
		if (p == &tempbufs[i])
			return p;
	if (p < bufs || p >= bufs + old_count)
		return p;
	int pidx = p - bufs;
	if (pidx == idx)
		return &bufs[fallback];
	if (pidx > idx)
		return &bufs[pidx - 1];
	return &bufs[pidx];
}

#ifdef NEXTVI_EMBEDDED
static int bufs_protected(int idx)
{
	struct buf *p = &bufs[idx];
	return p == ex_buf || p == ex_pbuf || p == ex_tpbuf;
}

static int bufs_saved_victim(void)
{
	int victim = -1;
	unsigned long lastused = ULONG_MAX;
	for (int i = 0; i < xbufcur; i++) {
		if (bufs_protected(i) || bufs[i].lb->modified || !bufs[i].path[0])
			continue;
		if (bufs[i].lastused < lastused) {
			lastused = bufs[i].lastused;
			victim = i;
		}
	}
	return victim;
}

static void bufs_evict_saved(int idx)
{
	int old_count = xbufcur;
	bufs_free(idx);
	for (int i = idx; i < xbufcur - 1; i++)
		bufs[i] = bufs[i + 1];
	xbufcur--;
	ex_buf = bufs_after_wipe(ex_buf, idx, 0, old_count);
	ex_pbuf = bufs_after_wipe(ex_pbuf, idx, 0, old_count);
	ex_tpbuf = bufs_after_wipe(ex_tpbuf, idx, 0, old_count);
}

static int bufs_reclaim_one_saved(void)
{
	int idx = bufs_saved_victim();
	if (idx < 0)
		return 0;
	bufs_evict_saved(idx);
	return 1;
}

static void bufs_reclaim_until(size_t free_needed)
{
	while (typewrt_heap_free_bytes() < free_needed)
		if (!bufs_reclaim_one_saved())
			break;
}

static size_t ex_open_file_bytes(const char *path)
{
	struct stat st;
	char *fspath = ex_pathresolve(path);
	int ret = stat(fspath, &st);
	free(fspath);
	if (ret || !S_ISREG(st.st_mode) || st.st_size <= 0)
		return 0;
	return (size_t)st.st_size;
}

static const char *bufs_prepare_open(const char *path)
{
	size_t file_bytes = ex_open_file_bytes(path);
	size_t free_needed = NEXTVI_OPEN_FREE_FLOOR;
	size_t largest_needed = file_bytes ? file_bytes + 1 : 0;
	if (file_bytes > (size_t)-1 - free_needed)
		free_needed = (size_t)-1;
	else
		free_needed += file_bytes;
	bufs_reclaim_until(free_needed);
	while (largest_needed &&
			typewrt_heap_largest_free_block() < largest_needed)
		if (!bufs_reclaim_one_saved())
			break;
	if (xbufcur >= xbufsmax && !bufs_reclaim_one_saved())
		return "no saved buffers to close";
	if (typewrt_heap_free_bytes() < NEXTVI_OPEN_FREE_FLOOR)
		return "not enough memory";
	if (largest_needed && typewrt_heap_largest_free_block() < largest_needed)
		return "not enough contiguous memory";
	return NULL;
}
#else
static const char *bufs_prepare_open(const char *path)
{
	(void)path;
	return NULL;
}
#endif

static void *ec_bufwipe(char *loc, char *cmd, char *arg)
{
	int idx, old_count, fallback = 0, wiping_current;
	int current_temp = 0;
	(void)loc;
	for (int i = 0; i < LEN(tempbufs); i++)
		current_temp |= ex_buf == &tempbufs[i];
	if (arg[0])
		idx = atoi(arg);
	else if (current_temp)
		return "cannot wipe temporary buffer";
	else
		idx = ex_buf - bufs;
	if (idx < 0)
		return "cannot wipe temporary buffer";
	if (idx >= xbufcur)
		return "no such buffer";
	if (bufs[idx].lb->modified && !strchr(cmd, '!'))
		return "buffer modified";
	wiping_current = ex_buf == &bufs[idx];
	if (xbufcur == 1) {
		bufs_free(0);
		bufs_make_blank(0);
		ex_pbuf = &bufs[0];
		ex_tpbuf = &bufs[0];
		if (!current_temp) {
			ex_buf = &bufs[0];
			exbuf_load(ex_buf)
			xredraw = 1;
		}
		return NULL;
	}
	old_count = xbufcur;
	if (wiping_current)
		fallback = idx < xbufcur - 1 ? idx : idx - 1;
	else if (!current_temp && ex_buf >= bufs && ex_buf < bufs + xbufcur)
		fallback = ex_buf - bufs - ((ex_buf - bufs) > idx);
	bufs_free(idx);
	for (int i = idx; i < xbufcur - 1; i++)
		bufs[i] = bufs[i + 1];
	xbufcur--;
	ex_buf = bufs_after_wipe(ex_buf, idx, fallback, old_count);
	ex_pbuf = bufs_after_wipe(ex_pbuf, idx, fallback, old_count);
	ex_tpbuf = bufs_after_wipe(ex_tpbuf, idx, fallback, old_count);
	if (wiping_current) {
		exbuf_load(ex_buf)
		xredraw = 1;
	}
	return NULL;
}

static void *ec_quit(char *loc, char *cmd, char *arg)
{
	if (xexec_dep == 1 && xgrec == 1 && !strchr(cmd, '!') && xquit >= 0)
		for (int i = 0; i < xbufcur; i++)
			if (bufs[i].lb->modified)
				return "buffers modified";
	xquit = !xquit ? 1 : xquit;
	xqprop = *loc ? atoi(loc) : -1;
	if (*arg)
		xquit = abs(atoi(arg)) + 1;
	if (strchr(cmd, '!'))
		xquit = -xquit;
	return NULL;
}

void ex_bufpostfix(struct buf *p, int clear)
{
	p->mtime = mtime(p->path);
	lbuf_saved(p->lb, clear);
}

static void *ec_setpath(char *loc, char *cmd, char *arg)
{
	char *stored = ex_pathstore(arg, strlen(arg));
	free(xb_path);
	xb_path = stored;
	ex_buf->plen = strlen(stored);
	return NULL;
}

static void *ec_read(char *loc, char *cmd, char *arg)
{
	sbuf obuf;
	char msg[512];
	char *path, *fspath = NULL, *ret = NULL;
	int beg = 0, end = 0, o1 = 0, o2 = -1;
	int row = xrow, off = xoff, fd = -1;
	struct lbuf *lb = lbuf_make(), *pxb = xb;
	if (arg[0] == '!') {
		ret = "unsupported command";
		goto err;
	} else {
		path = arg[0] ? arg : xb_path;
		fspath = ex_pathresolve(path);
		if ((fd = open(fspath, O_RDONLY)) < 0) {
			ret = "open failed";
			goto err;
		}
		if (lbuf_rd(lb, fd, 0, 0)) {
			ret = "read failed";
			goto err;
		}
	}
	xb = lb;
	xrow = 0;
	xoff = 0;
	vi_hardwrap_all();
	if (lbuf_len(lb) && ex_region(loc, &beg, &end, &o1, &o2)) {
		ret = xrerr;
		goto err;
	} else if (!*loc) {
		beg = 0;
		end = lbuf_len(lb);
	}
	lbuf_region(lb, &obuf, beg, o1, end-1, o2);
	lbuf_edit(pxb, obuf.s, row, row, 0, 0);
	free(obuf.s);
	snprintf(msg, sizeof(msg), "\"%s\" %dL [r]",
			fspath, lbuf_len(pxb) - lbuf_len(lb));
	ex_print(msg)
	err:
	lbuf_free(lb);
	xrow = row;
	xoff = off;
	xb = pxb;
	if (fd >= 0)
		close(fd);
	free(fspath);
	return ret;
}

static void *ec_write(char *loc, char *cmd, char *arg)
{
	char msg[512], *path = NULL;
	int fd, beg = 0, end = 0, o1 = -1, o2 = -1;
#ifdef NEXTVI_EMBEDDED
	int write_started = 0;
#endif
	void *ret = NULL;
	if (cmd[0] == 'x' && !xb->modified)
		return ec_quit("", cmd, "");
	if (lbuf_len(xb) && ex_region(loc, &beg, &end, &o1, &o2))
		return xrerr;
	if (!*loc) {
		beg = 0;
		end = lbuf_len(xb);
	}
	if (arg[0] == '!')
		return "unsupported command";
	path = ex_pathresolve(arg[0] ? arg : xb_path);
	if (!strchr(cmd, '!')) {
		if (!strcmp(xb_path, path) && mtime(path) > ex_buf->mtime)
			ret = "write failed: file changed";
		else if (arg[0] && strcmp(xb_path, path) && mtime(path) >= 0)
			ret = "write failed: file exists";
		if (ret)
			goto done;
	}
#ifdef NEXTVI_EMBEDDED
	typewrt_sd_write_begin();
	write_started = 1;
#endif
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, conf_mode);
	if (fd < 0) {
		ret = "write failed: cannot create file";
		goto done;
	}
	if (o1 >= 0) {
		sbuf ibuf;
		lbuf_region(xb, &ibuf, beg, o1, end-1, o2);
		o1 = write(fd, ibuf.s, ibuf.s_n);
		free(ibuf.s);
	} else
		o1 = lbuf_wr(xb, fd, beg, end);
	close(fd);
	if (o1 < 0) {
		ret = "write failed";
		goto done;
	}
	snprintf(msg, sizeof(msg), "\"%s\" %dL [w]",
			path, end - beg);
	ex_print(msg)
	if (strcmp(xb_path, path))
		ec_setpath(NULL, NULL, path);
	lbuf_saved(xb, 0);
	ex_buf->mtime = mtime(path);
#ifdef NEXTVI_EMBEDDED
	nextvi_menu_mark_local_edited(path);
#endif
	if (cmd[0] == 'x' || (cmd[0] == 'w' && cmd[1] == 'q'))
		ec_quit("", cmd, "");
	done:
#ifdef NEXTVI_EMBEDDED
	if (write_started)
		typewrt_sd_write_end();
#endif
	free(path);
	return ret;
}

static void *ec_termexec(char *loc, char *cmd, char *arg)
{
	if (*arg && term_sbuf)
		term_exec(arg, strlen(arg), cmd[0])
	return term_sbuf ? NULL : "unsupported command";
}

void ex_cprint(char *line, int r, int c, int left, int flg)
{
	if (xpr) {
		ex_regput(xpr, line, 1);
		if (flg & 1 && isupper(xpr) &&
				xregs[xpr] && xregs[xpr]->s_n &&
				xregs[xpr]->s[xregs[xpr]->s_n-1] != '\n')
			ex_regput(xpr, "\n", 1);
	}
	if (xvis & 1) {
		term_write(line, dstrlen(line, '\n'))
		term_write("\n", 1)
		return;
	}
	if (flg && !(xvis & 2)) {
		int nl = (!xpln && xmpt > 0) || flg == 2;
		if (!nl || xmpt <= 0)
			term_pos(xrows, 0);
		if (nl)
			term_chr('\n');
		xmpt += xmpt >= 0 && flg == 1;
	}
	xpln = 0;
	led_crender(line, r, c, left, left + xcols - c)
	if (flg && xvis & 2)
		term_chr('\n');
}

static void *ec_insert(char *loc, char *cmd, char *arg)
{
	int key, beg = 0, end = 0, o1 = -1, o2 = -1, ps = 0;
	if (lbuf_len(xb) && ex_region(loc, &beg, &end, &o1, &o2))
		return xrerr;
	sbuf _sb, *sb = &_sb;
	if (xvis & 1 && *arg) {
		sb->s = arg;
		sb->s_n = 1;
		key = 127;
	} else {
		_sbuf_make(sb, 128,)
		if (*arg)
			term_push(arg, strlen(arg));
		while (1) {
			if ((key = ex_read(sb, "", NULL, ps, 0)) != '\n')
				break;
			if (xvis & 1 && !strcmp(".", sb->s + ps)) {
				sb->s_n = MAX(0, sb->s_n - 2);
				break;
			}
			sbuf_chr(sb, '\n')
			ps = sb->s_n;
		}
		if (key == TK_CTL('c'))
			goto ret;
		if (key == 127 && sb->s_n && sb->s[sb->s_n-1] == '\n')
			sb->s_n--;
		sbuf_null(sb)
	}
	if (cmd[0] == 'a' && (beg + 1 <= lbuf_len(xb))) {
		beg++;
		end = beg;
	} else if (cmd[0] == 'i')
		end = beg;
	if (o1 >= 0 && cmd[0] == 'c') {
		if (sb->s == arg)
			sb->s_n = strlen(arg);
		if (!sb->s_n && o2 <= o1)
			goto ret;
		char *p = lbuf_joinsb(xb, beg, end-1, sb, &o1, &o2);
		o1 -= sb->s[0] == '\n';
		if (sb->s != arg)
			free(sb->s);
		sb->s = p;
	} else if (key != 127)
		sbufn_chr(sb, '\n')
	else if (!sb->s_n)
		goto ret;
	ps = lbuf_len(xb);
	lbuf_edit(xb, sb->s, beg, end, o1, o2);
	xrow = MIN(lbuf_len(xb) - 1, end + lbuf_len(xb) - ps - 1);
	if (o1 >= 0)
		xoff = o1;
	ret:
	if (sb->s != arg)
		free(sb->s);
	return NULL;
}

static void *ec_print(char *loc, char *cmd, char *arg)
{
	int i, beg, end, o1 = -1, o2 = -1;
	char *o, *ln;
	if (!*cmd && !*loc && *arg)
		return "unknown command";
	if (*cmd && *arg) {
		ex_print(arg)
		return NULL;
	}
	if ((i = ex_region(loc, &beg, &end, &o1, &o2)))
		return i == 2 && !*cmd ? NULL : xrerr;
	if (o1 >= 0)
		xoff = MAX(o1, o2);
	if (!*cmd && *loc) {
		xrow = MAX(beg, end - 1);
		return NULL;
	}
	rstate = rstates+1;
	for (i = beg; i < end; i++) {
		o = NULL;
		rstate->s = o;
		ln = lbuf_get(xb, i);
		if (o1 >= 0 && o2 >= 0 && beg == end-1)
			o = uc_sub(ln, o1, o2);
		else if (o1 >= 0 && i == beg)
			o = uc_sub(ln, o1, -1);
		else if (o2 >= 0 && i == end-1)
			o = uc_sub(ln, 0, o2);
		else {
			ex_cprint(ln, -1, 0, *loc ? 0 : xleft, 1);
			continue;
		}
		ex_cprint(o, -1, 0, 0, 1);
		free(o);
	}
	rstate--;
	xrow = MAX(beg, end - (cmd[0] || loc[0]));
	return NULL;
}

static void *ec_delete(char *loc, char *cmd, char *arg)
{
	int beg, end, o1 = -1, o2 = -1;
	sbuf sb;
	char *p = NULL;
	if (ex_region(loc, &beg, &end, &o1, &o2))
		return xrerr;
	if (o1 >= 0) {
		sb.s = "";
		sb.s_n = 0;
		p = lbuf_joinsb(xb, beg, end-1, &sb, &o1, &o2);
		xoff = o1;
	}
	lbuf_edit(xb, p, beg, end, o1, o2);
	free(p);
	xrow = MIN(beg, lbuf_len(xb) - !!lbuf_len(xb));
	return NULL;
}

void ex_regput(unsigned char c, const char *s, int append)
{
	sbuf *sb = xregs[c];
	if (s) {
		if (!sb) {
			sbuf_make(sb, 64)
			xregs[c] = sb;
		}
		if (!append)
			sbuf_cut(sb, 0)
		sbuf_str(sb, s)
		sbufn_null(sb)
	} else if (sb) {
		sbuf_free(sb)
		xregs[c] = NULL;
	}
}

static void *ec_yank(char *loc, char *cmd, char *arg)
{
	int beg, end, o1 = 0, o2 = -1;
	if (cmd[2] == '!') {
		ex_regput(*arg, NULL, 0);
		return NULL;
	} else if (ex_region(loc, &beg, &end, &o1, &o2))
		return xrerr;
	sbuf sb;
	lbuf_region(xb, &sb, beg, o1, end-1, o2);
	ex_regput(*arg, sb.s, *arg && arg[1]);
	free(sb.s);
	return NULL;
}

static void *ec_put(char *loc, char *cmd, char *arg)
{
	int beg = 0, end = 0, i = 0;
	sbuf *buf;
	if (!*arg || (arg[i] == '!' && arg[i+1] && arg[i+1] != ' '))
		buf = xregs[i];
	else
		buf = xregs[(unsigned char)arg[i++]];
	if (!buf)
		return "uninitialized register";
	for (; arg[i] && arg[i] != '!'; i++){}
	if (arg[i] == '!' && arg[i+1])
		return "unsupported command";
	int n = lbuf_len(xb), o1 = -1, o2 = -1;
	if (n && ex_region(loc, &beg, &end, &o1, &o2))
		return xrerr;
	if (o1 >= 0 && n) {
		char *p = lbuf_joinsb(xb, end-1, end-1, buf, &o1, &o2);
		lbuf_edit(xb, p, end-1, end, o1, o1);
		free(p);
	} else
		lbuf_edit(xb, buf->s, end, end, o1, o1);
	xrow = MIN(lbuf_len(xb) - 1, end + lbuf_len(xb) - n - 1);
	return NULL;
}

static void *ec_num(char *loc, char *cmd, char *arg)
{
	char msg[128];
	int arr[4] = {0, 0, -1, -1};
	int ret = ex_region(loc, &arr[0], &arr[1], &arr[2], &arr[3]);
	if (ret && !((*arg && arg[1]) || (*arg && (*arg ^ '0') >= 4)))
		return xrerr;
	if ((*arg ^ '0') < 4)
		nextvi_itoa(arr[*arg ^ '0'], msg);
	else
		sprintf(msg, "%d %d %d %d", arr[0], arr[1], arr[2], arr[3]);
	ex_print(msg)
	return NULL;
}

static void *ec_undoredo(char *loc, char *cmd, char *arg)
{
	int ref;
	if (cmd[0] == 'u')
		return lbuf_undo(xb, &ref, &ref) ? xuerr : NULL;
	return lbuf_redo(xb, &ref, &ref) ? xuerr : NULL;
}

static void *ec_bufsave(char *loc, char *cmd, char *arg)
{
	lbuf_saved(xb, *arg);
	return NULL;
}

static void *ec_mark(char *loc, char *cmd, char *arg)
{
	int beg, end, o1 = xoff, o2 = xoff;
	if (ex_region(loc, &beg, &end, &o1, &o2))
		return xrerr;
	for (int i = 0; *arg; i++)
		lbuf_mark(xb, (unsigned char)*arg++,
			i % 2 ? end - 1 : beg, i % 2 ? o2 : o1);
	return NULL;
}

static int ec_sub_confirm(int row)
{
	char msg[64];

	snprintf(msg, sizeof(msg), "replace on line %d? y/n/a/q", row + 1);
	ex_print(msg)
	return term_read(0);
}

static void *ec_substitute(char *loc, char *cmd, char *arg)
{
	int beg, end, grp;
	char *pat, *rep = NULL, *_rep;
	char *s = arg;
	rset *rs = xkwdrs;
	int flags, i, first = -1, last, nmatch = 0, nlines = 0;
	int ask, ask_all = 0, quit = 0;
	struct lopt *lo;
	if (ex_vregion(loc, &beg, &end))
		return xrerr;
	pat = re_read(&s, 0);
	if (pat && *s) {
		s--;
		rep = re_read(&s, 0);
	}
	flags = (xic || strchr(s, 'i')) ? REG_ICASE : 0;
	if (pat && (*pat || !rs))
		rs = rset_smake(*pat ? pat : xregs['/'] ? xregs['/']->s : "",
			flags);
	else if (strchr(s, 'i') && xregs['/'])
		rs = rset_smake(xregs['/']->s, flags);
	if (!rs) {
		free(pat);
		free(rep);
		return xserr;
	}
	free(pat);
	ask = strchr(s, 'c') != NULL;
	int offs[rs->nsubc];
	for (i = beg; i < end && !quit; i++) {
		char *ln = lbuf_get(xb, i);
		sbuf *r = NULL;
		int line_match = 0, line_change = 0;
		while (rset_find(rs, ln, offs, REG_NEWLINE) >= 0) {
			int sub = 1;
			if (offs[xgrp] < 0) {
				ln += offs[1] > 0 ? offs[1] : 1;
				continue;
			}
			line_match++;
			nmatch++;
			if (strchr(s, 'n')) {
				ln += offs[xgrp + 1];
				if (!offs[xgrp + 1] && *ln)
					ln++;
				if (*ln == '\n' || !*ln || !strchr(s, 'g'))
					break;
				continue;
			}
			if (ask && !ask_all) {
				int c = ec_sub_confirm(i);
				if (c == 'a' || c == 'A')
					ask_all = 1;
				else if (c == 'q' || c == 'Q' || TK_INT(c)) {
					quit = 1;
					sub = 0;
				} else if (c != 'y' && c != 'Y' && c != ' ')
					sub = 0;
			}
			if (!r)
				sbuf_make(r, 256)
			sbuf_mem(r, ln, offs[xgrp])
			if (sub && rep) {
				for (_rep = rep; *_rep; _rep++) {
					if (*_rep != '\\' || !_rep[1]) {
						sbuf_chr(r, (unsigned char)*_rep)
						continue;
					}
					_rep++;
					grp = abs((*_rep - '0') * 2);
					if (grp + 1 >= rs->nsubc)
						sbuf_chr(r, (unsigned char)*_rep)
					else if (offs[grp] >= 0)
						sbuf_mem(r, ln + offs[grp], offs[grp + 1] - offs[grp])
				}
			} else if (!sub)
				sbuf_mem(r, ln + offs[xgrp], offs[xgrp + 1] - offs[xgrp])
			line_change |= sub;
			ln += offs[xgrp + 1];
			if (!offs[xgrp + 1])	/* zero-length match */
				sbuf_chr(r, (unsigned char)*ln++)
			if (*ln == '\n' || !*ln || !strchr(s, 'g') || quit)
				break;
		}
		nlines += !!line_match;
		if (r && line_change) {
			if (first < 0) {
				first = i;
				lo = lbuf_opt(xb, xrow, xoff, 0);
				lbuf_smark(xb, lo, i, 0);
				lbuf_emark(xb, lo, 0, 0);
			}
			sbufn_str(r, ln)
			lbuf_edit(xb, r->s, i, i + 1, 0, 0);
			last = i;
			if (strchr(s, 'p'))
				ex_print(lbuf_get(xb, i))
			sbuf_free(r)
		} else if (r)
			sbuf_free(r)
	}
	if (strchr(s, 'n')) {
		char msg[64];
		snprintf(msg, sizeof(msg), "%d matches on %d lines", nmatch, nlines);
		ex_print(msg)
		if (rs != xkwdrs)
			rset_free(rs);
		free(rep);
		return NULL;
	}
	if (first >= 0) {
		lo = lbuf_opt(xb, xrow, xoff, 0);
		lbuf_smark(xb, lo, first, 0);
		lbuf_emark(xb, lo, last, 0);
	}
	if (rs != xkwdrs)
		rset_free(rs);
	free(rep);
	return first < 0 ? xuerr : NULL;
}

static void *ec_unknown(char *loc, char *cmd, char *arg)
{
	return "unknown command";
}

static void *ec_cmap(char *loc, char *cmd, char *arg)
{
	if (arg[0])
		xkmap_alt = conf_kmapfind(arg);
	else
		ex_print(conf_kmap(xkmap)[0])
	if (arg[0] && !strchr(cmd, '!'))
		xkmap = xkmap_alt;
	return NULL;
}

static void *ec_glob(char *loc, char *cmd, char *arg)
{
	int i, beg, end, not, matched = 0;
	char *pat, *s = arg;
	rset *rs;
	if (!loc[0] && !xgdep)
		loc = "%";
	if (ex_vregion(loc, &beg, &end))
		return xrerr;
	not = !!strchr(cmd, '!');
	pat = re_read(&s, 0);
	if (pat && *pat)
		rs = rset_smake(pat, xic ? REG_ICASE : 0);
	else
		rs = rset_smake(xregs['/'] ? xregs['/']->s : "", xic ? REG_ICASE : 0);
	free(pat);
	if (!rs)
		return xserr;
	xgdep = !xgdep ? 1 : xgdep * 2;
	for (i = beg; i < end; i++)
		lbuf_i(xb, i)->grec |= xgdep;
	for (i = beg; i < lbuf_len(xb);) {
		char *ln = lbuf_get(xb, i);
		lbuf_s(ln)->grec &= ~xgdep;
		if (rset_match(rs, ln, REG_NEWLINE) != not) {
			matched = 1;
			xrow = i;
			if (ex_exec(s))
				break;
			i = MIN(i, xrow);
		}
		while (i < lbuf_len(xb) && !(lbuf_i(xb, i)->grec & xgdep))
			i++;
	}
	rset_free(rs);
	xgdep /= 2;
	return matched ? NULL : xuerr;
}

static void *ec_while(char *loc, char *cmd, char *arg)
{
	int isdq = cmd[1] == '?';
	char *cond = isdq ? NULL : re_read(&arg, *cmd);
	char *ret = NULL, *branch;
	int inv = cmd[1 + isdq] == '!';
	char *then_cmd, *else_cmd;
	if (isdq && *loc) {
		int id = atoi(loc);
		if (!*arg) {
			int err = (xpret != NULL) ^ inv;
			if (!xanchor)
				sbuf_make(xanchor, 4 * (int)sizeof(int))
			sbuf_mem(xanchor, &id, (int)sizeof(id))
			sbuf_mem(xanchor, &err, (int)sizeof(err))
			return ret;
		} else if (!xanchor)
			return ret;
		then_cmd = re_read(&arg, *cmd);
		else_cmd = *arg ? re_read(&arg, *cmd) : NULL;
		int *ap = (int*)xanchor->s, n = xanchor->s_n / (int)sizeof(int);
		int and_res = 0, or_res = 1;
		for (int i = n; i >= 2;) {
			i -= 2;
			if (ap[i] != id)
				continue;
			and_res |= ap[i + 1];
			for (; *loc && *loc != ',' && *loc != ';'; loc++);
			if (!*loc || *loc == ';') {
				 or_res &= and_res;
				 and_res = 0;
			}
			if (!*loc) {
				branch = or_res ^ inv ? else_cmd : then_cmd;
				if (branch)
					ret = ex_exec(branch);
				break;
			}
			id = atoi(++loc);
			i = n;
		}
	} else {
		int count = *loc ? (*loc == '$' && cond ? INT_MAX : atoi(loc)) : 1;
		then_cmd = *arg ? re_read(&arg, *cmd) : NULL;
		else_cmd = *arg ? re_read(&arg, *cmd) : NULL;
		for (; count && !ret; count--) {
			ret = isdq ? xpret : (cond ? ex_exec(cond) : NULL);
			branch = (ret != NULL) ^ inv ? else_cmd : then_cmd;
			if (branch)
				ret = ex_exec(branch);
		}
	}
	free(cond);
	free(then_cmd);
	free(else_cmd);
	return ret;
}

static void *ec_join(char *loc, char *cmd, char *arg)
{
	int beg, end, o2 = 0;
	if (ex_vregion(loc, &beg, &end))
		return xrerr;
	xrow = beg;
	return lbuf_join(xb, beg, end+1, xoff, &o2, arg[0]) ? xuerr : NULL;
}

static void *ec_setdir(char *loc, char *cmd, char *arg)
{
	static char *exdir;
	if (cmd[1] == 'p') {
		free(exdir);
		exdir = *arg ? uc_dup(arg) : NULL;
	} else if (cmd[1] == 'd') {
		char *path = ex_pathresolve(*arg ? arg : (exdir ? exdir : "."));
		dir_calc(path);
		free(path);
	}
	return NULL;
}

static void *ec_chdir(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	char *path;
	struct stat st;
	if (!*arg) {
		ex_print(ex_vcwd)
		return NULL;
	}
	path = ex_pathresolve(arg);
	if (stat(path, &st) || !S_ISDIR(st.st_mode)) {
		free(path);
		return "chdir error";
	}
	for (int i = 0; i < xbufcur; i++) {
		if (!bufs[i].path[0] || bufs[i].path[0] == '/')
			continue;
		char *oldpath = ex_pathresolve(bufs[i].path);
		free(bufs[i].path);
		bufs[i].path = oldpath;
		bufs[i].plen = strlen(oldpath);
	}
	strncpy(ex_vcwd, path, sizeof(ex_vcwd) - 1);
	ex_vcwd[sizeof(ex_vcwd) - 1] = '\0';
	setenv("PWD", ex_vcwd, 1);
	free(path);
	return NULL;
#else
	char oldpath[4096];
	char newpath[4096];
	char *opath;
	int i, c, plen;
	oldpath[0] = '\0';
	oldpath[sizeof(oldpath)-1] = '\0';
	if (!getcwd(oldpath, sizeof(oldpath)))
		if ((opath = getenv("PWD")))
			strncpy(oldpath, opath, sizeof(oldpath)-1);
	if (!*arg) {
		ex_print(oldpath)
		return NULL;
	}
	plen = strlen(oldpath);
	i = plen == sizeof(oldpath)-1;
	if (chdir(arg))
		return "chdir error";
	if (!getcwd(newpath, sizeof(newpath)))
		return "getcwd error";
	setenv("PWD", newpath, 1);
	if (i)
		return "oldpath >= 4096";
	if (plen && oldpath[plen-1] != '/')
		oldpath[plen++] = '/';
	for (i = 0; i < xbufcur; i++) {
		if (!bufs[i].path[0])
			continue;
		if (bufs[i].path[0] == '/') {
			opath = bufs[i].path;
		} else {
			opath = oldpath;
			strncpy(opath+plen, bufs[i].path, sizeof(oldpath)-plen-1);
		}
		for (c = 0; opath[c] && opath[c] == newpath[c]; c++);
		if (newpath[c] || !opath[c])
			c = 0;
		else if (opath[c] == '/')
			c++;
		opath = uc_dup(opath+c);
		free(bufs[i].path);
		bufs[i].path = opath;
		bufs[i].plen = strlen(opath);
	}
	return NULL;
#endif
}

static void *ec_setincl(char *loc, char *cmd, char *arg)
{
	rset_free(fsincl);
	if (!*arg)
		fsincl = NULL;
	else if (!(fsincl = rset_smake(arg, xic ? REG_ICASE : 0)))
		return xserr;
	return NULL;
}

static void *ec_setacreg(char *loc, char *cmd, char *arg)
{
	if (xacreg)
		sbuf_free(xacreg)
	if (*arg) {
		sbuf_make(xacreg, 128)
		sbufn_str(xacreg, arg)
	} else
		xacreg = NULL;
	return NULL;
}

static void *ec_setbufsmax(char *loc, char *cmd, char *arg)
{
	xbufsmax = *arg ? atoi(arg) : xbufsalloc;
	if (xbufsmax <= 0)
		return xserr;
	int bufidx = ex_buf - bufs;
	int pbufidx = ex_pbuf - bufs;
	int tpbufidx = ex_tpbuf - bufs;
	int istemp = !ex_buf ? 0 : istempbuf(ex_buf);
	for (; xbufcur > xbufsmax; xbufcur--)
		bufs_free(xbufcur - 1);
	bufs = erealloc(bufs, sizeof(struct buf) * xbufsmax);
	if (!istemp)
		ex_buf = bufidx >= &bufs[xbufsmax] - bufs ? bufs : bufs+bufidx;
	ex_pbuf = pbufidx >= &bufs[xbufsmax] - bufs ? bufs : bufs+pbufidx;
	ex_tpbuf = tpbufidx >= &bufs[xbufsmax] - bufs ? bufs : bufs+tpbufidx;
	return NULL;
}

static void *ec_regprint(char *loc, char *cmd, char *arg)
{
	if (*loc && *arg) {
		int reg = atoi(loc);
		if (reg < 0 || reg > 255)
			return xserr;
		ex_regput(reg, arg, cmd[3] == '+');
		return NULL;
	}
	static char buf[5] = "  ";
	int flg = (xvis & 2) == 0;
	for (int i = 1; i < LEN(xregs); i++) {
		if (xregs[i] && i != xpr) {
			*buf = i;
			ex_cprint2(buf, -1, 0, 0, flg)
			ex_cprint2(xregs[i]->s, -1, xleft ? 0 : 2, xleft, !flg)
		}
	}
	return NULL;
}

static void *ec_setenc(char *loc, char *cmd, char *arg)
{
	if (cmd[0] == 'p') {
		if (!*arg) {
			if (ph != _ph)
				free(ph);
			phlen = LEN(_ph);
			ph = _ph;
			return NULL;
		} else if (ph == _ph) {
			ph = NULL;
			phlen = 0;
		}
		ph = erealloc(ph, sizeof(struct placeholder) * (phlen + 1));
		ph[phlen].cp[0] = strtol(arg, &arg, 0);
		ph[phlen].cp[1] = strtol(arg, &arg, 0);
		ph[phlen].wid = strtol(arg, &arg, 0);
		ph[phlen].l = strtol(arg, &arg, 0);
		if (strlen(arg) && strlen(arg) < LEN(ph[0].d))
			strcpy(ph[phlen++].d, arg);
		return NULL;
	}
	if (cmd[1] == 'z')
		zwlen = !zwlen ? def_zwlen : 0;
	else if (cmd[1] == 'b')
		bclen = !bclen ? def_bclen : 0;
	else if (utf8_length[0xc0] == 1) {
		memset(utf8_length+0xc0, 2, 0xe0 - 0xc0);
		memset(utf8_length+0xe0, 3, 0xf0 - 0xe0);
		memset(utf8_length+0xf0, 4, 0xf8 - 0xf0);
	} else
		memset(utf8_length+1, 1, 255);
	return NULL;
}

static void *ec_specials(char *loc, char *cmd, char *arg)
{
	int i = 0;
	if (*loc) {
		i = atoi(loc);
		goto direct;
	}
	xesc = cmd[2] ? 0 : '\\';
	xsep = cmd[2] ? 0 : ':';
	xexp = cmd[2] ? 0 : '%';
	for (; *arg; arg++, i++) {
		direct:
		if (i == 0)
			xesc = *arg;
		else if (i == 1)
			xsep = *arg;
		else if (i == 2)
			xexp = *arg;
	}
	return NULL;
}

void ex_regesc(sbuf *sb, char *beg, char *end, int ex)
{
	for (; beg < end; beg++) {
		if (ex && (*beg == xsep || *beg == xesc)) {
			sbuf_chr(sb, xesc)
			if (*beg == '\\')
				sbuf_chr(sb, '\\')
		}
		if (strchr("!%{}[]().?\\^$|*/+", *beg))
			sbuf_chr(sb, '\\')
		sbuf_chr(sb, *beg)
	}
}

static void *ec_krsset(char *loc, char *cmd, char *arg)
{
	if (*arg && !*loc)
		ex_krsset(arg, +1);
	else {
		int beg, end, o1 = 0, o2 = -1;
		if (ex_region(loc, &beg, &end, &o1, &o2))
			return xrerr;
		sbuf reg;
		lbuf_region(xb, &reg, beg, o1, end - 1, o2);
		sbuf_smake(sb, 64)
		ex_regesc(sb, reg.s, reg.s + reg.s_n, 1);
		free(reg.s);
		sbuf_null(sb)
		ex_krsset(sb->s, +1);
		free(sb->s);
	}
	return xkwdrs ? NULL : xserr;
}

static void *ec_rtc(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	char buf[32];
	const char *err;
	(void)loc;
	(void)cmd;
	if (*arg) {
		err = typewrt_rtc_set_datetime(arg, buf, sizeof(buf));
		if (err)
			return (void*)err;
	} else if (!typewrt_rtc_get_datetime(buf, sizeof(buf)))
		return "rtc read failed";
	ex_print(buf)
	return NULL;
#else
	return "unsupported command";
#endif
}

static void *ec_off(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	(void)loc;
	(void)cmd;
	(void)arg;
	return typewrt_power_off() ? NULL : "power off failed: sd card busy";
#else
	return "unsupported command";
#endif
}

static void *ec_menu(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	(void)loc;
	(void)cmd;
	(void)arg;
	xquit = !xquit ? 1 : xquit;
	return NULL;
#else
	return "unsupported command";
#endif
}

static void *ec_about(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	(void)loc;
	(void)cmd;
	(void)arg;
	nextvi_about_show();
	xredraw = 1;
	return NULL;
#else
	return "unsupported command";
#endif
}

static void *ec_battery(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	char buf[96];
	(void)loc;
	(void)cmd;
	(void)arg;
	typewrt_battery_get_status(buf, sizeof(buf));
	ex_print(buf)
	return NULL;
#else
	return "unsupported command";
#endif
}

static void *ec_power(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	char buf[96];
	(void)loc;
	(void)cmd;
	(void)arg;
	for (int i = 0; typewrt_power_get_status_line(i, buf, sizeof(buf)); i++)
		ex_print(buf)
	return NULL;
#else
	return "unsupported command";
#endif
}

static void *ec_ble(char *loc, char *cmd, char *arg)
{
#ifdef NEXTVI_EMBEDDED
	char msg[128], *fspath;
	char *a, *end;
	const char *err;
	sbuf payload;
	(void)loc;
	(void)cmd;

	a = arg;
	while (*a == ' ' || *a == '\t')
		a++;
	end = a + strlen(a);
	while (end > a && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';

	if (!strcmp(a, "off")) {
		typewrt_ble_stop();
		typewrt_ble_get_status(msg, sizeof(msg));
		ex_print(msg)
		return NULL;
	}
	if (!strcmp(a, "status")) {
		typewrt_ble_get_status(msg, sizeof(msg));
		ex_print(msg)
		return NULL;
	}
	if (!strcmp(a, "recv") || !strcmp(a, "receive"))
		return "ble recv is menu-only";

	if (*a) {
		fspath = ex_pathresolve(a);
		err = typewrt_ble_send_file(a, fspath);
		free(fspath);
	} else {
		if (lbuf_len(xb))
			lbuf_region(xb, &payload, 0, 0, lbuf_len(xb) - 1, -1);
		else {
			_sbuf_make((&payload), 1,)
			sbuf_null((&payload))
		}
		err = typewrt_ble_send_buffer(*xb_path ? xb_path : "unnamed.txt",
			payload.s, payload.s_n);
		free(payload.s);
	}
	if (err)
		return (void*)err;
	typewrt_ble_get_status(msg, sizeof(msg));
	ex_print(msg)
	return NULL;
#else
	return "unsupported command";
#endif
}

static int eo_val(char *arg)
{
	int val = atoi(arg);
	if (!val && !isdigit((unsigned char)*arg))
		return (unsigned char)*arg;
	return val;
}

#define _EO(opt, inner) \
static void *eo_##opt(char *loc, char *cmd, char *arg) { inner }

#define EO(opt) \
	_EO(opt, x##opt = !*arg ? !x##opt : eo_val(arg); return NULL;)

EO(pac) EO(pr) EO(ai) EO(err) EO(ic) EO(mpt)
EO(seq) EO(ts) EO(lim) EO(led) EO(vis)

_EO(grp, xgrp = (!*arg ? !xgrp : eo_val(arg)) * 2; return NULL;)

_EO(left,
	if (*loc)
		xleft = (xcols / 2) * atoi(loc);
	else if (*arg)
		xleft = atoi(arg);
	else if (lbuf_get(xb, xrow))
		xleft = ren_position(lbuf_get(xb, xrow))->pos[MIN(xoff, rstate->n)];
	return NULL;
)

#undef EO
#define EO(opt) {#opt, eo_##opt}

/* commands & opts must be sorted longest of its kind topmost */
static struct excmd {
	char *name;
	void *(*ec)(char *loc, char *cmd, char *arg);
} excmds[] = {
	{"@", ec_termexec},
	{"&", ec_termexec},
	{"?\?!", ec_while},
	{"??", ec_while},
	{"?!", ec_while},
	{"?", ec_while},
	{"about", ec_about},
	{"bp", ec_setpath},
	{"bs", ec_bufsave},
	{"bw!", ec_bufwipe},
	{"bw", ec_bufwipe},
	{"bx", ec_setbufsmax},
	{"battery", ec_battery},
	{"bat", ec_battery},
	{"ble", ec_ble},
	{"b", ec_buffer},
	EO(pac),
	EO(pr),
	{"power", ec_power},
	{"pu", ec_put},
	{"ph", ec_setenc},
	{"p", ec_print},
	EO(ai),
	{"ac", ec_setacreg},
	{"a", ec_insert},
	EO(err),
	{"ef!", ec_fuzz},
	{"ef", ec_fuzz},
	{"e!", ec_edit},
	{"e", ec_edit},
	{"ft", ec_unknown},
	{"fd", ec_setdir},
	{"fp", ec_setdir},
	{"f+", ec_find},
	{"f-", ec_find},
	{"f>", ec_find},
	{"f<", ec_find},
	{"f", ec_fuzz},
	{"help", ec_help},
	{"inc", ec_setincl},
	EO(ic),
	{"i", ec_insert},
	{"d", ec_delete},
	EO(grp),
	{"gmarks", ec_gmarks},
	{"g!", ec_glob},
	{"g", ec_glob},
	EO(mpt),
	{"menu", ec_menu},
	{"m", ec_mark},
	{"off", ec_off},
	{"q!", ec_quit},
	{"q", ec_quit},
	{"reg+", ec_regprint},
	{"reg", ec_regprint},
	{"re", ec_krsset},
	{"rd", ec_undoredo},
	{"rtc", ec_rtc},
	{"r", ec_read},
	{"wq!", ec_write},
	{"wq", ec_write},
	{"w!", ec_write},
	{"w", ec_write},
	{"uc", ec_setenc},
	{"uz", ec_setenc},
	{"ub", ec_setenc},
	{"ud", ec_undoredo},
	{"version", ec_about},
	{"ver", ec_about},
	EO(seq),
	{"sc!", ec_specials},
	{"sc", ec_specials},
	{"s", ec_substitute},
	{"x!", ec_write},
	{"x", ec_write},
	{"ya!", ec_yank},
	{"ya", ec_yank},
	{"cm!", ec_cmap},
	{"cm", ec_cmap},
	{"cd", ec_chdir},
	{"c", ec_insert},
	{"j", ec_join},
	EO(ts),
	EO(left),
	EO(lim),
	EO(led),
	EO(vis),
	{"=", ec_num},
	{"", ec_print}, /* do not remove */
	{"", ec_print}, /* do not remove */
};

/* parse command argument expanding % */
static const char *ex_arg(const char *src, sbuf *sb, int *arg)
{
	*arg = sb->s_n;
	while (*src && *src != xsep) {
		if (*src == xexp) {
			src++;
			if (*src == '@' && src[1] && src[1] != xesc) {
				sbuf *reg = xregs[(unsigned char)src[1]];
				if (reg)
					sbuf_mem(sb, reg->s, reg->s_n)
				src += 2;
			} else {
				struct buf *pbuf = ex_buf;
				int n;
				if (*src == '#') {
					src++;
					pbuf = ex_pbuf;
				} else if ((*src ^ '0') < 10) {
					pbuf = &bufs[n = atoi(src)];
					src += nextvi_itoalen(n);
				}
				src += *src == xesc && src[-1] != '#' && (src[1] ^ '0') < 10;
				if (pbuf >= bufs && pbuf < &bufs[xbufcur] && pbuf->path[0])
					sbuf_mem(sb, pbuf->path, pbuf->plen)
			}
		} else {
			if (*src == xesc && (src[1] == xsep || src[1] == xexp
					|| src[1] == xesc) && src[1])
				src++;
			sbuf_chr(sb, *src++)
		}
	}
	sbuf_null(sb)
	return src;
}

/* parse prefix and command */
static const char *ex_cmd(const char *src, sbuf *sb, int *idx)
{
	int i, j;
	char *dst = sb->s;
	if ((*src && *src == xsep) || (*idx == LEN(excmds) - 1))
		src++;
	while (memchr(" \t0123456789+-.,<>/$';%*#|", *src, 26)) {
		if (*src == '\'' && src[1])
			*dst++ = *src++;
		if (*src == '>' || *src == '<' || *src == '|') {
			j = *src;
			do {
				*dst++ = *src++;
			} while (*src && (*src != j || src[-1] == '\\'));
			if (*src)
				*dst++ = *src++;
		} else if (*src == ' ' || *src == '\t')
			src++;
		else
			*dst++ = *src++;
	}
	*dst++ = '\0';
	sb->s_n = dst - sb->s;
	if (*src == xsep) {
		*idx = LEN(excmds) - 1;
		return src;
	}
	for (i = 0; i < LEN(excmds); i++) {
		for (j = 0; excmds[i].name[j]; j++)
			if (!src[j] || src[j] != excmds[i].name[j])
				break;
		if (!excmds[i].name[j]) {
			*idx = i;
			src += j;
			break;
		}
	}
	if (*src == ' ' || *src == '\t')
		src++;
	return src;
}

/* execute a single ex command chain */
void *ex_exec(const char *ln)
{
	int arg, idx = 0;
	char *ret = NULL;
	preserve(int, xquit, xquit = 0;)
	if (!xexec_dep)
		lbuf_mark(xb, '*', xrow, xoff);
	xexec_dep++;
	sbuf_smake(sb, strlen(ln) + 4)
	do {
		sbuf_cut(sb, 0)
		ln = ex_arg(ex_cmd(ln, sb, &idx), sb, &arg);
		ret = excmds[idx].ec(sb->s, excmds[idx].name, sb->s + arg);
		xpret = ret;
		if (ret && ret != xuerr && xerr & 1) {
			ex_print(ret)
			ret = xuerr;
		}
		if (ret && xerr & 2)
			break;
	} while (*ln && !xquit);
	free(sb->s);
	xexec_dep--;
	if (xquit > 0 && (xexec_dep || xqprop >= 0) && --xqprop < 0)
		restore(xquit)
	if (!xexec_dep) {
		if (xanchor) {
			sbuf_free(xanchor)
			xanchor = NULL;
		}
		xqprop = 0;
	}
	return xerr & 4 ? NULL : ret;
}

/* ex main loop */
void ex(void)
{
	xgrec++;
	int esc = 0;
	sbuf_smake(sb, xcols)
	while (!xquit) {
		if (ex_read(sb, ":", NULL, 0, 1) == '\n') {
			if (!strcmp(sb->s, ":") && esc) {
				xpln = 2;
				ex_exec(xregs[':']->s);
			} else
				ex_command(sb->s + !(xvis & 1))
			xb->useq += xseq;
			esc = 1;
		} else
			esc = 0;
		sbuf_cut(sb, 0)
	}
	free(sb->s);
	xgrec--;
}

void ex_init(char **files, int n)
{
	xbufsalloc = MAX(n, xbufsalloc);
	ec_setbufsmax(NULL, NULL, "");
	char *s = files[0] ? files[0] : "";
	do {
		xmpt = 0;
		ec_edit("", "e", s);
		s = *(++files);
	} while (--n > 0);
	xvis &= ~4;
	if ((s = getenv("EXINIT")))
		ex_command(s)
}
