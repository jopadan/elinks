/* Cookie path matching */

#include <string.h>
#include "cookies/path.h"

int
is_path_prefix(char *cookiepath, char *requestpath)
{
	//ELOG
	int dl = strlen(cookiepath);

	if (!dl) return 1;

	if (memcmp(cookiepath, requestpath, dl)) return 0;

	return (!memcmp(cookiepath, requestpath, dl)
	&& (cookiepath[dl - 1] == '/' || requestpath[dl] == '/' || requestpath[dl] == '\0'));
}
