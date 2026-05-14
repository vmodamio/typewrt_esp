#ifdef NEXTVI_EMBEDDED

#include <ctype.h>
#include <errno.h>
#include <time.h>

#define MENU_TOP_ROW		0
#define MENU_SPACER_ROW		1
#define MENU_FIRST_ROW		2
#define MENU_BOTTOM_ROW		NEXTVI_DISPLAY_ROWS
#define MENU_VISIBLE_ROWS	(NEXTVI_DISPLAY_ROWS - 2)
#define MENU_FS_ROOT		NEXTVI_FS_ROOT
#define MENU_SYNC_MAX		64
#define MENU_DIR_STATE_MAX	32
#define MENU_SIZE_COL_WIDTH	8
#define MENU_DATE_COL_WIDTH	10
#define MENU_META_GAP		1
#define MENU_STATUS_WAKE_MS	1000

typedef enum {
	MENU_SORT_NAME = 0,
	MENU_SORT_SIZE,
	MENU_SORT_MTIME,
} menu_sort;

typedef struct {
	char *name;
	char *path;
	long size;
	long mtime;
	long words;
	int is_dir;
	int synced;
} menu_entry;

typedef struct {
	char *path;
	long size;
	long mtime;
} menu_sync;

typedef struct {
	char *path;
	int cursor;
	int top;
	menu_sort sort;
	int reverse;
	char filter[64];
} menu_dir_state;

typedef struct {
	menu_entry *entry;
	int count;
	int cap;
	int cursor;
	int top;
	menu_sort sort;
	int reverse;
	char filter[64];
	char message[80];
	int bottom_message_active;
	char *prev_path;
	int top_valid;
	time_t top_minute;
	int top_power_state;
	char top_line[NEXTVI_DISPLAY_COLS + 1];
} menu_state;

static menu_sync menu_syncs[MENU_SYNC_MAX];
static int menu_sync_count;
static int menu_sync_next;
static menu_dir_state menu_dir_states[MENU_DIR_STATE_MAX];
static int menu_dir_state_count;
static int menu_dir_state_next;

static void menu_line(char out[NEXTVI_DISPLAY_COLS + 1], const char *text)
{
	int i = 0;
	memset(out, ' ', NEXTVI_DISPLAY_COLS);
	out[NEXTVI_DISPLAY_COLS] = '\0';
	if (!text)
		return;
	for (; i < NEXTVI_DISPLAY_COLS && text[i]; i++)
		out[i] = text[i];
}

static void menu_draw_row(int row, const char *text, int inverted)
{
	char line[NEXTVI_DISPLAY_COLS + 1];
	menu_line(line, text);
	if (inverted)
		nextvi_display_refresh_line_inverted(row, line, NEXTVI_DISPLAY_COLS);
	else
		nextvi_display_refresh_line(row, line, NEXTVI_DISPLAY_COLS);
}

static char *menu_path_join(const char *dir, const char *name)
{
	int dlen;
	char *path;
	if (!name || !*name)
		return uc_dup(dir && *dir ? dir : "/");
	if (name[0] == '/')
		return uc_dup(name);
	dir = dir && *dir ? dir : "/";
	dlen = strlen(dir);
	path = emalloc(dlen + strlen(name) + 2);
	strcpy(path, dir);
	if (dlen && path[dlen - 1] != '/')
		path[dlen++] = '/';
	strcpy(path + dlen, name);
	return path;
}

static int menu_same_path(const char *a, const char *b)
{
	return a && b && !strcmp(a, b);
}

static menu_dir_state *menu_dir_state_find(const char *path)
{
	for (int i = 0; i < menu_dir_state_count; i++)
		if (menu_same_path(menu_dir_states[i].path, path))
			return &menu_dir_states[i];
	return NULL;
}

static void menu_dir_state_save(menu_state *m)
{
	menu_dir_state *s = menu_dir_state_find(ex_vcwd);

	if (!s) {
		if (menu_dir_state_count < MENU_DIR_STATE_MAX)
			s = &menu_dir_states[menu_dir_state_count++];
		else {
			s = &menu_dir_states[menu_dir_state_next++ %
				MENU_DIR_STATE_MAX];
			free(s->path);
		}
		s->path = uc_dup(ex_vcwd);
	}
	s->cursor = m->cursor;
	s->top = m->top;
	s->sort = m->sort;
	s->reverse = m->reverse;
	snprintf(s->filter, sizeof(s->filter), "%s", m->filter);
}

static int menu_dir_state_restore(menu_state *m, const char *path)
{
	menu_dir_state *s = menu_dir_state_find(path);

	if (!s)
		return 0;
	m->cursor = s->cursor;
	m->top = s->top;
	m->sort = s->sort;
	m->reverse = s->reverse;
	snprintf(m->filter, sizeof(m->filter), "%s", s->filter);
	return 1;
}

