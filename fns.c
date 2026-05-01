/*
 * fns - shared globals and stray CRT replacements used across subsystems.
 *
 * Holds the runtime-filled paths to the mul/bkp data files that
 * dynamic.c and filemanager.c hand to fopen, plus small helpers
 * (strlwr, strcmpi) that the MSVC CRT provided and glibc does not.
 */

#include <ctype.h>
#include <string.h>
#include <strings.h>

char *GLOBAL_file_map0_mul;
char *GLOBAL_file_staidx0_mul;
char *GLOBAL_file_staidx0_bkp;
char *GLOBAL_file_statics0_mul;
char *GLOBAL_file_statics0_bkp;
char *GLOBAL_file_dynidx0_mul;
char *GLOBAL_file_dynidx0_bkp;
char *GLOBAL_file_dynamic0_mul;
char *GLOBAL_file_dynamic0_bkp;
char *GLOBAL_file_tempidx_mul;
char *GLOBAL_file_temp_mul;
char *GLOBAL_file_regions_txt;
int g_TerminateServerFlag;

char *
strlwr(char *s)
{
	unsigned int i;

	for (i = 0; s[i] != '\0'; i++)
		s[i] = tolower(s[i]);
	return s;
}

char *
strcasestr(const char *haystack, const char *needle)
{
	size_t nlen;

	if (*needle == '\0')
		return (char *)haystack;
	nlen = strlen(needle);
	for (; *haystack != '\0'; haystack++) {
		if (strncasecmp(haystack, needle, nlen) == 0)
			return (char *)haystack;
	}
	return NULL;
}
