/** @file src/os/readdir.c Directory traversal for POSIX systems */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __WATCOMC__
#include <direct.h>
#else
#include <dirent.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include "types.h"
#include "file.h"
#include "dune_error.h"
#include "dune_strings.h"
#include "readdir.h"

/**
 * Process all files in the directory.
 *
 * @param dirpath path to directory to traverse.
 * @param callback function called back for each file found.
 * @return True if and only if everything was ok.
 */
#if defined(TDECK)
#include <errno.h>
#include <string.h>
#endif

bool ReadDir_ProcessAllFiles(const char * dirpath, bool (*callback)(const char *name, const char *path, uint32 size))
{
	struct dirent *ep;
	DIR *dp;

#if defined(TDECK)
	/* Confirmed via hardware testing (errno 22): ESP-IDF's VFS/FATFS
	 * opendir() returns EINVAL for a path with a trailing slash, unlike
	 * most POSIX systems. dirpath always ends in '/' by construction
	 * here (see File_MakeCompleteFilename in file.c) because the loop
	 * below concatenates dirpath directly with each entry's name without
	 * inserting a separator - so we open a slash-stripped COPY, while
	 * leaving the original dirpath (and that concatenation) untouched. */
	char dirpathNoSlash[1024];
	size_t dirpathLen = strlen(dirpath);
	if (dirpathLen > 1 && dirpath[dirpathLen - 1] == '/') {
		if (dirpathLen >= sizeof(dirpathNoSlash)) dirpathLen = sizeof(dirpathNoSlash) - 1;
		memcpy(dirpathNoSlash, dirpath, dirpathLen - 1);
		dirpathNoSlash[dirpathLen - 1] = '\0';
		dp = opendir(dirpathNoSlash);
	} else {
		dp = opendir(dirpath);
	}
#else
	dp = opendir(dirpath);
#endif

	if (dp == NULL) {
#if defined(TDECK)
		Error("opendir(\"%s\") failed: errno=%d (%s)\n", dirpath, errno, strerror(errno));
#endif
		return false;
	}
	while ((ep = readdir(dp)) != NULL) {
		char path[1024];
		struct stat st;
		if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0)
			continue;
		snprintf(path, sizeof(path), "%s%s", dirpath, ep->d_name);
		/* lstat() should be prefered, but is not always available */
		if (stat(path, &st) < 0) {
			Warning("lstat(%s) failed\n", path);
			continue;
		}
		if (S_ISDIR(st.st_mode)) continue;
		if (!callback(ep->d_name, path, (uint32)st.st_size)) {
			closedir(dp);
			return false;
		}
	}
	closedir(dp);
	return true;
}