static int menu_root_path(const char *path)
{
	return path && (!strcmp(path, MENU_FS_ROOT) || !strcmp(path, "/"));
}

static int menu_under_root(const char *path)
{
	int root_len = strlen(MENU_FS_ROOT);
	return path && !strncmp(path, MENU_FS_ROOT, root_len) &&
		(path[root_len] == '\0' || path[root_len] == '/');
}

static int menu_parent_of(const char *parent, const char *child)
{
	const char *slash;
	int root_len = strlen(MENU_FS_ROOT);
	int parent_len;

	if (!parent || !child || !menu_under_root(parent) ||
			!menu_under_root(child) || !strcmp(parent, child))
		return 0;
	slash = strrchr(child, '/');
	if (!slash || slash < child + root_len)
		return 0;
	parent_len = slash - child;
	if (parent_len == root_len)
		return !strcmp(parent, MENU_FS_ROOT);
	return (int)strlen(parent) == parent_len &&
		!strncmp(parent, child, parent_len);
}

static void menu_visible_path(char out[NEXTVI_DISPLAY_COLS + 1])
{
	menu_line(out, ex_vcwd);
}

static char *menu_path_normalize(const char *path)
{
	char *input;
	char *out;
	const char *p;
	int root_len = strlen(MENU_FS_ROOT);
	int out_len;

	if (!path || !*path || !strcmp(path, "/"))
		input = uc_dup(MENU_FS_ROOT);
	else if (path[0] == '/') {
		if (menu_under_root(path))
			input = uc_dup(path);
		else
			input = menu_path_join(MENU_FS_ROOT, path + 1);
	} else
		input = menu_path_join(ex_vcwd, path);

	out = emalloc(strlen(input) + root_len + 2);
	strcpy(out, MENU_FS_ROOT);
	out_len = root_len;
	p = menu_under_root(input) ? input + root_len : input;
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
			if (out_len > root_len) {
				while (out_len > root_len && out[out_len - 1] != '/')
					out_len--;
				if (out_len > root_len)
					out_len--;
				out[out_len] = '\0';
			}
			continue;
		}
		out[out_len++] = '/';
		memcpy(out + out_len, seg, len);
		out_len += len;
		out[out_len] = '\0';
	}
	free(input);
	return out;
}

void nextvi_menu_mark_synced(const char *path)
{
	struct stat st;
	char *fspath;
	int idx = -1;

	if (!path || !*path)
		return;
	fspath = menu_path_normalize(path);
	if (stat(fspath, &st) || S_ISDIR(st.st_mode)) {
		free(fspath);
		return;
	}
	for (int i = 0; i < menu_sync_count; i++)
		if (menu_same_path(menu_syncs[i].path, fspath)) {
			idx = i;
			break;
		}
	if (idx < 0) {
		if (menu_sync_count < MENU_SYNC_MAX)
			idx = menu_sync_count++;
		else {
			idx = menu_sync_next++ % MENU_SYNC_MAX;
			free(menu_syncs[idx].path);
		}
		menu_syncs[idx].path = fspath;
	} else
		free(fspath);
	menu_syncs[idx].size = (long)st.st_size;
	menu_syncs[idx].mtime = (long)st.st_mtime;
}

static int menu_path_synced(const char *path, const struct stat *st)
{
	for (int i = 0; i < menu_sync_count; i++)
		if (menu_same_path(menu_syncs[i].path, path))
			return menu_syncs[i].size == (long)st->st_size &&
				menu_syncs[i].mtime == (long)st->st_mtime;
	return 0;
}

static int menu_glob_match(const char *pat, const char *text)
{
	if (!pat || !*pat)
		return 1;
	if (*pat == '*') {
		do {
			if (menu_glob_match(pat + 1, text))
				return 1;
		} while (*text++);
		return 0;
	}
	if (*pat == '?')
		return *text && menu_glob_match(pat + 1, text + 1);
	return *pat == *text && menu_glob_match(pat + 1, text + 1);
}

static long menu_count_words(const char *path)
{
	char buf[256];
	long words = 0;
	int in_word = 0;
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (ssize_t i = 0; i < n; i++) {
			if (isspace((unsigned char)buf[i]))
				in_word = 0;
			else if (!in_word) {
				words++;
				in_word = 1;
			}
		}
	}
	close(fd);
	return n < 0 ? -1 : words;
}

static void menu_entries_free(menu_state *m)
{
	for (int i = 0; i < m->count; i++) {
		free(m->entry[i].name);
		free(m->entry[i].path);
	}
	free(m->entry);
	m->entry = NULL;
	m->count = 0;
	m->cap = 0;
}

