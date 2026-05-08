static int ren_cwid(char *s, int pos)
{
	if (s[0] == '\t')
		return xts ? xts - (pos % xts) : 0;
	if (s[0] == '\n')
		return 1;
	int c, l; uc_code(c, s, l)
	for (int i = 0; i < phlen; i++)
		if (c >= ph[i].cp[0] && c <= ph[i].cp[1] && l == ph[i].l)
			return ph[i].wid;
	return uc_wid(c);
}

ren_state rstates[3]; /* 0 = current line, 1 = all other lines, 2 = aux rendering */
ren_state *rstate = rstates;

/* specify the screen position of the characters in s */
ren_state *ren_position(char *s)
{
	if (rstate->s == s)
		return rstate;
	else if (rstate->col) {
		free(rstate->col - 2);
		free(rstate->pos);
	}
	rstate->s = s;
	unsigned int n, max, l;
	char *ss = s;
	if (xlim >= 0 && rstate == rstates+1) {
		max = (unsigned int)xlim;
		for (n = 0; n < max && (l = uc_len(ss)); n++)
			ss += l;
		rstate->holelen = uc_len(ss);
		memcpy(rstate->nullhole, ss, rstate->holelen);
		memset(ss, 0, rstate->holelen);
	} else
		for (n = 0; (l = uc_len(ss)); n++)
			ss += l;
	unsigned int b = n + 1, c = 2, i;
	int cpos = 0, wid, *col;
	int *pos = emalloc((b * 2 * sizeof(pos[0])) + b * sizeof(char*));
	int *off = &pos[b];
	char **chrs = (char**)&off[b];
	for (i = 0; i < n; i++) {
		chrs[i] = s;
		pos[i] = cpos;
		cpos += ren_cwid(s, cpos);
		s += uc_len(s);
	}
	chrs[n] = s;
	pos[n] = cpos;
	col = emalloc((cpos + 2) * sizeof(col[0]));
	for (i = 0; i < n; i++) {
		wid = pos[i+1] - pos[i];
		off[i] = wid;
		while (wid--)
			col[c++] = i;
	}
	off[n] = 0;
	col[0] = n;
	col[1] = n;
	rstate->wid = off;
	rstate->cmax = cpos - 1;
	rstate->col = col + 2;
	rstate->pos = pos;
	rstate->chrs = chrs;
	rstate->n = n;
	return rstate;
}

/* convert character offset to visual position */
int ren_pos(char *s, int off)
{
	ren_state *r = ren_position(s);
	return off < r->n ? r->pos[off] : 0;
}

/* convert visual position to character offset */
int ren_off(char *s, int p)
{
	ren_state *r = ren_position(s);
	return r->col[p < r->cmax ? p : r->cmax];
}

/* adjust cursor position */
int ren_cursor(char *s, int p)
{
	if (!s)
		return 0;
	ren_state *r = ren_position(s);
	if (p >= r->cmax)
		p = r->cmax - (*r->chrs[r->col[r->cmax]] == '\n');
	int i = r->col[p];
	return r->pos[i] + r->wid[i] - 1;
}

/* return an offset before EOL */
int ren_noeol(char *s, int o)
{
	if (!s)
		return 0;
	ren_state *r = ren_position(s);
	o = MAX(0, o >= r->n ? r->n - 1 : o);
	return o - (o > 0 && *r->chrs[o] == '\n');
}

/* the visual position of the next character */
int ren_next(char *s, int p, int dir)
{
	ren_state *r = ren_position(s);
	if (p+dir < 0 || p > r->cmax)
		return r->pos[r->col[r->cmax]];
	int i = r->col[p];
	if (r->wid[i] > 1 && dir > 0)
		return r->pos[i] + r->wid[i];
	return r->pos[i] + dir;
}

char *ren_translate(char *s, char *ln)
{
	if (s[0] == '\t' || s[0] == '\n')
		return NULL;
	int c, l; uc_code(c, s, l)
	for (int i = 0; i < phlen; i++)
		if (c >= ph[i].cp[0] && c <= ph[i].cp[1] && l == ph[i].l)
			return ph[i].d;
	if (l == 1)
		return NULL;
	if (uc_acomb(c)) {
		static char buf[16] = "ـ";
		*((char*)memcpy(buf+2, s, l)+l) = '\0';
		return buf;
	}
	if (uc_isbell(c))
		return "�";
	return NULL;
}
