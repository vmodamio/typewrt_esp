static sbuf *suggestsb;
static sbuf *acsb;
static char *led_sels;
static int led_selbeg, led_selend;
static int led_pcols;

int dstrlen(const char *s, char delim)
{
	register const char *i;
	for (i=s; *i && *i != delim; ++i);
	return i-s;
}

static int search(const char *pattern, int l)
{
	if (!*pattern)
		return 0;
	sbuf_cut(suggestsb, 0)
	sbuf_smake(sylsb, 1024)
	char *part = strstr(acsb->s, pattern);
	while (part) {
		char *part1 = part;
		while (*part != '\n')
			part--;
		int len = dstrlen(++part, '\n');
		if (len++ != l) {
			if (part == part1)
				sbuf_mem(suggestsb, part, len)
			else
				sbuf_mem(sylsb, part, len)
		}
		part = strstr(part+len, pattern);
	}
	sbuf_mem(suggestsb, sylsb->s, sylsb->s_n)
	free(sylsb->s);
	sbufn_null(suggestsb)
	return suggestsb->s_n;
}

static void file_index(struct lbuf *buf)
{
	char reg[] = "[^\t !-/:-@[-\\]^`{-\x7f]+";
	int len, sidx, grp = xgrp;
	char **ss = buf->ln;
	int ln_n = lbuf_len(buf), n;
	rset *rs = rset_smake(xacreg ? xacreg->s : reg,
		xic ? REG_ICASE | REG_NEWLINE : REG_NEWLINE);
	if (!rs)
		return;
	int subs[rs->nsubc];
	sbuf_smake(ibuf, 1024)
	for (n = 1; n <= acsb->s_n; n++)
		if (acsb->s[n - 1] == '\n')
			sbuf_mem(ibuf, &n, (int)sizeof(n))
	for (int i = 0; i < ln_n; i++) {
		sidx = 0;
		while (rset_find(rs, ss[i]+sidx, subs, sidx ? REG_NOTBOL : 0) >= 0) {
			/* if target group not found, continue with group 1
			which will always be valid, otherwise there be no match */
			if (subs[grp] < 0) {
				sidx += subs[1] > 0 ? subs[1] : 1;
				continue;
			}
			len = subs[grp + 1] - subs[grp];
			if (len > 1) {
				char *part = ss[i]+sidx+subs[grp];
				int *ip = (int*)(ibuf->s+sizeof(n));
				for (n = len+1; ip < (int*)&ibuf->s[ibuf->s_n]; ip++)
					if (*ip - ip[-1] == n &&
						!memcmp(acsb->s + ip[-1], part, len))
							goto skip;
				sbuf_mem(acsb, part, len)
				sbuf_chr(acsb, '\n')
				sbuf_mem(ibuf, &acsb->s_n, (int)sizeof(n))
			}
			skip:
			sidx += subs[grp + 1] > 0 ? subs[grp + 1] : 1;
		}
	}
	sbuf_null(acsb)
	free(ibuf->s);
	rset_free(rs);
}

static char *kmap_map(int kmap, int c)
{
	static char cs[4];
	char **keymap = conf_kmap(kmap);
	if (c < 0 || c >= 256)
		return NULL;
	cs[0] = c;
	return keymap[c] ? keymap[c] : cs;
}

struct deadkey {
	int dead;
	char *base;
	char *composed;
};

static char *deadkey_spacing(int dead)
{
	switch (dead) {
	case '\'': return "´";
	case '`': return "`";
	case '^': return "^";
	case '"': return "¨";
	case '~': return "~";
	case ',': return "¸";
	}
	return "";
}

static int deadkey_code(char *cs)
{
	return cs && cs[0] == '\001' ? (unsigned char)cs[1] : 0;
}

static char *deadkey_compose(int dead, char *cs)
{
	static char buf[16];
	static struct deadkey table[] = {
		{'\'', "a", "á"}, {'\'', "A", "Á"},
		{'\'', "e", "é"}, {'\'', "E", "É"},
		{'\'', "i", "í"}, {'\'', "I", "Í"},
		{'\'', "o", "ó"}, {'\'', "O", "Ó"},
		{'\'', "u", "ú"}, {'\'', "U", "Ú"},
		{'\'', "y", "ý"}, {'\'', "Y", "Ý"},
		{'`', "a", "à"}, {'`', "A", "À"},
		{'`', "e", "è"}, {'`', "E", "È"},
		{'`', "i", "ì"}, {'`', "I", "Ì"},
		{'`', "o", "ò"}, {'`', "O", "Ò"},
		{'`', "u", "ù"}, {'`', "U", "Ù"},
		{'^', "a", "â"}, {'^', "A", "Â"},
		{'^', "e", "ê"}, {'^', "E", "Ê"},
		{'^', "i", "î"}, {'^', "I", "Î"},
		{'^', "o", "ô"}, {'^', "O", "Ô"},
		{'^', "u", "û"}, {'^', "U", "Û"},
		{'"', "a", "ä"}, {'"', "A", "Ä"},
		{'"', "e", "ë"}, {'"', "E", "Ë"},
		{'"', "i", "ï"}, {'"', "I", "Ï"},
		{'"', "o", "ö"}, {'"', "O", "Ö"},
		{'"', "u", "ü"}, {'"', "U", "Ü"},
		{'"', "y", "ÿ"}, {'"', "Y", "Ÿ"},
		{'~', "a", "ã"}, {'~', "A", "Ã"},
		{'~', "n", "ñ"}, {'~', "N", "Ñ"},
		{'~', "o", "õ"}, {'~', "O", "Õ"},
		{',', "c", "ç"}, {',', "C", "Ç"},
	};
	if (cs && cs[0] == ' ' && !cs[1])
		return deadkey_spacing(dead);
	for (int i = 0; i < LEN(table); i++)
		if (table[i].dead == dead && cs && !strcmp(table[i].base, cs))
			return table[i].composed;
	snprintf(buf, sizeof(buf), "%s%s", deadkey_spacing(dead), cs ? cs : "");
	return buf;
}

