#include "kmap.h"

/* access mode of new files */
const int conf_mode = 0600;

/* automatic hard wrap column; set to 0 to disable */
const int conf_hwwidth = 40;

struct placeholder _ph[3] = {
	{{0x0,0x1f}, "^", 1, 1},
	{{0x200b,0x200b}, "", 0, 3},
	{{0x200c,0x200d}, "-", 1, 3},
};
struct placeholder *ph = _ph;
int phlen = LEN(_ph);

char **conf_kmap(int id)
{
	return kmaps[id];
}

int conf_kmapfind(char *name)
{
	for (int i = 0; i < LEN(kmaps); i++)
		if (name && kmaps[i][0] && !strcmp(name, kmaps[i][0]))
			return i;
	return 0;
}

char *conf_digraph(int c1, int c2)
{
	for (int i = 0; i < LEN(digraphs); i++)
		if (digraphs[i][0][0] == c1 && digraphs[i][0][1] == c2)
			return digraphs[i][1];
	return NULL;
}