static void menu_entry_add(menu_state *m, const char *name, char *path,
	const struct stat *st)
{
	menu_entry *e;
	if (m->filter[0] && strcmp(name, "..") && !menu_glob_match(m->filter, name)) {
		free(path);
		return;
	}
	if (m->count == m->cap) {
		m->cap = m->cap ? m->cap * 2 : 32;
		m->entry = erealloc(m->entry, m->cap * sizeof(m->entry[0]));
	}
	e = &m->entry[m->count++];
	e->name = uc_dup(name);
	e->path = path;
	e->size = (long)st->st_size;
	e->mtime = (long)st->st_mtime;
	e->is_dir = S_ISDIR(st->st_mode);
	e->words = e->is_dir ? -1 : menu_count_words(path);
	e->synced = e->is_dir ? 0 : menu_path_synced(path, st);
}

static int menu_entry_cmp(const void *a, const void *b)
{
	const menu_entry *ea = a;
	const menu_entry *eb = b;
	int ret;
	if (ea->is_dir != eb->is_dir)
		return eb->is_dir - ea->is_dir;
	if (!strcmp(ea->name, ".."))
		return -1;
	if (!strcmp(eb->name, ".."))
		return 1;
	ret = strcmp(ea->name, eb->name);
	if (ret)
		return ret;
	return strcmp(ea->path, eb->path);
}

static menu_sort menu_qsort_mode;
static int menu_qsort_reverse;

static int menu_entry_cmp_selected(const void *a, const void *b)
{
	const menu_entry *ea = a;
	const menu_entry *eb = b;
	int ret = 0;
	if (ea->is_dir != eb->is_dir)
		return eb->is_dir - ea->is_dir;
	if (!strcmp(ea->name, ".."))
		return -1;
	if (!strcmp(eb->name, ".."))
		return 1;
	if (menu_qsort_mode == MENU_SORT_SIZE && ea->size != eb->size)
		ret = ea->size < eb->size ? 1 : -1;
	if (menu_qsort_mode == MENU_SORT_MTIME && ea->mtime != eb->mtime)
		ret = ea->mtime < eb->mtime ? 1 : -1;
	if (!ret)
		ret = menu_entry_cmp(a, b);
	return menu_qsort_reverse ? -ret : ret;
}

static void menu_set_message(menu_state *m, const char *msg)
{
	snprintf(m->message, sizeof(m->message), "%s", msg ? msg : "");
}

static void menu_fit_cursor(menu_state *m)
{
	if (m->cursor >= m->count)
		m->cursor = m->count - 1;
	if (m->cursor < 0)
		m->cursor = 0;
	if (m->top > m->cursor)
		m->top = m->cursor;
	if (m->cursor >= m->top + MENU_VISIBLE_ROWS)
		m->top = m->cursor - MENU_VISIBLE_ROWS + 1;
	if (m->top < 0)
		m->top = 0;
}

static void menu_select_path(menu_state *m, const char *path)
{
	if (!path)
		return;
	for (int i = 0; i < m->count; i++)
		if (menu_same_path(m->entry[i].path, path)) {
			m->cursor = i;
			menu_fit_cursor(m);
			return;
		}
}

static int menu_load(menu_state *m)
{
	DIR *dir;
	struct dirent *de;
	struct stat st;
	char *path;

	menu_entries_free(m);
	dir = opendir(ex_vcwd);
	if (!dir) {
		menu_set_message(m, "open directory failed");
		return -1;
	}
	if (!menu_root_path(ex_vcwd)) {
		path = menu_path_normalize("..");
		if (!stat(path, &st))
			menu_entry_add(m, "..", path, &st);
		else
			free(path);
	}
	while ((de = readdir(dir))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		path = menu_path_join(ex_vcwd, de->d_name);
		if (stat(path, &st)) {
			free(path);
			continue;
		}
		menu_entry_add(m, de->d_name, path, &st);
	}
	closedir(dir);
	menu_qsort_mode = m->sort;
	menu_qsort_reverse = m->reverse;
	qsort(m->entry, m->count, sizeof(m->entry[0]), menu_entry_cmp_selected);
	menu_fit_cursor(m);
	return 0;
}

static void menu_battery_brief(char *state, int *percent)
{
	char bat[96] = "";
	int whole, frac;
	char bstate[16] = "";

	*state = typewrt_usb_power_present() ? 'C' : 'D';
	*percent = -1;
	if (!typewrt_battery_get_status(bat, sizeof(bat)))
		return;
	if (sscanf(bat, "bat %d.%d%% %*s %15s", &whole, &frac, bstate) == 3) {
		*percent = whole + (frac >= 5 ? 1 : 0);
		if (!strcmp(bstate, "chg"))
			*state = 'C';
		else if (!strcmp(bstate, "dis"))
			*state = 'D';
	}
	if (!strncmp(bat, "bat absent", 10))
		*percent = -1;
}

