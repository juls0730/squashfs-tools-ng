/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * fstree_from_file.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#include "config.h"

#include "util/util.h"
#include "io/file.h"
#include "compat.h"
#include "mkfs.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>

static const struct {
	const char *name;
	unsigned int clear_flag;
	unsigned int set_flag;
} glob_scan_flags[] = {
	{ "-type b", DIR_SCAN_NO_BLK, 0 },
	{ "-type c", DIR_SCAN_NO_CHR, 0 },
	{ "-type d", DIR_SCAN_NO_DIR, 0 },
	{ "-type p", DIR_SCAN_NO_FIFO, 0 },
	{ "-type f", DIR_SCAN_NO_FILE, 0 },
	{ "-type l", DIR_SCAN_NO_SLINK, 0 },
	{ "-type s", DIR_SCAN_NO_SOCK, 0 },
	{ "-xdev", 0, DIR_SCAN_ONE_FILESYSTEM },
	{ "-mount", 0, DIR_SCAN_ONE_FILESYSTEM },
	{ "-keeptime", 0, DIR_SCAN_KEEP_TIME },
	{ "-nonrecursive", 0, DIR_SCAN_NO_RECURSION },
};

static int add_generic(fstree_t *fs, const char *filename, size_t line_num,
		       const char *path, struct stat *sb,
		       const char *basepath, unsigned int glob_flags,
		       const char *extra)
{
	(void)basepath;
	(void)glob_flags;

	if (fstree_add_generic(fs, path, sb, extra) == NULL) {
		fprintf(stderr, "%s: " PRI_SZ ": %s: %s\n",
			filename, line_num, path, strerror(errno));
		return -1;
	}

	return 0;
}

static int add_device(fstree_t *fs, const char *filename, size_t line_num,
		      const char *path, struct stat *sb, const char *basepath,
		      unsigned int glob_flags, const char *extra)
{
	unsigned int maj, min;
	char c;

	if (sscanf(extra, "%c %u %u", &c, &maj, &min) != 3) {
		fprintf(stderr, "%s: " PRI_SZ ": "
			"expected '<c|b> major minor'\n",
			filename, line_num);
		return -1;
	}

	if (c == 'c' || c == 'C') {
		sb->st_mode |= S_IFCHR;
	} else if (c == 'b' || c == 'B') {
		sb->st_mode |= S_IFBLK;
	} else {
		fprintf(stderr, "%s: " PRI_SZ ": unknown device type '%c'\n",
			filename, line_num, c);
		return -1;
	}

	sb->st_rdev = makedev(maj, min);
	return add_generic(fs, filename, line_num, path, sb, basepath,
			   glob_flags, NULL);
}

static int add_file(fstree_t *fs, const char *filename, size_t line_num,
		    const char *path, struct stat *basic, const char *basepath,
		    unsigned int glob_flags, const char *extra)
{
	if (extra == NULL || *extra == '\0')
		extra = path;

	return add_generic(fs, filename, line_num, path, basic,
			   basepath, glob_flags, extra);
}

static int add_hard_link(fstree_t *fs, const char *filename, size_t line_num,
			 const char *path, struct stat *basic,
			 const char *basepath, unsigned int glob_flags,
			 const char *extra)
{
	(void)basepath;
	(void)glob_flags;
	(void)basic;

	if (fstree_add_hard_link(fs, path, extra) == NULL) {
		fprintf(stderr, "%s: " PRI_SZ ": %s\n",
			filename, line_num, strerror(errno));
		return -1;
	}
	return 0;
}

static size_t name_string_length(const char *str)
{
	size_t len = 0;
	int start;

	if (*str == '"' || *str == '\'') {
		start = *str;
		++len;

		while (str[len] != '\0' && str[len] != start)
			++len;

		if (str[len] == start)
			++len;
	} else {
		while (str[len] != '\0' && !isspace(str[len]))
			++len;
	}

	return len;
}