/* map cursor horizontal position to terminal column number */
int led_pos(char *s, int pos)
{
	return pos - xleft;
}

void led_prompt_width(int width)
{
	led_pcols = width;
}

void led_select(char *s, int beg, int end)
{
	led_sels = s;
	led_selbeg = beg;
	led_selend = end;
}

#define print_ch1(out) sbuf_mem(out, chrs[o], l)
#define print_ch2(out) sbuf_mem(out, *chrs[o] == ' ' ? "_" : chrs[o], l)

#define hid_ch1(out) sbuf_set(out, ' ', i - l)
#define hid_ch2(out) \
sbuf_set(out, *chrs[o] == '\n' ? '\\' : '-', i - l) \
if (*chrs[o] == '\t') \
	out->s[out->s_n-1] = '>'; \

#define led_out(out, n) \
{ \
for (i = 0; i < cterm;) { \
	o = off[i]; \
	if (o >= 0) { \
		for (l = i; off[i] == o; i++); \
		int sel = s0 == led_sels && o >= led_selbeg && o <= led_selend; \
		if (sel && !rev) { \
			sbuf_str(out, "\033[7m") \
			rev = 1; \
		} else if (!sel && rev) { \
			sbuf_str(out, "\033[27m") \
			rev = 0; \
		} \
		char *s = ren_translate(chrs[o], s0); \
		if (s) \
			sbuf_str(out, s) \
		else if (uc_isprint(chrs[o])) { \
			l = uc_len(chrs[o]); \
			print_ch##n(out) \
		} else { \
			hid_ch##n(out) \
		} \
	} else { \
		if (rev) { \
			sbuf_str(out, "\033[27m") \
			rev = 0; \
		} \
		if (cbeg) { \
			sbuf_chr(out, ' ') \
			i++; \
		} else \
			break; \
	} \
} \
if (rev) \
	sbuf_str(out, "\033[27m") \
} \

/* render a line */
void led_render(char *s0, int cbeg, int cend)
{
	if (!xled)
		return;
	ren_state *r = ren_position(s0);
	int c, l, i, o, n = r->n, cterm = cend - cbeg, rev = 0;
	char **chrs = r->chrs;	/* chrs[i]: the i-th character in s0 */
	int off[cterm+1];	/* off[i]: the character at screen position i */
	off[cterm] = -1;
	for (c = cbeg; c < cend; c++)
		off[c - cbeg] = c <= r->cmax ? r->col[c] : -1;
	if (r->cmax > cterm || cbeg) {
		i = 0;
		o = off[i];
		if (o >= 0 && cbeg && r->pos[o] < cbeg)
			while (off[i] == o)
				off[i++] = -1;
		i = cterm-1;
		o = off[i];
		if (o >= 0 && r->cmax > cterm && r->pos[o] + r->wid[o] > cend)
			while (off[i] == o)
				off[i--] = -1;
	}
	/* generate term output */
	if (vi_hidch) {
		led_out(term_sbuf, 2)
	} else {
		led_out(term_sbuf, 1)
	}
	if (r->holelen) {
		memcpy(chrs[n], r->nullhole, r->holelen);
		r->holelen = 0;
	}
}

static int led_lastchar(char *s)
{
	char *r = *s ? strchr(s, '\0') : s;
	if (r != s)
		r = uc_beg(s, r - 1);
	return r - s;
}

static int led_lastword(char *s)
{
	char *r = *s ? uc_beg(s, strchr(s, '\0') - 1) : s;
	int kind;
	while (r > s && uc_isspace(r))
		r = uc_beg(s, r - 1);
	kind = r > s ? uc_kind(r) : 0;
	while (r > s && uc_kind(uc_beg(s, r - 1)) == kind)
		r = uc_beg(s, r - 1);
	return r - s;
}