static int menu_update_top_status(menu_state *m, int force)
{
	char line[NEXTVI_DISPLAY_COLS + 1];
	char rtc[24] = "";
	char date[16] = "";
	char clock[8] = "";
	char pct[8] = "--%";
	time_t now = time(NULL);
	time_t minute = now / 60;
	int pct_value;
	char power_state;
	int power_col;
	int pct_col;
	int clock_col;
	int changed;

	if (!force && m->top_valid && minute == m->top_minute)
		return 0;

	menu_battery_brief(&power_state, &pct_value);
	menu_line(line, "");
	if (!typewrt_rtc_get_datetime(rtc, sizeof(rtc)))
		strcpy(rtc, "-- --- ----  --:--");
	if (strlen(rtc) >= 5) {
		char *time_part = rtc + strlen(rtc) - 5;
		char *date_end = time_part;
		int date_len;
		if (isdigit((unsigned char)time_part[0]) &&
				isdigit((unsigned char)time_part[1]) &&
				time_part[2] == ':' &&
				isdigit((unsigned char)time_part[3]) &&
				isdigit((unsigned char)time_part[4])) {
			snprintf(clock, sizeof(clock), "%s", time_part);
			while (date_end > rtc && date_end[-1] == ' ')
				date_end--;
			date_len = MIN((int)sizeof(date) - 1, (int)(date_end - rtc));
		} else
			date_len = MIN((int)sizeof(date) - 1, (int)strlen(rtc));
		memcpy(date, rtc, date_len);
		date[date_len] = '\0';
	}
	if (pct_value >= 0) {
		if (pct_value > 100)
			pct_value = 100;
		snprintf(pct, sizeof(pct), "%d%%", pct_value);
	}
	power_col = NEXTVI_DISPLAY_COLS - 2 - strlen(pct);
	if (power_col < 0)
		power_col = 0;
	pct_col = power_col + 2;
	for (int i = 0; date[i] && i < power_col - 1; i++)
		line[i] = date[i];
	clock_col = (NEXTVI_DISPLAY_COLS - (int)strlen(clock)) / 2;
	for (int i = 0; clock[i] && clock_col + i < power_col - 1; i++)
		if (clock_col + i >= 0)
			line[clock_col + i] = clock[i];
	line[power_col] = power_state;
	for (int i = 0; pct[i] && pct_col + i < NEXTVI_DISPLAY_COLS; i++)
		line[pct_col + i] = pct[i];

	changed = !m->top_valid || strcmp(m->top_line, line) ||
		m->top_power_state != power_state;
	memcpy(m->top_line, line, sizeof(m->top_line));
	m->top_minute = minute;
	m->top_power_state = power_state;
	m->top_valid = 1;
	return changed;
}

static void menu_draw_top(menu_state *m, int force)
{
	menu_update_top_status(m, force);
	nextvi_display_refresh_line(MENU_TOP_ROW, m->top_line,
		NEXTVI_DISPLAY_COLS);
	menu_draw_row(MENU_SPACER_ROW, "", 0);
	nextvi_display_draw_hline(NEXTVI_FONT_HEIGHT + 3, 0);
}

static void menu_draw_top_if_changed(menu_state *m)
{
	if (!menu_update_top_status(m, 0))
		return;
	nextvi_display_refresh_line(MENU_TOP_ROW, m->top_line,
		NEXTVI_DISPLAY_COLS);
	nextvi_display_draw_hline(NEXTVI_FONT_HEIGHT + 3, 0);
}

static void menu_schedule_status_wakeup(void)
{
	time_t now = time(NULL);
	int seconds = 60 - (int)(now % 60);

	if (seconds <= 0 || seconds > 60)
		seconds = 60;
	typewrt_sleep_set_ui_wakeup_us((uint64_t)seconds * 1000000ULL);
}

static int menu_read_key(menu_state *m)
{
	int c;

	menu_schedule_status_wakeup();
	c = term_read_timeout(0, MENU_STATUS_WAKE_MS);
	if (!c)
		menu_draw_top_if_changed(m);
	return c;
}

static void menu_format_words(char *out, int out_len, long words)
{
	if (out_len <= 0)
		return;
	if (words < 0)
		out[0] = '\0';
	else if (words < 1000)
		snprintf(out, out_len, "%ld", words);
	else if (words < 1000000) {
		long tenths = (words * 10 + 500) / 1000;
		snprintf(out, out_len, "%ld.%ld k", tenths / 10, tenths % 10);
	} else {
		long tenths = (words * 10 + 500000) / 1000000;
		snprintf(out, out_len, "%ld.%ld M", tenths / 10, tenths % 10);
	}
}