static void quote_remove(char *str)
{
	char *dst = str;
	int start = *(str++);

	if (start != '\'' && start != '"')
		return;

	while (*str != start && *str != '\0')
		*(dst++) = *(str++);

	*(dst++) = '\0';
}

static int glob_files(fstree_t *fs, const char *filename, size_t line_num,
		      const char *path, struct stat *basic,
		      const char *basepath, unsigned int glob_flags,
		      const char *extra)
{
	char *name_pattern = NULL, *prefix = NULL;
	unsigned int scan_flags = 0, all_flags;
	dir_iterator_t *dir = NULL;
	bool first_clear_flag;
	size_t i, count, len;
	dir_tree_cfg_t cfg;
	tree_node_t *root;
	int ret;

	/* fetch the actual target node */
	root = fstree_get_node_by_path(fs, fs->root, path, true, false);
	if (root == NULL) {
		fprintf(stderr, "%s: " PRI_SZ ": %s: %s\n",
			filename, line_num, path, strerror(errno));
		return -1;
	}

	if (!S_ISDIR(root->mode)) {
		fprintf(stderr, "%s: " PRI_SZ ": %s is not a directoy!\n",
			filename, line_num, path);
		return -1;
	}

	prefix = fstree_get_path(root);
	if (canonicalize_name(prefix) != 0) {
		fprintf(stderr, "%s: " PRI_SZ ": error cannonicalizing `%s`!\n",
			filename, line_num, prefix);
		free(prefix);
		return -1;
	}

	/* process options */
	first_clear_flag = true;

	all_flags = DIR_SCAN_NO_BLK | DIR_SCAN_NO_CHR | DIR_SCAN_NO_DIR |
		DIR_SCAN_NO_FIFO | DIR_SCAN_NO_FILE | DIR_SCAN_NO_SLINK |
		DIR_SCAN_NO_SOCK;

	while (extra != NULL && *extra != '\0') {
		count = sizeof(glob_scan_flags) / sizeof(glob_scan_flags[0]);

		for (i = 0; i < count; ++i) {
			len = strlen(glob_scan_flags[i].name);
			if (strncmp(extra, glob_scan_flags[i].name, len) != 0)
				continue;

			if (isspace(extra[len])) {
				extra += len;
				while (isspace(*extra))
					++extra;
				break;
			}
		}

		if (i < count) {
			if (glob_scan_flags[i].clear_flag != 0 &&
			    first_clear_flag) {
				scan_flags |= all_flags;
				first_clear_flag = false;
			}

			scan_flags &= ~(glob_scan_flags[i].clear_flag);
			scan_flags |= glob_scan_flags[i].set_flag;
			continue;
		}

		if (strncmp(extra, "-name", 5) == 0 && isspace(extra[5])) {
			for (extra += 5; isspace(*extra); ++extra)
				;

			len = name_string_length(extra);

			free(name_pattern);
			name_pattern = strndup(extra, len);
			extra += len;

			while (isspace(*extra))
				++extra;

			quote_remove(name_pattern);
			continue;
		}

		if (strncmp(extra, "-path", 5) == 0 && isspace(extra[5])) {
			for (extra += 5; isspace(*extra); ++extra)
				;

			len = name_string_length(extra);

			free(name_pattern);
			name_pattern = strndup(extra, len);
			extra += len;

			while (isspace(*extra))
				++extra;

			quote_remove(name_pattern);
			glob_flags |= DIR_SCAN_MATCH_FULL_PATH;
			continue;
		}

		if (extra[0] == '-') {
			if (extra[1] == '-' && isspace(extra[2])) {
				extra += 2;
				while (isspace(*extra))
					++extra;
				break;
			}

			fprintf(stderr, "%s: " PRI_SZ ": unknown option.\n",
				filename, line_num);
			free(name_pattern);
			free(prefix);
			return -1;
		} else {
			break;
		}
	}

	if (extra != NULL && *extra == '\0')
		extra = NULL;

	/* do the scan */
	memset(&cfg, 0, sizeof(cfg));
	cfg.flags = scan_flags | glob_flags;
	cfg.def_mtime = basic->st_mtime;
	cfg.def_uid = basic->st_uid;
	cfg.def_gid = basic->st_gid;
	cfg.def_mode = basic->st_mode;
	cfg.prefix = prefix;
	cfg.name_pattern = name_pattern;

	if (basepath == NULL) {
		dir = dir_tree_iterator_create(extra == NULL ? "." : extra,
					       &cfg);
	} else {
		size_t plen = strlen(basepath);
		size_t slen = strlen(extra);
		char *temp = calloc(1, plen + 1 + slen + 1);

		if (temp == NULL) {
			fprintf(stderr, "%s: " PRI_SZ ": allocation failure.\n",
				filename, line_num);
		} else {
			memcpy(temp, basepath, plen);
			temp[plen] = '/';
			memcpy(temp + plen + 1, extra, slen);
			temp[plen + 1 + slen] = '\0';

			dir = dir_tree_iterator_create(temp, &cfg);
		}

		free(temp);
	}

	if (dir != NULL) {
		ret = fstree_from_dir(fs, dir);
		sqfs_drop(dir);
	} else {
		ret = -1;
	}

	free(name_pattern);
	free(prefix);
	return ret;
}

