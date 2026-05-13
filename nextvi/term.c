#ifndef NEXTVI_NOTERM
static struct termios termios;
#endif
sbuf *term_sbuf;
int term_record;
int term_winch;
int term_resized;
int xrows, xcols;
unsigned int ibuf_pos, ibuf_cnt, ibuf_sz = 128, icmd_pos;
unsigned char *ibuf, icmd[4096];
unsigned int texec, tn;

#ifdef NEXTVI_NOTERM
static char term_screen[NEXTVI_DISPLAY_ROWS + 1][NEXTVI_DISPLAY_COLS + 1];
static unsigned char term_dirty[NEXTVI_DISPLAY_ROWS + 1];
static int term_row, term_col, term_cursor_on = 1;
static int term_cursor_row = -1, term_cursor_col = -1, term_cursor_drawn;
static int term_cursor_suspended;
static unsigned char kq[128];
static unsigned int kq_r, kq_w;
static int key_shift, key_ctrl, key_alt, key_win, key_caps;

/* Aligned with the compact key codes produced by KBDMAP in keyboard_input.h. */
static const unsigned char key_normal[64] = {
	0, TK_ESC, '1', '2', '3', '4', '5', '6',
	'7', '8', '9', '0', '-', '=', 127, '\t',
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
	'o', 'p', '[', ']', '\n', 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
	'\'', '\\', '<', 'z', 'x', 'c', 'v', 'b',
	'n', 'm', ',', '.', '/', ' ', 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

static const unsigned char key_shifted[64] = {
	0, '~', '!', '@', '#', '$', '%', '^',
	'&', '*', '(', ')', '_', '+', 127, '\t',
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
	'O', 'P', '{', '}', '\n', 0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
	'"', '|', '<', 'Z', 'X', 'C', 'V', 'B',
	'N', 'M', '<', '>', '?', ' ', 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

static void noterm_dirty(int row)
{
	if (row >= 0 && row <= NEXTVI_DISPLAY_ROWS)
		term_dirty[row] = 1;
}

static void noterm_refresh_dirty(void)
{
	int cursor_erased = 0;
	for (int r = 0; r <= NEXTVI_DISPLAY_ROWS; r++) {
		if (!term_dirty[r])
			continue;
		if (term_cursor_drawn && r == term_cursor_row)
			cursor_erased = 1;
		nextvi_display_refresh_line(r, term_screen[r], NEXTVI_DISPLAY_COLS);
		term_dirty[r] = 0;
	}
	if (cursor_erased)
		term_cursor_drawn = 0;
	if (term_cursor_suspended)
		return;
	if (term_cursor_drawn && (!term_cursor_on ||
			term_cursor_row != term_row || term_cursor_col != term_col)) {
		if (term_cursor_on && term_row >= 0 &&
				term_row <= NEXTVI_DISPLAY_ROWS &&
				term_col >= 0 && term_col < NEXTVI_DISPLAY_COLS) {
			if (cursor_erased)
				nextvi_display_refresh_cursor(term_row, term_col, 1);
			else
				nextvi_display_move_cursor(term_cursor_row,
					term_cursor_col, term_row, term_col, 1);
			term_cursor_row = term_row;
			term_cursor_col = term_col;
			term_cursor_drawn = 1;
		} else {
			if (!cursor_erased)
				nextvi_display_refresh_cursor(term_cursor_row,
					term_cursor_col, 0);
			term_cursor_drawn = 0;
		}
		return;
	}
	if (term_cursor_drawn && !cursor_erased)
		return;
	if (term_cursor_on && term_row >= 0 && term_row <= NEXTVI_DISPLAY_ROWS &&
			term_col >= 0 && term_col < NEXTVI_DISPLAY_COLS) {
		nextvi_display_refresh_cursor(term_row, term_col, 1);
		term_cursor_row = term_row;
		term_cursor_col = term_col;
		term_cursor_drawn = 1;
	}
}

static void noterm_refresh_cursor(void)
{
	if (term_cursor_suspended || term_record)
		return;
	noterm_refresh_dirty();
}

static void noterm_clear_line(int row, int col)
{
	if (row < 0 || row > NEXTVI_DISPLAY_ROWS)
		return;
	col = MAX(0, MIN(col, NEXTVI_DISPLAY_COLS));
	memset(term_screen[row] + col, ' ', NEXTVI_DISPLAY_COLS - col);
	term_screen[row][NEXTVI_DISPLAY_COLS] = '\0';
	noterm_dirty(row);
}

static void noterm_room(int n)
{
	int count = abs(n);
	int last = term_row < xrows ? xrows - 1 : NEXTVI_DISPLAY_ROWS;
	if (!n || term_row < 0 || term_row > NEXTVI_DISPLAY_ROWS)
		return;
	last = MIN(last, NEXTVI_DISPLAY_ROWS);
	count = MIN(count, last - term_row + 1);
	if (n > 0) {
		for (int r = last; r >= term_row + count; r--) {
			memcpy(term_screen[r], term_screen[r - count],
				NEXTVI_DISPLAY_COLS + 1);
			noterm_dirty(r);
		}
		for (int r = term_row; r < term_row + count; r++)
			noterm_clear_line(r, 0);
	} else {
		for (int r = term_row; r <= last - count; r++) {
			memcpy(term_screen[r], term_screen[r + count],
				NEXTVI_DISPLAY_COLS + 1);
			noterm_dirty(r);
		}
		for (int r = last - count + 1; r <= last; r++)
			noterm_clear_line(r, 0);
	}
}

static void noterm_put(int ch)
{
	if (ch == '\033')
		return;
	if (ch == '\r') {
		term_col = 0;
		return;
	}
	if (ch == '\n') {
		term_row = MIN(term_row + 1, NEXTVI_DISPLAY_ROWS);
		term_col = 0;
		return;
	}
	if (term_row < 0 || term_row > NEXTVI_DISPLAY_ROWS)
		return;
	if (term_col >= 0 && term_col < NEXTVI_DISPLAY_COLS) {
		term_screen[term_row][term_col] = ch ? ch : ' ';
		noterm_dirty(term_row);
	}
	term_col++;
}

static void noterm_csi(int cmd, int a, int b, int got_b)
{
	if (cmd == 'H') {
		term_row = MAX(0, MIN(a - 1, NEXTVI_DISPLAY_ROWS));
		term_col = MAX(0, MIN((got_b ? b : 1) - 1, NEXTVI_DISPLAY_COLS));
	} else if (cmd == 'C') {
		term_col = MAX(0, MIN(term_col + MAX(1, a), NEXTVI_DISPLAY_COLS));
	} else if (cmd == 'D') {
		term_col = MAX(0, MIN(term_col - MAX(1, a), NEXTVI_DISPLAY_COLS));
	} else if (cmd == 'K') {
		noterm_clear_line(term_row, term_col);
	} else if (cmd == 'L') {
		noterm_room(MAX(1, a));
	} else if (cmd == 'M') {
		noterm_room(-MAX(1, a));
	}
}

static void noterm_write_n(char *s, unsigned int n)
{
	for (unsigned int i = 0; i < n; i++) {
		if (s[i] == '\033' && i + 1 < n && s[i + 1] == '[') {
			int a = 0, b = 0, got_b = 0;
			i += 2;
			while (i < n && s[i] >= '0' && s[i] <= '9')
				a = a * 10 + s[i++] - '0';
			if (i < n && s[i] == ';') {
				got_b = 1;
				i++;
				while (i < n && s[i] >= '0' && s[i] <= '9')
					b = b * 10 + s[i++] - '0';
			}
			while (i < n && !isalpha((unsigned char)s[i]))
				i++;
			if (i < n)
				noterm_csi((unsigned char)s[i], a, b, got_b);
			continue;
		}
		noterm_put((unsigned char)s[i]);
	}
}

static void noterm_write_esc(char *s)
{
	for (; *s; s++) {
		if (*s == '\033' && s[1] == '[') {
			s += 2;
			while (*s && !isalpha((unsigned char)*s))
				s++;
			continue;
		}
		noterm_put((unsigned char)*s);
	}
}

static int noterm_modifier_bit(unsigned char code)
{
	switch (code & NEXTVI_MOD_CODE_MASK) {
	case NEXTVI_MOD_SHIFT: return NEXTVI_MOD_SHIFT;
	case NEXTVI_MOD_CTRL: return NEXTVI_MOD_CTRL;
	case NEXTVI_MOD_ALT: return NEXTVI_MOD_ALT;
	case NEXTVI_MOD_WIN: return NEXTVI_MOD_WIN;
	case NEXTVI_MOD_CAPS: return NEXTVI_MOD_CAPS;
	default: return 0;
	}
}

static void noterm_modifier(unsigned char ev)
{
	int *state = NULL, bit = noterm_modifier_bit(ev);
	if (!bit)
		return;
	if (bit == NEXTVI_MOD_CAPS) {
		if (ev & NEXTVI_KEY_PRESS)
			key_caps = !key_caps;
		return;
	}
	if (bit == NEXTVI_MOD_SHIFT)
		state = &key_shift;
	else if (bit == NEXTVI_MOD_CTRL)
		state = &key_ctrl;
	else if (bit == NEXTVI_MOD_ALT)
		state = &key_alt;
	else if (bit == NEXTVI_MOD_WIN)
		state = &key_win;
	*state = !!(ev & NEXTVI_KEY_PRESS);
}

static int noterm_key_event_timeout(int timeout_ms)
{
	unsigned char ev, code, ch;
	while ((timeout_ms >= 0 ?
			nextvi_keyboard_read_timeout(&ev, timeout_ms) :
			nextvi_keyboard_read(&ev)) > 0) {
		if (ev & NEXTVI_KEY_MODIFIER) {
			noterm_modifier(ev);
			continue;
		}
		if (!(ev & NEXTVI_KEY_PRESS))
			continue;
		code = ev & NEXTVI_KEY_CODE_MASK;
		ch = (key_shift ^ (key_caps && key_normal[code] >= 'a' &&
			key_normal[code] <= 'z')) ? key_shifted[code] : key_normal[code];
		if (key_ctrl && ch >= 'a' && ch <= 'z')
			ch = TK_CTL(ch);
		else if (key_ctrl && ch >= 'A' && ch <= 'Z')
			ch = ((ch - 'A') + 'a') & 037;
		(void)key_alt;
		(void)key_win;
		if (ch)
			return ch;
	}
	return 0;
}

static int noterm_key_event(void)
{
	return noterm_key_event_timeout(-1);
}

__attribute__((weak)) void nextvi_display_refresh_line(int row, const char *text, int cols)
{
	(void)row;
	(void)text;
	(void)cols;
}

__attribute__((weak)) void nextvi_display_refresh_line_inverted(int row, const char *text, int cols)
{
	nextvi_display_refresh_line(row, text, cols);
}

__attribute__((weak)) void nextvi_display_draw_hline(int y, int color)
{
	(void)y;
	(void)color;
}

__attribute__((weak)) void nextvi_display_refresh_cursor(int row, int col, int on)
{
	(void)row;
	(void)col;
	(void)on;
}

__attribute__((weak)) void nextvi_display_move_cursor(int old_row, int old_col,
	int new_row, int new_col, int on)
{
	nextvi_display_refresh_cursor(old_row, old_col, 0);
	if (on)
		nextvi_display_refresh_cursor(new_row, new_col, 1);
}

__attribute__((weak)) void nextvi_display_note_insert(void)
{
}

int nextvi_keyboard_queue_push(unsigned char event)
{
	unsigned int next = (kq_w + 1) % LEN(kq);
	if (next == kq_r)
		return 0;
	kq[kq_w] = event;
	kq_w = next;
	return 1;
}

int nextvi_keyboard_queue_pop(unsigned char *event)
{
	if (kq_r == kq_w)
		return 0;
	*event = kq[kq_r];
	kq_r = (kq_r + 1) % LEN(kq);
	return 1;
}

__attribute__((weak)) int nextvi_keyboard_read(unsigned char *event)
{
	return nextvi_keyboard_queue_pop(event);
}
#endif

void term_init(void)
{
#ifndef NEXTVI_NOTERM
	struct winsize win;
	struct termios newtermios;
	char *s;
#endif
	term_winch = 0;
	term_resized++;
	sbuf_make(term_sbuf, 2048)
#ifdef NEXTVI_NOTERM
	xcols = NEXTVI_DISPLAY_COLS;
	xrows = NEXTVI_DISPLAY_ROWS;
	term_row = term_col = 0;
	for (int r = 0; r <= NEXTVI_DISPLAY_ROWS; r++) {
		memset(term_screen[r], ' ', NEXTVI_DISPLAY_COLS);
		term_screen[r][NEXTVI_DISPLAY_COLS] = '\0';
		term_dirty[r] = 1;
	}
	noterm_refresh_dirty();
#else
	tcgetattr(0, &termios);
	newtermios = termios;
	newtermios.c_lflag &= ~(ICANON | ISIG | ECHO);
	tcsetattr(0, TCSAFLUSH, &newtermios);
	if (!ioctl(0, TIOCGWINSZ, &win)) {
		xcols = win.ws_col;
		xrows = win.ws_row;
	} else {
		if ((s = getenv("LINES")))
			xrows = atoi(s);
		if ((s = getenv("COLUMNS")))
			xcols = atoi(s);
	}
	xcols = xcols ? xcols : 80;
	xrows = xrows ? xrows : 25;
#endif
}

void term_done(void)
{
	if (!term_sbuf)
		return;
	term_cursor(1);
	term_commit();
	sbuf_free(term_sbuf)
#ifndef NEXTVI_NOTERM
	tcsetattr(0, 0, &termios);
#endif
}

void term_clean(void)
{
#ifdef NEXTVI_NOTERM
	for (int r = 0; r <= NEXTVI_DISPLAY_ROWS; r++)
		noterm_clear_line(r, 0);
	term_row = term_col = 0;
	noterm_refresh_dirty();
#else
	term_write("\x1b[2J", 4)	/* clear screen */
	term_write("\x1b[H", 3)		/* cursor topleft */
#endif
}

void term_commit(void)
{
#ifdef NEXTVI_NOTERM
	noterm_write_n(term_sbuf->s, term_sbuf->s_n);
	noterm_refresh_dirty();
#else
	term_write(term_sbuf->s, term_sbuf->s_n)
#endif
	sbuf_cut(term_sbuf, 0)
	term_record = 0;
}

static void term_out(char *s)
{
	if (term_record)
		sbufn_str(term_sbuf, s)
	else {
#ifdef NEXTVI_NOTERM
		noterm_write_esc(s);
		noterm_refresh_dirty();
#else
		term_write(s, strlen(s))
#endif
	}
}

void term_chr(int ch)
{
	char s[4] = {ch};
	term_out(s);
}

void term_kill(void)
{
#ifdef NEXTVI_NOTERM
	if (term_record)
		sbufn_str(term_sbuf, "\33[K")
	else
		noterm_clear_line(term_row, term_col);
#else
	term_out("\33[K");
#endif
}

void term_room(int n)
{
#ifdef NEXTVI_NOTERM
	char cmd[64] = "\33[";
	if (!n)
		return;
	if (term_record) {
		char *s = nextvi_itoa(abs(n), cmd+2);
		s[0] = n < 0 ? 'M' : 'L';
		s[1] = '\0';
		sbufn_str(term_sbuf, cmd)
	} else {
		noterm_room(n);
	}
#else
	char cmd[64] = "\33[";
	if (!n)
		return;
	char *s = nextvi_itoa(abs(n), cmd+2);
	s[0] = n < 0 ? 'M' : 'L';
	s[1] = '\0';
	term_out(cmd);
#endif
}

void term_pos(int r, int c)
{
#ifdef NEXTVI_NOTERM
	char buf[64] = "\r\33[", *s;
	if (term_record) {
		if (r < 0) {
			memcpy(nextvi_itoa(MAX(0, c), buf+3), c > 0 ? "C" : "D", 2);
			sbufn_str(term_sbuf, buf)
		} else {
			s = nextvi_itoa(r + 1, buf+3);
			if (c > 0) {
				*s++ = ';';
				s = nextvi_itoa(c + 1, s);
			}
			memcpy(s, "H", 2);
			sbufn_str(term_sbuf, buf+1)
		}
	} else if (r < 0) {
		term_col = MAX(0, MIN(c, NEXTVI_DISPLAY_COLS));
	} else {
		term_row = MAX(0, MIN(r, NEXTVI_DISPLAY_ROWS));
		term_col = MAX(0, MIN(c, NEXTVI_DISPLAY_COLS));
	}
#else
	char buf[64] = "\r\33[", *s;
	if (r < 0) {
		memcpy(nextvi_itoa(MAX(0, c), buf+3), c > 0 ? "C" : "D", 2);
		term_out(buf);
	} else {
		s = nextvi_itoa(r + 1, buf+3);
		if (c > 0) {
			*s++ = ';';
			s = nextvi_itoa(c + 1, s);
		}
		memcpy(s, "H", 2);
		term_out(buf+1);
	}
#endif
}

void term_cursor(int on)
{
#ifdef NEXTVI_NOTERM
	term_cursor_on = on;
	noterm_refresh_cursor();
#else
	term_out(on ? "\33[?25h" : "\33[?25l");
#endif
}

void term_cursor_suspend(int on)
{
#ifdef NEXTVI_NOTERM
	term_cursor_suspended += on ? 1 : -1;
	if (term_cursor_suspended < 0)
		term_cursor_suspended = 0;
#else
	(void)on;
#endif
}

/* read s before reading from the input backend */
void term_push(char *s, unsigned int n)
{
	static unsigned int tibuf_pos, tibuf_cnt;
	if (texec == '@' && xquit > 0) {
		xquit = 0;
		tn = 0;
		ibuf_cnt = tibuf_cnt;
		ibuf_pos = tibuf_cnt;
	}
	if (ibuf_cnt + n >= ibuf_sz || ibuf_sz - ibuf_cnt + n > 128) {
		ibuf_sz = ibuf_cnt + n + 128;
		ibuf = erealloc(ibuf, ibuf_sz);
	}
	if (texec) {
		if (tibuf_pos != ibuf_pos)
			tn = 0;
		memmove(ibuf + ibuf_pos + n + tn,
			ibuf + ibuf_pos + tn, ibuf_cnt - ibuf_pos - tn);
		memcpy(ibuf + ibuf_pos + tn, s, n);
		tn += n;
		tibuf_pos = ibuf_pos;
	} else
		memcpy(ibuf + ibuf_cnt, s, n);
	tibuf_cnt = ibuf_cnt;
	ibuf_cnt += n;
}

void term_back(int c)
{
	char s[1] = {c};
	term_push(s, 1);
}

int term_read(int winch)
{
#ifndef NEXTVI_NOTERM
	static struct pollfd ufd = {STDIN_FILENO, POLLIN};
	int cw;
#endif
	if (ibuf_pos >= ibuf_cnt) {
		if (texec) {
			xquit = !xquit ? 1 : xquit;
			if (texec == '&')
				goto err;
		}
		if (term_winch && winch) {
			*ibuf = winch;	/* yield until term_winch is cleared */
			goto ret;
		}
#ifdef NEXTVI_NOTERM
		if (!(*ibuf = noterm_key_event())) {
			err:
			*ibuf = 0;
		}
#else
		cw = 0;
		re:
		/* read a single input character */
		if (xquit < 0 || poll(&ufd, 1, -1) <= 0 ||
				read(STDIN_FILENO, ibuf, 1) <= 0) {
			xquit = !isatty(STDIN_FILENO) ? -1 : xquit;
			if (term_winch && winch && xquit >= 0) {
				*ibuf = winch;
				goto ret;
			} else if (term_winch != cw && !winch && xquit >= 0) {
				cw = term_winch;
				goto re;
			}
			err:
			*ibuf = 0;
		}
#endif
		ret:
		ibuf_cnt = 1;
		ibuf_pos = 0;
	}
	if (icmd_pos < sizeof(icmd))
		icmd[icmd_pos++] = ibuf[ibuf_pos];
	return ibuf[ibuf_pos++];
}

int term_read_timeout(int winch, int timeout_ms)
{
#ifndef NEXTVI_NOTERM
	(void)timeout_ms;
	return term_read(winch);
#else
	if (ibuf_pos >= ibuf_cnt) {
		if (texec) {
			xquit = !xquit ? 1 : xquit;
			if (texec == '&')
				goto err;
		}
		if (term_winch && winch) {
			*ibuf = winch;
			goto ret;
		}
		if (!(*ibuf = noterm_key_event_timeout(timeout_ms))) {
			err:
			*ibuf = 0;
		}
		ret:
		ibuf_cnt = 1;
		ibuf_pos = 0;
	}
	if (icmd_pos < sizeof(icmd))
		icmd[icmd_pos++] = ibuf[ibuf_pos];
	return ibuf[ibuf_pos++];
#endif
}