static void menu_format_mtime(char *out, int out_len, long mtime)
{
	static const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	time_t now_time = time(NULL);
	time_t entry_time = (time_t)mtime;
	struct tm now_tm;
	struct tm entry_tm;

	localtime_r(&now_time, &now_tm);
	localtime_r(&entry_time, &entry_tm);
	if (entry_tm.tm_year == now_tm.tm_year &&
			entry_tm.tm_yday == now_tm.tm_yday)
		snprintf(out, out_len, "%02d:%02d", entry_tm.tm_hour,
			entry_tm.tm_min);
	else if (entry_tm.tm_year == now_tm.tm_year)
		snprintf(out, out_len, "%s %02d", months[entry_tm.tm_mon],
			entry_tm.tm_mday);
	else
		snprintf(out, out_len, "%s %02d", months[entry_tm.tm_mon],
			entry_tm.tm_mday);
}

static void menu_render_entry(char line[NEXTVI_DISPLAY_COLS + 1],
	const menu_entry *e)
{
	char left[128];
	char words[16];
	char date[16];
	const char *tag = e->is_dir ? "[+]" : (e->synced ? " s " : " * ");
	int words_len;
	int date_len;
	int left_cols = NEXTVI_DISPLAY_COLS - MENU_SIZE_COL_WIDTH -
		MENU_META_GAP - MENU_DATE_COL_WIDTH;
	int size_col = left_cols;
	int date_col = NEXTVI_DISPLAY_COLS - MENU_DATE_COL_WIDTH;

	menu_line(line, "");
	snprintf(left, sizeof(left), "%s %s%s", tag, e->name,
		e->is_dir && strcmp(e->name, "..") ? "/" : "");
	for (int i = 0; left[i] && i < left_cols; i++)
		line[i] = left[i];
	menu_format_words(words, sizeof(words), e->words);
	menu_format_mtime(date, sizeof(date), e->mtime);
	words_len = strlen(words);
	date_len = strlen(date);
	for (int i = words_len - 1, col = size_col + MENU_SIZE_COL_WIDTH - 1;
			i >= 0 && col >= size_col; i--, col--)
		line[col] = words[i];
	for (int i = date_len - 1, col = date_col + MENU_DATE_COL_WIDTH - 1;
			i >= 0 && col >= date_col; i--, col--)
		line[col] = date[i];
}

static void menu_draw(menu_state *m)
{
	char line[NEXTVI_DISPLAY_COLS + 1];

	term_cursor(0);
	term_commit();
	menu_draw_top(m, 0);
	for (int row = 0; row < MENU_VISIBLE_ROWS; row++) {
		int idx = m->top + row;
		if (idx < m->count) {
			menu_entry *e = &m->entry[idx];
			menu_render_entry(line, e);
		} else if (!m->count && row == 0)
			menu_line(line, "empty");
		else
			menu_line(line, "");
		menu_draw_row(MENU_FIRST_ROW + row, line,
			idx == m->cursor && m->count);
	}
	if (m->message[0]) {
		menu_line(line, m->message);
		m->message[0] = '\0';
		m->bottom_message_active = 1;
	} else {
		menu_visible_path(line);
		m->bottom_message_active = 0;
	}
	if (m->bottom_message_active)
		nextvi_display_refresh_line(MENU_BOTTOM_ROW, line, NEXTVI_DISPLAY_COLS);
	else
		nextvi_display_refresh_line_inverted(MENU_BOTTOM_ROW, line,
			NEXTVI_DISPLAY_COLS);
}

static void menu_draw_bottom_path(menu_state *m)
{
	char line[NEXTVI_DISPLAY_COLS + 1];

	menu_visible_path(line);
	nextvi_display_refresh_line_inverted(MENU_BOTTOM_ROW, line,
		NEXTVI_DISPLAY_COLS);
	m->bottom_message_active = 0;
}

static char *menu_trim(char *s)
{
	char *end;
	while (*s == ' ' || *s == '\t')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	return s;
}

static int menu_prompt_cursor_col(const char *prefix, int n)
{
	int col = strlen(prefix) + n;

	if (col >= NEXTVI_DISPLAY_COLS)
		col = NEXTVI_DISPLAY_COLS - 1;
	return col;
}

static int menu_prompt(menu_state *m, const char *prefix, char *out, int out_len)
{
	int n = 0;
	out[0] = '\0';
	while (1) {
		char line[NEXTVI_DISPLAY_COLS + 1];
		char prompt[160];
		int c;
		int cursor_col;
		snprintf(prompt, sizeof(prompt), "%s%s", prefix, out);
		menu_line(line, prompt);
		nextvi_display_refresh_line(MENU_BOTTOM_ROW, line, NEXTVI_DISPLAY_COLS);
		cursor_col = menu_prompt_cursor_col(prefix, n);
		nextvi_display_refresh_cursor(MENU_BOTTOM_ROW, cursor_col, 1);
		c = menu_read_key(m);
		if (!c)
			continue;
		nextvi_display_refresh_cursor(MENU_BOTTOM_ROW, cursor_col, 0);
		if (c == TK_ESC || c == TK_CTL('c')) {
			menu_draw(m);
			return 0;
		}
		if (c == '\n') {
			out[n] = '\0';
			return 1;
		}
		if (c == 127 || c == TK_CTL('h')) {
			if (n > 0)
				out[--n] = '\0';
			continue;
		}
		if (c >= ' ' && c < 0x7f && n + 1 < out_len) {
			out[n++] = c;
			out[n] = '\0';
		}
	}
}