static const struct callback_t {
	const char *keyword;
	unsigned int mode;
	bool need_extra;
	bool is_glob;
	bool allow_root;
	int (*callback)(fstree_t *fs, const char *filename, size_t line_num,
			const char *path, struct stat *sb,
			const char *basepath, unsigned int glob_flags,
			const char *extra);
} file_list_hooks[] = {
	{ "dir", S_IFDIR, false, false, true, add_generic },
	{ "slink", S_IFLNK, true, false, false, add_generic },
	{ "link", 0, true, false, false, add_hard_link },
	{ "nod", 0, true, false, false, add_device },
	{ "pipe", S_IFIFO, false, false, false, add_generic },
	{ "sock", S_IFSOCK, false, false, false, add_generic },
	{ "file", S_IFREG, false, false, false, add_file },
	{ "glob", 0, false, true, true, glob_files },
};

#define NUM_HOOKS (sizeof(file_list_hooks) / sizeof(file_list_hooks[0]))

static char *skip_space(char *str)
{
	if (!isspace(*str))
		return NULL;
	while (isspace(*str))
		++str;
	return str;
}

static char *read_u32(char *str, sqfs_u32 *out, sqfs_u32 base)
{
	*out = 0;

	if (!isdigit(*str))
		return NULL;

	while (isdigit(*str)) {
		sqfs_u32 x = *(str++) - '0';

		if (x >= base || (*out) > (0xFFFFFFFF - x) / base)
			return NULL;

		(*out) = (*out) * base + x;
	}

	return str;
}

static char *read_str(char *str, char **out)
{
	*out = str;

	if (*str == '"') {
		char *ptr = str++;

		while (*str != '\0' && *str != '"') {
			if (str[0] == '\\' &&
			    (str[1] == '"' || str[1] == '\\')) {
				*(ptr++) = str[1];
				str += 2;
			} else {
				*(ptr++) = *(str++);
			}
		}

		if (str[0] != '"' || !isspace(str[1]))
			return NULL;

		*ptr = '\0';
		++str;
	} else {
		while (*str != '\0' && !isspace(*str))
			++str;

		if (!isspace(*str))
			return NULL;

		*(str++) = '\0';
	}

	while (isspace(*str))
		++str;

	return str;
}