static void led_printparts(sbuf *sb, int pre, int ps,
	char *post, int postn, int *poff)
{
	if (!xled) {
		sbufn_null(sb)
		return;
	}
	int dir, off, pos, psn = sb->s_n;
	sbuf_str(sb, post)
	sbufn_null(sb)
	/* XXX: O(n) insertion; recursive array data structure cannot be optimized.
	For correctness, rstate must be recomputed. */
	rstate += 2;
	rstate->s = NULL;
	ren_state *r = ren_position(sb->s + ps);
	off = r->n - postn;
	*poff = off;
	pos = ren_cursor(r->s, r->pos[MAX(0, off-1)]);
	if (off > 0) {
		int two = off > 1 && psn != pre;
		dir = r->pos[off-two] - r->pos[off-(two+1)];
		if (abs(dir) > r->wid[off-(two+1)])
			pos = ren_cursor(r->s, r->pos[off-two]);
		pos += dir < 0 ? -1 : 1;
	}
	int cols = led_pcols > 0 ? MIN(led_pcols, xcols) : xcols;
	if (pos > xleft + cols || pos < xleft)
		xleft = pos <= cols ? 0 : pos - cols / 2;
	int guard = led_pcols > 0 && pos == xleft + cols;
	led_crender(r->s, -1, 0, xleft, xleft + cols);
	term_pos(-1, led_pos(r->s, guard ? pos - 1 : pos));
	term_cursor(!guard);
	sbufn_cut(sb, psn)
	rstate -= 2;
}

#define LED_HARDWRAP	-2
#define LED_HARDUNWRAP	-3
#define LED_REFLOW	-4
#define LED_HARDSEP	-5
#define LED_SMARTKEY	-6
#define LED_WORD_DELETE_REENTER	-7

static int led_wrap_ps;
static int led_wrap_hidden_sep;
static int led_wrap_sep_pending;
static int led_wrap_sep_ps;

static int led_hardwrap_forced(char *ln)
{
	return HWBRK_IS(ln);
}

static int led_hardwrap_existing(int row)
{
	return led_hardwrap_forced(lbuf_get(xb, row)) ||
		led_hardwrap_forced(lbuf_get(xb, row + 1));
}

static int led_hardwrap_saved_sep(sbuf *sb, int ps, int crow)
{
	if (ps != HWBRK_LEN || sb->s_n < HWBRK_LEN)
		return 0;
	if (!led_hardwrap_forced(lbuf_get(xb, crow)) || !HWBRK_SEP(sb->s))
		return 0;
	memcpy(sb->s, HWBRK_NOSPACE, HWBRK_LEN);
	return 1;
}

static int led_hardwrap_indent_len(sbuf *sb, int ps)
{
	int len = 0;

	while (ps + len < sb->s_n &&
			(sb->s[ps + len] == ' ' || sb->s[ps + len] == '\t'))
		len++;
	return len;
}

static int led_hardwrap_insert(sbuf *sb, int ps, char **post, int *postn)
{
	int cur, n, cut = 0, br = -1, end, next, prebytes, skip, indent_len;
	char *tail, *indent = NULL;
	led_wrap_hidden_sep = 0;
	sbuf_null(sb)
	prebytes = sb->s_n - ps;
	indent_len = led_hardwrap_indent_len(sb, ps);
	sbuf_smake(tmp, prebytes + strlen(*post) + 1)
	sbuf_mem(tmp, sb->s + ps, prebytes)
	sbufn_str(tmp, *post)
	rstate += 2;
	rstate->s = NULL;
	ren_state *r = ren_position(tmp->s);
	n = r->n && *r->chrs[r->n - 1] == '\n' ? r->n - 1 : r->n;
	if (conf_hwwidth <= 0 || !n || r->pos[n] <= conf_hwwidth) {
		free(tmp->s);
		rstate -= 2;
		return 0;
	}
	for (cur = 0; cur < n && r->chrs[cur] - tmp->s < prebytes; cur++);
	if (!cur) {
		free(tmp->s);
		rstate -= 2;
		return 0;
	}
	while (cut < n && r->pos[cut] + r->wid[cut] <= conf_hwwidth)
		cut++;
	cut = MIN(cut, cur);
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
		end = next = br;
		if (uc_isspace(r->chrs[br - 1])) {
			end = br - 1;
			while (next < n && uc_isspace(r->chrs[next]))
				next++;
		}
	} else
		end = next = cur;
	if (end <= 0)
		end = next = cur;
	led_wrap_hidden_sep = next > end;
	end = r->chrs[end] - tmp->s;
	next = r->chrs[next] - tmp->s;
	skip = MAX(0, next - prebytes);
	if (skip) {
		*postn -= uc_off(*post, skip);
		if (*postn < 0)
			*postn = 0;
		*post += skip;
	}
	end = MIN(end, prebytes);
	if (indent_len) {
		indent = emalloc(indent_len);
		memcpy(indent, sb->s + ps, indent_len);
	}
	tail = next < prebytes ? uc_dup(sb->s + ps + next) : uc_dup("");
	sbuf_cut(sb, ps + end)
	sbuf_chr(sb, '\n')
	sbuf_str(sb, led_wrap_hidden_sep ? HWBRK : HWBRK_NOSPACE)
	if (indent_len)
		sbuf_mem(sb, indent, indent_len)
	sbuf_str(sb, tail)
	free(indent);
	free(tail);
	free(tmp->s);
	rstate -= 2;
	return 1;
}