static int menu_confirm(menu_state *m, const char *msg)
{
	char line[NEXTVI_DISPLAY_COLS + 1];
	int c = 0;
	menu_line(line, msg);
	nextvi_display_refresh_line(MENU_BOTTOM_ROW, line, NEXTVI_DISPLAY_COLS);
	while (!c)
		c = menu_read_key(m);
	menu_draw(m);
	return c == 'y' || c == 'Y';
}

static menu_entry *menu_selected(menu_state *m)
{
	if (!m->count || m->cursor < 0 || m->cursor >= m->count)
		return NULL;
	return &m->entry[m->cursor];
}

static int menu_change_dir(menu_state *m, const char *path)
{
	const char *arg = path && *path ? path : MENU_FS_ROOT;
	char *old_path;
	char *target;
	int restored;
	void *ret;

	if (!strcmp(arg, "-")) {
		if (!m->prev_path) {
			menu_set_message(m, "no previous directory");
			return -1;
		}
		arg = m->prev_path;
	}
	target = menu_path_normalize(arg);
	old_path = uc_dup(ex_vcwd);
	menu_dir_state_save(m);
	ret = ec_chdir("", "cd", target);
	if (ret) {
		menu_set_message(m, ret);
		free(old_path);
		free(target);
		return -1;
	}
	free(m->prev_path);
	m->prev_path = old_path;
	m->cursor = 0;
	m->top = 0;
	m->sort = MENU_SORT_NAME;
	m->reverse = 0;
	m->filter[0] = '\0';
	restored = menu_dir_state_restore(m, ex_vcwd);
	menu_load(m);
	if (!restored && menu_parent_of(ex_vcwd, old_path))
		menu_select_path(m, old_path);
	free(target);
	return 0;
}

static int menu_open_path(const char *path)
{
	int old_xvis = xvis;
	xvis |= 4;
	ex_edit(path, strlen(path));
	xvis = old_xvis;
	xquit = 0;
	xmpt = 0;
	return 1;
}

static int menu_open_selected(menu_state *m)
{
	menu_entry *e = menu_selected(m);
	if (!e) {
		menu_set_message(m, "no file selected");
		return 0;
	}
	if (e->is_dir) {
		menu_change_dir(m, e->path);
		return 0;
	}
	return menu_open_path(e->path);
}

static char *menu_resolve_arg(const char *arg)
{
	char *trimmed = menu_trim((char *)arg);
	if (!*trimmed)
		return NULL;
	return menu_path_normalize(trimmed);
}

static void menu_delete_selected(menu_state *m)
{
	menu_entry *e = menu_selected(m);
	char msg[96];
	int ret;
	if (!e) {
		menu_set_message(m, "no file selected");
		return;
	}
	snprintf(msg, sizeof(msg), "delete %s? y/N", e->name);
	if (!menu_confirm(m, msg))
		return;
	ret = e->is_dir ? rmdir(e->path) : unlink(e->path);
	if (ret)
		menu_set_message(m, strerror(errno));
	else {
		menu_set_message(m, "deleted");
		menu_load(m);
	}
}

static void menu_rename_selected(menu_state *m)
{
	menu_entry *e = menu_selected(m);
	char dst_arg[256];
	char *dst;
	if (!e) {
		menu_set_message(m, "no file selected");
		return;
	}
	if (!menu_prompt(m, "rename: ", dst_arg, sizeof(dst_arg)))
		return;
	dst = menu_resolve_arg(dst_arg);
	if (!dst) {
		menu_set_message(m, "rename cancelled");
		return;
	}
	if (rename(e->path, dst))
		menu_set_message(m, strerror(errno));
	else {
		menu_set_message(m, "renamed");
		menu_load(m);
	}
	free(dst);
}

static int menu_copy_file(const char *src, const char *dst)
{
	char buf[512];
	int in, out, ret = 0;
	ssize_t n;
	in = open(src, O_RDONLY);
	if (in < 0)
		return -1;
	typewrt_sd_write_begin();
	out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, conf_mode);
	if (out < 0) {
		ret = -1;
		goto done;
	}
	while ((n = read(in, buf, sizeof(buf))) > 0)
		if (write(out, buf, n) != n) {
			ret = -1;
			break;
		}
	if (n < 0)
		ret = -1;
	close(out);
	done:
	typewrt_sd_write_end();
	close(in);
	return ret;
}