static int handle_line(fstree_t *fs, const char *filename,
		       size_t line_num, char *line,
		       const char *basepath)
{
	const char *extra = NULL, *msg = NULL;
	const struct callback_t *cb = NULL;
	unsigned int glob_flags = 0;
	sqfs_u32 uid, gid, mode;
	struct stat sb;
	char *path;

	for (size_t i = 0; i < NUM_HOOKS; ++i) {
		size_t len = strlen(file_list_hooks[i].keyword);
		if (strncmp(file_list_hooks[i].keyword, line, len) != 0)
			continue;

		if (isspace(line[len])) {
			cb = file_list_hooks + i;
			line = skip_space(line + len);
			break;
		}
	}

	if (cb == NULL)
		goto fail_kw;

	if ((line = read_str(line, &path)) == NULL)
		goto fail_ent;

	if (canonicalize_name(path))
		goto fail_ent;

	if (*path == '\0' && !cb->allow_root)
		goto fail_root;

	if (cb->is_glob && *line == '*') {
		++line;
		mode = 0;
		glob_flags |= DIR_SCAN_KEEP_MODE;
	} else {
		if ((line = read_u32(line, &mode, 8)) == NULL || mode > 07777)
			goto fail_mode;
	}

	if ((line = skip_space(line)) == NULL)
		goto fail_ent;

	if (cb->is_glob && *line == '*') {
		++line;
		uid = 0;
		glob_flags |= DIR_SCAN_KEEP_UID;
	} else {
		if ((line = read_u32(line, &uid, 10)) == NULL)
			goto fail_uid_gid;
	}

	if ((line = skip_space(line)) == NULL)
		goto fail_ent;

	if (cb->is_glob && *line == '*') {
		++line;
		gid = 0;
		glob_flags |= DIR_SCAN_KEEP_GID;
	} else {
		if ((line = read_u32(line, &gid, 10)) == NULL)
			goto fail_uid_gid;
	}

	if ((line = skip_space(line)) != NULL && *line != '\0')
		extra = line;

	if (cb->need_extra && extra == NULL)
		goto fail_no_extra;

	/* forward to callback */
	memset(&sb, 0, sizeof(sb));
	sb.st_mtime = fs->defaults.mtime;
	sb.st_mode = mode | cb->mode;
	sb.st_uid = uid;
	sb.st_gid = gid;

	return cb->callback(fs, filename, line_num, path,
			    &sb, basepath, glob_flags, extra);
fail_root:
	fprintf(stderr, "%s: " PRI_SZ ": cannot use / as argument for %s.\n",
		filename, line_num, cb->keyword);
	return -1;
fail_no_extra:
	fprintf(stderr, "%s: " PRI_SZ ": missing argument for %s.\n",
		filename, line_num, cb->keyword);
	return -1;
fail_uid_gid:
	msg = "uid & gid must be decimal numbers < 2^32";
	goto out_desc;
fail_mode:
	msg = "mode must be an octal number <= 07777";
	goto out_desc;
fail_kw:
	msg = "unknown entry type";
	goto out_desc;
fail_ent:
	msg = "error in entry description";
	goto out_desc;
out_desc:
	fprintf(stderr, "%s: " PRI_SZ ": %s.\n", filename, line_num, msg);
	fputs("expected: <type> <path> <mode> <uid> <gid> [<extra>]\n",
	      stderr);
	return -1;
}

int fstree_from_file_stream(fstree_t *fs, istream_t *fp, const char *basepath)
{
	const char *filename;
	size_t line_num = 1;
	char *line;
	int ret;

	filename = istream_get_filename(fp);

	for (;;) {
		ret = istream_get_line(fp, &line, &line_num,
				       ISTREAM_LINE_LTRIM | ISTREAM_LINE_SKIP_EMPTY);
		if (ret < 0)
			return -1;
		if (ret > 0)
			break;

		if (line[0] != '#') {
			if (handle_line(fs, filename, line_num,
					line, basepath)) {
				goto fail_line;
			}
		}

		free(line);
		++line_num;
	}

	return 0;
fail_line:
	free(line);
	return -1;
}

int fstree_from_file(fstree_t *fs, const char *filename, const char *basepath)
{
	istream_t *fp;
	int ret;

	fp = istream_open_file(filename);
	if (fp == NULL)
		return -1;

	ret = fstree_from_file_stream(fs, fp, basepath);

	sqfs_drop(fp);
	return ret;
}