static int led_hardwrap_unwrap(sbuf *sb, int ps, char *post)
{
	int brk, prev, sep, width;
	char *tail;
	led_wrap_hidden_sep = 0;
	if (conf_hwwidth <= 0 || ps < HWBRK_LEN + 1)
		return 0;
	brk = ps - HWBRK_LEN - 1;
	if (sb->s[brk] != '\n' || !HWBRK_IS(sb->s + brk + 1))
		return 0;
	for (prev = brk; prev > 0 && sb->s[prev - 1] != '\n'; prev--);
	sep = HWBRK_SEP(sb->s + brk + 1) && brk > prev &&
		sb->s[brk - 1] != ' ' && (ps < sb->s_n || *post);
	sbuf_smake(tmp, sb->s_n - prev + strlen(post) + 2)
	sbuf_mem(tmp, sb->s + prev, brk - prev)
	if (sep)
		sbuf_chr(tmp, ' ')
	sbuf_mem(tmp, sb->s + ps, sb->s_n - ps)
	sbufn_str(tmp, post)
	rstate += 2;
	rstate->s = NULL;
	ren_state *r = ren_position(tmp->s);
	int n = r->n && *r->chrs[r->n - 1] == '\n' ? r->n - 1 : r->n;
	width = r->pos[n];
	free(tmp->s);
	rstate -= 2;
	if (width > conf_hwwidth)
		return 0;
	led_wrap_hidden_sep = sep;
	tail = uc_dup(sb->s + ps);
	sbuf_cut(sb, brk)
	if (sep)
		sbuf_chr(sb, ' ')
	sbuf_str(sb, tail)
	sbuf_null(sb)
	free(tail);
	led_wrap_ps = prev;
	return 1;
}

static int led_hardwrap_join_at(sbuf *sb, int prev, int ps, char *post,
	int cut)
{
	int width;
	char *tail;
	sbuf_smake(tmp, sb->s_n - prev + strlen(post) + 1)
	sbuf_mem(tmp, sb->s + prev, cut - prev)
	sbuf_mem(tmp, sb->s + ps, sb->s_n - ps)
	sbufn_str(tmp, post)
	rstate += 2;
	rstate->s = NULL;
	ren_state *r = ren_position(tmp->s);
	int n = r->n && *r->chrs[r->n - 1] == '\n' ? r->n - 1 : r->n;
	width = r->pos[n];
	free(tmp->s);
	rstate -= 2;
	if (width > conf_hwwidth)
		return 0;
	tail = uc_dup(sb->s + ps);
	sbuf_cut(sb, cut)
	sbuf_str(sb, tail)
	sbuf_null(sb)
	free(tail);
	led_wrap_ps = prev;
	return 1;
}

static int led_hardwrap_backspace(sbuf *sb, int ps, char *post)
{
	int brk, prev, cut;
	led_wrap_hidden_sep = 0;
	if (conf_hwwidth <= 0 || ps < HWBRK_LEN + 1)
		return 0;
	brk = ps - HWBRK_LEN - 1;
	if (sb->s[brk] != '\n' || !HWBRK_IS(sb->s + brk + 1))
		return 0;
	for (prev = brk; prev > 0 && sb->s[prev - 1] != '\n'; prev--);
	if (brk > prev && sb->s[brk - 1] != ' ' &&
			(ps < sb->s_n || *post) &&
			led_hardwrap_join_at(sb, prev, ps, post, brk))
		return 1;
	if (brk <= prev)
		return 0;
	cut = uc_beg(sb->s + prev, sb->s + brk - 1) - sb->s;
	return led_hardwrap_join_at(sb, prev, ps, post, cut);
}

static int led_hardwrap_reflow_after_delete(sbuf *sb, int ps, char *post,
	int crow)
{
	char *cur = lbuf_get(xb, crow);
	char *prev = lbuf_get(xb, crow - 1);
	int prev_body, prev_n, cur_n, sep, width, cur_width, prev_last_space;
	ren_state *r;

	if (!led_hardwrap_forced(cur))
		return 1;
	if (!prev)
		return 1;
	if (ps < 0 || ps > sb->s_n)
		return 1;
	prev_body = led_hardwrap_forced(prev) ? HWBRK_LEN : 0;
	rstate += 2;
	rstate->s = NULL;
	r = ren_position(prev + prev_body);
	prev_n = r->n && *r->chrs[r->n - 1] == '\n' ? r->n - 1 : r->n;
	width = r->pos[prev_n];
	prev_last_space = prev_n && uc_isspace(r->chrs[prev_n - 1]);
	sbuf_smake(tmp, sb->s_n - ps + strlen(post) + 1)
	sbuf_mem(tmp, sb->s + ps, sb->s_n - ps)
	sbufn_str(tmp, post)
	rstate->s = NULL;
	r = ren_position(tmp->s);
	cur_n = r->n && *r->chrs[r->n - 1] == '\n' ? r->n - 1 : r->n;
	cur_width = r->pos[cur_n];
	if (!cur_n && HWBRK_SEP(cur)) {
		free(tmp->s);
		rstate -= 2;
		return 0;
	}
	sep = HWBRK_SEP(cur) && prev_n && cur_n &&
		!uc_isspace(r->chrs[0]) && !prev_last_space;
	width += sep + cur_width;
	free(tmp->s);
	rstate -= 2;
	return !cur_n || width <= conf_hwwidth;
}