static void menu_copy_selected(menu_state *m)
{
	menu_entry *e = menu_selected(m);
	char dst_arg[256];
	char *dst;
	if (!e || e->is_dir) {
		menu_set_message(m, "select a file to copy");
		return;
	}
	if (!menu_prompt(m, "copy to: ", dst_arg, sizeof(dst_arg)))
		return;
	dst = menu_resolve_arg(dst_arg);
	if (!dst) {
		menu_set_message(m, "copy cancelled");
		return;
	}
	if (menu_copy_file(e->path, dst))
		menu_set_message(m, strerror(errno));
	else {
		menu_set_message(m, "copied");
		menu_load(m);
	}
	free(dst);
}

static void menu_ble_path(menu_state *m, const char *display, const char *path)
{
	const char *err = typewrt_ble_send_file(display, path);
	char status[128];
	if (err) {
		menu_set_message(m, err);
		return;
	}
	nextvi_menu_mark_synced(path);
	typewrt_ble_get_status(status, sizeof(status));
	menu_set_message(m, status);
	menu_load(m);
}

static void menu_ble_selected(menu_state *m)
{
	menu_entry *e = menu_selected(m);
	if (!e || e->is_dir) {
		menu_set_message(m, "select a file for ble");
		return;
	}
	menu_ble_path(m, e->name, e->path);
}

static char *menu_token(char **p)
{
	char *s = *p;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!*s) {
		*p = s;
		return NULL;
	}
	*p = s;
	while (**p && **p != ' ' && **p != '\t')
		(*p)++;
	if (**p)
		*(*p)++ = '\0';
	return s;
}

static int menu_command(menu_state *m, char *cmdline)
{
	char *p = menu_trim(cmdline);
	char *cmd = menu_token(&p);
	if (!cmd)
		return 0;
	if (!strcmp(cmd, "q") || !strcmp(cmd, "quit"))
		return 1;
	if (!strcmp(cmd, "open") || !strcmp(cmd, "e")) {
		char *path;
		int ret;
		p = menu_trim(p);
		if (!*p)
			return menu_open_selected(m);
		path = menu_resolve_arg(p);
		if (!path) {
			menu_set_message(m, "open needs a path");
			return 0;
		}
		ret = menu_open_path(path);
		free(path);
		return ret;
	}
	if (!strcmp(cmd, "cd")) {
		p = menu_trim(p);
		menu_change_dir(m, *p ? p : MENU_FS_ROOT);
		return 0;
	}
	if (!strcmp(cmd, "ls")) {
		char *tok;
		m->sort = MENU_SORT_NAME;
		m->reverse = 0;
		m->filter[0] = '\0';
		while ((tok = menu_token(&p))) {
			if (tok[0] == '-') {
				for (int i = 1; tok[i]; i++) {
					if (tok[i] == 's' || tok[i] == 'S')
						m->sort = MENU_SORT_SIZE;
					else if (tok[i] == 't')
						m->sort = MENU_SORT_MTIME;
					else if (tok[i] == 'r')
						m->reverse = 1;
				}
			} else if (!strcmp(tok, "by-size")) {
				m->sort = MENU_SORT_SIZE;
			} else if (!strcmp(tok, "by-last-modified") ||
					!strcmp(tok, "by-mtime")) {
				m->sort = MENU_SORT_MTIME;
			} else
				snprintf(m->filter, sizeof(m->filter), "%s", tok);
		}
		menu_load(m);
		menu_dir_state_save(m);
		return 0;
	}
	if (!strcmp(cmd, "mkdir")) {
		char *path;
		p = menu_trim(p);
		path = menu_resolve_arg(p);
		if (!path)
			menu_set_message(m, "mkdir needs a path");
		else if (mkdir(path, 0777))
			menu_set_message(m, strerror(errno));
		else {
			menu_set_message(m, "directory created");
			menu_load(m);
		}
		free(path);
		return 0;
	}
	if (!strcmp(cmd, "rm") || !strcmp(cmd, "delete")) {
		char *path;
		struct stat st;
		p = menu_trim(p);
		if (!*p) {
			menu_delete_selected(m);
			return 0;
		}
		path = menu_resolve_arg(p);
		if (!path)
			menu_set_message(m, "delete needs a path");
		else if (!stat(path, &st) && S_ISDIR(st.st_mode) ? rmdir(path) :
				unlink(path))
			menu_set_message(m, strerror(errno));
		else {
			menu_set_message(m, "deleted");
			menu_load(m);
		}
		free(path);
		return 0;
	}
	if (!strcmp(cmd, "rename") || !strcmp(cmd, "mv")) {
		char *src = menu_token(&p);
		char *dst = menu_trim(p);
		char *spath, *dpath;
		if (!src || !*dst) {
			menu_rename_selected(m);
			return 0;
		}
		spath = menu_resolve_arg(src);
		dpath = menu_resolve_arg(dst);
		if (!spath || !dpath)
			menu_set_message(m, "rename needs paths");
		else if (rename(spath, dpath))
			menu_set_message(m, strerror(errno));
		else {
			menu_set_message(m, "renamed");
			menu_load(m);
		}
		free(spath);
		free(dpath);
		return 0;
	}
	if (!strcmp(cmd, "cp") || !strcmp(cmd, "copy")) {
		char *src = menu_token(&p);
		char *dst = menu_trim(p);
		char *spath, *dpath;
		if (!src || !*dst) {
			menu_copy_selected(m);
			return 0;
		}
		spath = menu_resolve_arg(src);
		dpath = menu_resolve_arg(dst);
		if (!spath || !dpath)
			menu_set_message(m, "copy needs paths");
		else if (menu_copy_file(spath, dpath))
			menu_set_message(m, strerror(errno));
		else {
			menu_set_message(m, "copied");
			menu_load(m);
		}
		free(spath);
		free(dpath);
		return 0;
	}
	if (!strcmp(cmd, "ble")) {
		p = menu_trim(p);
		if (!*p)
			menu_ble_selected(m);
		else if (!strcmp(p, "off")) {
			typewrt_ble_stop();
			menu_set_message(m, "ble off");
		} else if (!strcmp(p, "status")) {
			char status[128];
			typewrt_ble_get_status(status, sizeof(status));
			menu_set_message(m, status);
		} else {
			char *path = menu_resolve_arg(p);
			if (path)
				menu_ble_path(m, p, path);
			else
				menu_set_message(m, "ble needs a path");
			free(path);
		}
		return 0;
	}
	if (!strcmp(cmd, "rtc")) {
		char buf[32];
		const char *err;
		p = menu_trim(p);
		if (*p) {
			err = typewrt_rtc_set_datetime(p, buf, sizeof(buf));
			menu_set_message(m, err ? err : buf);
		} else if (typewrt_rtc_get_datetime(buf, sizeof(buf)))
			menu_set_message(m, buf);
		else
			menu_set_message(m, "rtc read failed");
		return 0;
	}
	if (!strcmp(cmd, "bat") || !strcmp(cmd, "battery")) {
		char buf[96];
		typewrt_battery_get_status(buf, sizeof(buf));
		menu_set_message(m, buf);
		return 0;
	}
	if (!strcmp(cmd, "off")) {
		typewrt_power_off();
		return 0;
	}
	menu_set_message(m, "unknown menu command");
	return 0;
}