static int led_hardwrap_after_delete(sbuf *sb, int ps, int pre, char *post,
	int postn, int *poff, int ai_max, int crow)
{
	if (ai_max < 0)
		return 0;
	if (sb->s_n == ps) {
		sbuf_null(sb)
		if (ps == HWBRK_LEN && led_hardwrap_forced(lbuf_get(xb, crow))) {
			if (HWBRK_SEP(sb->s))
				return 0;
			led_printparts(sb, pre, ps, post, postn, poff);
			return LED_REFLOW;
		}
		if (led_wrap_sep_pending && ps == led_wrap_sep_ps) {
			memcpy(sb->s + ps - HWBRK_LEN, HWBRK_NOSPACE, HWBRK_LEN);
			led_wrap_sep_pending = 0;
			led_wrap_sep_ps = -1;
			return LED_HARDSEP;
		}
		if (led_hardwrap_backspace(sb, ps, post))
			return LED_HARDUNWRAP;
	}
	if (led_hardwrap_existing(crow)) {
		if (!led_hardwrap_reflow_after_delete(sb, ps, post, crow))
			return 0;
		led_printparts(sb, pre, ps, post, postn, poff);
		return LED_REFLOW;
	}
	return 0;
}

/* read a character from the terminal */
char *led_read(int *kmap, int c)
{
	static char buf[16];
	static int dead;
	char *cs;
	int c1, c2, i, n;
	while (!TK_INT(c)) {
		switch (c) {
		case TK_CTL('f'):
			*kmap = xkmap_alt;
			dead = 0;
			break;
		case TK_CTL('e'):
			*kmap = 0;
			dead = 0;
			break;
		case TK_CTL('v'):	/* literal character */
			buf[0] = term_read(0);
			buf[1] = '\0';
			dead = 0;
			return buf;
		case TK_CTL('k'):	/* digraph */
			c1 = term_read(0);
			if (TK_INT(c1))
				return NULL;
			c2 = term_read(0);
			if (TK_INT(c2))
				return NULL;
			dead = 0;
			return conf_digraph(c1, c2);
		default:
			if ((c & 0xc0) == 0xc0) {	/* utf-8 character */
				buf[0] = c;
				n = uc_len(buf);
				for (i = 1; i < n; i++)
					buf[i] = term_read(0);
				buf[n] = '\0';
				cs = buf;
			} else
				cs = kmap_map(*kmap, c);
			if ((c1 = deadkey_code(cs))) {
				if (dead == c1) {
					dead = 0;
					return deadkey_spacing(c1);
				}
				dead = c1;
				break;
			}
			if (dead) {
				c1 = dead;
				dead = 0;
				return deadkey_compose(c1, cs);
			}
			return cs;
		}
		c = term_read(0);
	}
	dead = 0;
	return NULL;
}

#define led_info(buf) \
{ \
	sbuf_str(sb, buf) \
	led_printparts(sb, pre, ps, *post, *postn, poff); \
	sbuf_cut(sb, len) \
	c = term_read(TK_CTL('l')); \
	led_printparts(sb, pre, ps, *post, *postn, poff); \
	goto noredraw; \
} \

static void led_redraw(char *cs, int r, int orow, int crow, int ctop, int flg)
{
	rstate++;
	for (int nl = 0; r < xrows; r++) {
		if (r >= orow-ctop && r < crow-ctop) {
			sbuf_smake(cb, 128)
			nl = dstrlen(cs, '\n');
			sbuf_mem(cb, cs, nl+!!cs[nl])
			sbufn_null(cb)
			rstate->s = NULL;
			led_crender(cb->s, r, 0, xleft, xleft + xcols)
			free(cb->s);
			cs += nl+!!cs[nl];
			continue;
		}
		nl = r < crow-ctop ? r+ctop : (r-(crow-orow+!!(flg & 4)))+ctop;
		led_crender(lbuf_get(xb, nl) ? lbuf_get(xb, nl) : "~", r,
			0, xleft, xleft + xcols)
	}
	term_pos(crow - ctop, 0);
	rstate--;
}

void led_modeswap(void)
{
	preserve(int, xquit, xquit = 0;)
	preserve(int, texec, if (texec == '@') texec = 0;)
	preserve(int, xvis, xvis ^= 2;)
	preserve(int, xexec_dep, xexec_dep = 0;)
	if (xvis & 2)
		ex();
	else {
		vi(1);
	}
	if (xquit > 0)
		restore(xquit)
	restore(texec)
	restore(xvis)
	restore(xexec_dep)
}

/* read a line from the terminal */
static int led_line(sbuf *sb, int ps, int pre, char **post, int *postn, char **postref,
	int ai_max, int *poff, int *kmap, ins_state *is, int orow, int crow, int ctop, int flg)
{
	char *cs;
	int len, c, i, hkey;
	do {
		led_printparts(sb, pre, ps, *post, *postn, poff);
		len = sb->s_n;
		int queued = ibuf_pos < ibuf_cnt;
		c = term_read(TK_CTL('l'));
		if (!queued && ai_max >= 0 && TK_SMART_ANY(c)) {
			if (icmd_pos)
				icmd_pos--;
			if (icmd_pos < sizeof(icmd))
				icmd[icmd_pos++] = TK_ESC;
			term_cursor(0);
			return LED_SMARTKEY;
		}
		noredraw:
		switch (c) {
		case TK_CTL('h'):
		case 127:
			c = 127;
			if (len - pre > 0) {
				sbuf_cut(sb, led_lastchar(sb->s + pre) + pre)
				if ((hkey = led_hardwrap_after_delete(sb, ps, pre,
						*post, *postn, poff, ai_max, crow)))
					return hkey;
			}
			else if (ai_max >= 0) {
				sbuf_null(sb)
				if (pre == ps && led_wrap_sep_pending &&
						ps == led_wrap_sep_ps) {
					memcpy(sb->s + ps - HWBRK_LEN, HWBRK_NOSPACE,
						HWBRK_LEN);
					led_wrap_sep_pending = 0;
					led_wrap_sep_ps = -1;
					return LED_HARDSEP;
				}
				if (pre == ps && led_hardwrap_saved_sep(sb, ps, crow))
					return LED_REFLOW;
				if (pre == ps && ps == HWBRK_LEN &&
						led_hardwrap_forced(lbuf_get(xb, crow)))
					return 127;
				if (pre == ps && led_hardwrap_backspace(sb, ps, *post))
					return LED_HARDUNWRAP;
				term_cursor(0);
				return c;
			}
			else {
				term_cursor(0);
				return c;
			}
			break;
		case TK_CTL('u'):
			sbuf_cut(sb, is->sug_pt > pre && len > is->sug_pt ? is->sug_pt : pre)
			if ((hkey = led_hardwrap_after_delete(sb, ps, pre, *post,
					*postn, poff, ai_max, crow)))
				return hkey;
			break;
		case TK_CTL('w'):
			if (len - pre > 0) {
				sbuf_cut(sb, led_lastword(sb->s + pre) + pre)
				if ((hkey = led_hardwrap_after_delete(sb, ps, pre,
						*post, *postn, poff, ai_max, crow)))
					return hkey;
			}
			else if (ai_max >= 0)
				return LED_WORD_DELETE_REENTER;
			break;
		case TK_CTL('t'):
			cs = uc_dup(sb->s + ps);
			sbuf_cut(sb, ps)
			sbuf_chr(sb, '\t')
			sbuf_str(sb, cs)
			free(cs);
			pre++;
			break;
		case TK_CTL('d'):
			if (sb->s[ps] == ' ' || sb->s[ps] == '\t') {
				sbuf_cut(sb, ps)
				sbuf_str(sb, sb->s+ps+1)
				pre--;
			}
			break;
		case TK_CTL(']'):
		case TK_CTL('\\'):
			if (c == TK_CTL(']')) {
				if (is->p_reg < '/' || is->p_reg >= '9')
					is->p_reg = '/';
				while (is->p_reg < '9' && !xregs[++is->p_reg]);
			} else {
				c = term_read(0);
				is->p_reg = c == TK_CTL('\\') ? 0 : c;
			}
			if (xregs[is->p_reg])
				led_info(xregs[is->p_reg]->s)
			continue;
		case TK_CTL('p'):
			if (xregs[is->p_reg])
				sbuf_mem(sb, xregs[is->p_reg]->s, xregs[is->p_reg]->s_n)
			break;
		case TK_CTL('g'):
			if (!suggestsb) {
				sbuf_make(suggestsb, 1)
				sbuf_make(acsb, 1024)
				sbufn_chr(acsb, '\n')
			}
			file_index(xb);
			break;
		case TK_CTL('y'):
			led_done();
			suggestsb = NULL;
			break;
		case TK_CTL('r'):
			if (!suggestsb || !suggestsb->s_n)
				continue;
			if (!is->sug)
				is->sug = suggestsb->s;
			if (suggestsb->s_n == is->sug - suggestsb->s)
				is->sug--;
			for (i = 0; is->sug != suggestsb->s; is->sug--) {
				if (!*is->sug) {
					i++;
					if (i == 3) {
						is->sug++;
						goto redo_suggest;
					} else
						*is->sug = '\n';
				}
			}
			goto redo_suggest;
		case TK_CTL('x'):
			is->sug_pt = is->sug_pt == len ? -1 : len;
			char buf[100];
			nextvi_itoa(is->sug_pt, buf);
			led_info(buf)
		case TK_CTL('n'):
			if (!suggestsb)
				continue;
			is->lsug = is->sug_pt >= 0 ? is->sug_pt : led_lastword(sb->s + pre) + pre;
			if (is->_sug) {
				if (suggestsb->s_n == is->sug - suggestsb->s)
					continue;
				redo_suggest:
				if (!(is->_sug = strchr(is->sug, '\n'))) {
					is->sug = suggestsb->s;
					goto lookup;
				}
				suggest:
				*is->_sug = '\0';
				sbuf_cut(sb, is->lsug)
				sbuf_str(sb, is->sug)
				is->sug = is->_sug+1;
				continue;
			}
			lookup:
			if (search(sb->s + is->lsug, len - is->lsug)) {
				is->sug = suggestsb->s;
				if (!(is->_sug = strchr(is->sug, '\n')))
					continue;
				goto suggest;
			}
			continue;
		case TK_CTL('b'):
			if (ai_max >= 0) {
				pac:;
				sbuf_null(sb)
				int r = crow-ctop+1;
				if (is->sug)
					goto pac_;
				i = is->sug_pt >= 0 ? is->sug_pt : led_lastword(sb->s + pre) + pre;
				if (suggestsb && search(sb->s + i, sb->s_n - i)) {
					is->sug = suggestsb->s;
					pac_:;
					for (int left = 0; r < xrows; r++) {
						RS(2, led_crender(is->sug, r, 0, left, left+xcols))
						left += xcols;
						if (left >= rstates[2].pos[rstates[2].n])
							break;
					}
					r++;
				}
				led_redraw(sb->s, r, orow, crow, ctop, flg);
				continue;
			}
			temp_pos(0, -1, 0, 0);
			temp_write(0, sb->s + pre);
			preserve(struct buf*, ex_buf,)
			int bidx = istempbuf(ex_buf) ? -1 : ex_buf - bufs;
			int pidx = ex_pbuf - bufs;
			preserve(int, texec, if (texec == '@') texec = 0;)
			preserve(int, xquit, xquit = 0;)
			temp_switch(0, 0);
			vi(1);
			exbuf_save(ex_buf)
			restore(texec)
			ex_pbuf = pidx >= xbufcur ? bufs : bufs + pidx;
			if (bidx >= 0)
				ex_buf = bidx >= xbufcur ? bufs : bufs + bidx;
			else
				restore(ex_buf)
			exbuf_load(ex_buf)
			vi(1); /* redraw past screen */
			term_pos(xrows, 0);
			if (xquit > 0)
				restore(xquit)
			is->t_row = tempbufs[0].row;
			/* fall through */
		case TK_CTL('a'):
			is->t_row = is->t_row < -1 ? tempbufs[0].row : is->t_row;
			is->t_row += lbuf_len(tempbufs[0].lb);
			is->t_row = is->t_row % MAX(1, lbuf_len(tempbufs[0].lb));
			if ((cs = lbuf_get(tempbufs[0].lb, is->t_row--))) {
				sbuf_cut(sb, pre)
				sbuf_str(sb, cs)
				sb->s_n--;
			}
			break;
		case TK_CTL('l'):
			i = term_winch;
			term_done();
			term_init();
			if (ai_max >= 0)
				led_redraw(sb->s, 0, orow, crow, ctop, flg);
			else if (!i)
				term_clean();
			continue;
		case TK_CTL('o'): {
			if (!*postref)
				*postref = *post = uc_dup(*post);
			preserve(struct buf*, ex_buf,)
			int bidx = istempbuf(ex_buf) ? -1 : ex_buf - bufs;
			led_modeswap();
			if (bidx < 0) {
				if (ex_buf == tmpex_buf)
					continue;
				restore(ex_buf)
				exbuf_load(ex_buf)
			} else if (bidx != ex_buf - bufs && bidx < xbufcur) {
				ex_buf = bufs + bidx;
				exbuf_load(ex_buf)
			}
			continue; }
		default:
			if (TK_SMART_ANY(c))
				continue;
			if (TK_INT(c))
				return c;
			if (c == '\n')
				return c;
			if ((cs = led_read(kmap, c))) {
				nextvi_display_note_insert();
				sbuf_str(sb, cs)
			}
		}
		if (ai_max >= 0 && led_hardwrap_insert(sb, ps, post, postn))
			return LED_HARDWRAP;
		if (ai_max >= 0 && led_hardwrap_unwrap(sb, ps, *post))
			return LED_HARDUNWRAP;
		is->sug = NULL;
		is->_sug = NULL;
		if (ai_max >= 0 && xpac)
			goto pac;
	} while (!(flg & 2));
	return c;
}