static void menu_move(menu_state *m, int delta)
{
	if (!m->count)
		return;
	m->cursor += delta;
	if (m->cursor < 0)
		m->cursor = 0;
	if (m->cursor >= m->count)
		m->cursor = m->count - 1;
	menu_fit_cursor(m);
}

static int menu_finish(menu_state *m, int ret)
{
	menu_dir_state_save(m);
	menu_entries_free(m);
	free(m->prev_path);
	typewrt_sleep_clear_ui_wakeup();
	return ret;
}

int nextvi_menu_run(void)
{
	menu_state m;
	memset(&m, 0, sizeof(m));
	menu_dir_state_restore(&m, ex_vcwd);
	menu_load(&m);
	while (1) {
		char cmd[256];
		int c;
		menu_draw(&m);
		c = menu_read_key(&m);
		if (!c) {
			if (m.bottom_message_active)
				menu_draw_bottom_path(&m);
			continue;
		}
		switch (c) {
		case 'h':
			menu_change_dir(&m, "..");
			break;
		case 'j':
		case TK_CTL('n'):
			menu_move(&m, 1);
			break;
		case 'k':
		case TK_CTL('p'):
			menu_move(&m, -1);
			break;
		case TK_CTL('d'):
			menu_move(&m, MENU_VISIBLE_ROWS);
			break;
		case TK_CTL('u'):
			menu_move(&m, -MENU_VISIBLE_ROWS);
			break;
		case 'g':
			m.cursor = 0;
			menu_fit_cursor(&m);
			break;
		case 'G':
			m.cursor = m.count - 1;
			menu_fit_cursor(&m);
			break;
		case '\n':
		case 'l':
		case 'o':
			if (menu_open_selected(&m)) {
				return menu_finish(&m, 1);
			}
			break;
		case 'b':
			menu_ble_selected(&m);
			break;
		case 'c':
			menu_copy_selected(&m);
			break;
		case 'd':
			menu_delete_selected(&m);
			break;
		case 'r':
			menu_rename_selected(&m);
			break;
		case 'R':
			menu_load(&m);
			break;
		case 'P':
			typewrt_power_off();
			break;
		case ':':
			if (menu_prompt(&m, ":", cmd, sizeof(cmd)) &&
					menu_command(&m, cmd)) {
				return menu_finish(&m, 1);
			}
			break;
		case 'q':
			return menu_finish(&m, 1);
		default:
			break;
		}
	}
}

#endif