int led_prompt(sbuf *sb, char *insert, int *kmap, ins_state *is, int ps, int flg)
{
	int n = !(flg & 2) ? sb->s_n : 0, key, off, postn = 0;
	char *post = "", *postref = post;
	ins_state _is;
	if (insert)
		sbuf_str(sb, insert)
	if (!is) {
		ins_init(_is)
		is = &_is;
	}
	preserve(int, xleft, xleft = 0;)
	key = led_line(sb, ps, n, &post, &postn, &postref, -1,
			&off, kmap, is, 0, xrow, xtop, flg);
	if (key == TK_MENU)
		xquit = !xquit ? 1 : xquit;
	restore(xleft)
	if (key == '\n' && flg & 1) {
		lbuf_dedup(tempbufs[0].lb, sb->s + n, sb->s_n - n)
		temp_pos(0, -1, 0, 0);
		temp_write(0, sb->s + n);
	}
	return key;
}

static void led_open_next_row(int *crow, int *ctop)
{
	if (*crow - *ctop >= xrows - 1) {
		(*ctop)++;
		term_pos(0, 0);
		term_room(-1);
		term_pos(*crow - *ctop + 1, 0);
		term_kill();
	} else {
		term_chr('\n');
		term_room(1);
	}
	(*crow)++;
}

static int led_input_at(sbuf *sb, char *post, int postn, int row, int flg,
	int *pren, int ips, int icrow, int ipre)
{
	int ai_max = 128 * xai;
	int n, key, ps = ips, pre = ipre >= 0 ? MAX(ipre, ips) : -1;
	int crow = icrow, ctop = xtop;
	char *postref = NULL;
	ins_state is;
	led_pcols = conf_hwwidth > 0 ? conf_hwwidth : 0;
	led_wrap_sep_pending = 0;
	led_wrap_sep_ps = -1;
	while (1) {
		if (pre < ps)
			pre = sb->s_n;
		ins_init(is)
		key = led_line(sb, ps, pre, &post, &postn, &postref,
			ai_max, &xoff, &xkmap, &is, row, crow, ctop, flg);
		if (key == LED_HARDWRAP) {
			char *nl = strchr(sb->s + ps, '\n');
			int nllen;
			if (!nl)
				continue;
			term_cursor_suspend(1);
			nllen = nl - sb->s;
			sbuf_smake(tmp, nllen + 1)
			sbuf_mem(tmp, sb->s, nllen)
			led_printparts(tmp, -1, ps, "", 0, &xoff);
			free(tmp->s);
			led_open_next_row(&crow, &ctop);
			ps = nl + 1 + HWBRK_LEN - sb->s;
			pre = ps;
			led_wrap_sep_pending = led_wrap_hidden_sep;
			led_wrap_sep_ps = led_wrap_hidden_sep ? ps : -1;
			term_cursor_suspend(0);
			if (!led_wrap_hidden_sep)
				term_cursor(1);
			continue;
		}
		if (key == LED_HARDSEP)
			continue;
		if (key == LED_HARDUNWRAP) {
			int wraprow = MAX(1, crow - ctop);
			led_wrap_sep_pending = 0;
			led_wrap_sep_ps = -1;
			term_cursor_suspend(1);
			term_pos(wraprow, 0);
			term_room(-1);
			crow--;
			ps = led_wrap_ps;
			pre = ps;
			term_pos(crow - ctop, 0);
			led_printparts(sb, -1, ps, post, postn, &xoff);
			term_cursor_suspend(0);
			if (!led_wrap_hidden_sep)
				term_cursor(1);
			continue;
		}
		if (key != '\n') {
			*pren = sb->s_n;
			if (!xled) {
				xoff = uc_slen(sb->s+ps);
				sbufn_str(sb, post)
			} else
				sb->s[*pren] = *post;
			if (ps >= HWBRK_LEN && HWBRK_IS(sb->s + ps - HWBRK_LEN))
				xoff++;
			free(postref);
			xrow = crow;
			led_pcols = 0;
			if (key != 127)
				term_cursor(1);
			if (key == TK_MENU)
				xquit = !xquit ? 1 : xquit;
			return key;
		}
		sbuf_chr(sb, key)
		led_wrap_sep_pending = 0;
		led_wrap_sep_ps = -1;
		term_cursor_suspend(1);
		led_printparts(sb, -1, ps, "", 0, &xoff);
		led_open_next_row(&crow, &ctop);
		n = ps;
		ps = sb->s_n;
		pre = sb->s_n;
		if (ai_max) {	/* updating autoindent */
			for (; *post == ' ' || *post == '\t'; postn--)
				++post;
			int ai_new = n;
			while (sb->s[ai_new] == ' ' || sb->s[ai_new] == '\t')
				ai_new++;
			ai_new = ai_max > ai_new - n ? ai_new - n : ai_max;
			sbuf_mem(sb, sb->s+n, ai_new)
		}
		term_cursor_suspend(0);
		term_cursor(1);
	}
}

int led_input(sbuf *sb, char *post, int postn, int row, int flg, int *pren)
{
	return led_input_at(sb, post, postn, row, flg, pren, 0, xrow, -1);
}

void led_done(void)
{
	if (suggestsb) {
		sbuf_free(suggestsb)
		sbuf_free(acsb)
	}
}
