/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * mkfs.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#include "mkfs.h"

static int pack_file(sqfs_block_processor_t *data,
		     const char *path, tree_node_t *n, const options_t *opt)
{
	sqfs_ostream_t *out = NULL;
	sqfs_istream_t *in = NULL;
	sqfs_file_handle_t hnd;
	sqfs_u64 filesize;
	int ret, flags;

	ret = sqfs_native_file_open(&hnd, path, SQFS_FILE_OPEN_READ_ONLY);
	if (ret)
		goto done;

	ret = sqfs_native_file_get_size(hnd, &filesize);
	if (ret)
		goto done;

	ret = sqfs_istream_open_handle(&in, path, hnd, 0);
	if (ret)
		goto done;

	flags = n->data.file.flags;
	if (opt->no_tail_packing && filesize > opt->cfg.block_size)
		flags |= SQFS_BLK_DONT_FRAGMENT;

	ret = sqfs_block_processor_create_ostream(&out, path, data,
						  &(n->data.file.inode), flags);
	if (ret)
		goto done;

	do {
		ret = sqfs_istream_splice(in, out, opt->cfg.block_size);
	} while (ret > 0);

	if (ret == 0)
		ret = out->flush(out);
done:
	if (ret)
		sqfs_perror(path, NULL, ret);
	if (in == NULL)
		sqfs_native_file_close(hnd);
	sqfs_drop(out);
	sqfs_drop(in);
	return ret;
}

static int pack_files(sqfs_block_processor_t *data, fstree_t *fs,
		      options_t *opt)
{
	tree_node_t *node;
	int ret;

	if (opt->packdir != NULL && chdir(opt->packdir) != 0) {
		perror(opt->packdir);
		return -1;
	}

	for (node = fs->files; node != NULL; node = node->next_by_type) {
		const char *path = node->data.file.input_file;
		char *node_path = NULL;

		if (path == NULL) {
			node_path = fstree_get_path(node);
			if (node_path == NULL) {
				perror("reconstructing file path");
				return -1;
			}

			ret = canonicalize_name(node_path);
			assert(ret == 0);

			path = node_path;
		}

		if (!opt->cfg.quiet)
			printf("packing %s\n", path);

		ret = pack_file(data, path, node, opt);
		free(node_path);

		if (ret)
			return -1;
	}

	return 0;
}

static int relabel_tree_dfs(const char *filename, sqfs_xattr_writer_t *xwr,
			    tree_node_t *n, void *selinux_handle,
			    void *xattrmap)
{
	char *path = fstree_get_path(n);
	int ret;

	if (path == NULL) {
		perror("getting absolute node path for SELinux relabeling");
		return -1;
	}

	ret = sqfs_xattr_writer_begin(xwr, 0);
	if (ret) {
		sqfs_perror(filename, "recording xattr key-value pairs", ret);
		return -1;
	}

	if (xattrmap != NULL)
		ret = xattr_apply_map_file(path, xattrmap, xwr);

	if (ret == 0 && selinux_handle != NULL)
		ret = selinux_relable_node(selinux_handle, xwr, n, path);

	free(path);
	if (ret == 0)
		ret = sqfs_xattr_writer_end(xwr, &n->xattr_idx);

	if (ret) {
		sqfs_perror(filename, "flushing completed key-value pairs",
			    ret);
		return -1;
	}

	if (S_ISDIR(n->mode)) {
		for (n = n->data.children; n != NULL; n = n->next) {
			if (relabel_tree_dfs(filename, xwr, n,
					     selinux_handle, xattrmap)) {
				return -1;
			}
		}
	}

	return 0;
}

static int read_fstree(fstree_t *fs, options_t *opt, sqfs_xattr_writer_t *xwr,
		       void *selinux_handle, void *xattrmap)
{
	int ret;

	ret = fstree_from_file(fs, opt->infile, opt->packdir);

	if (ret == 0 && (selinux_handle != NULL || xattrmap != NULL))
		ret = relabel_tree_dfs(opt->cfg.filename, xwr,
				       fs->root, selinux_handle, xattrmap);

	return ret;
}

static void override_owner_dfs(const options_t *opt, tree_node_t *n)
{
	if (opt->force_uid)
		n->uid = opt->force_uid_value;

	if (opt->force_gid)
		n->gid = opt->force_gid_value;

	if (S_ISDIR(n->mode)) {
		for (n = n->data.children; n != NULL; n = n->next)
			override_owner_dfs(opt, n);
	}
}

int main(int argc, char **argv)
{
	int status = EXIT_FAILURE;
	sqfs_istream_t *sortfile = NULL;
	void *sehnd = NULL;
	void *xattrmap = NULL;
	sqfs_writer_t sqfs;
	options_t opt;

	process_command_line(&opt, argc, argv);

	if (sqfs_writer_init(&sqfs, &opt.cfg))
		return EXIT_FAILURE;

	if (opt.selinux != NULL) {
		sehnd = selinux_open_context_file(opt.selinux);
		if (sehnd == NULL)
			goto out;
	}
	if (opt.xattr_file != NULL) {
		xattrmap = xattr_open_map_file(opt.xattr_file);
		if (xattrmap == NULL)
			goto out;
	}

	if (opt.sortfile != NULL) {
		int ret = sqfs_istream_open_file(&sortfile, opt.sortfile, 0);
		if (ret) {
			sqfs_perror(opt.sortfile, NULL, ret);
			goto out;
		}
	}

	if (opt.infile == NULL) {
		sqfs_dir_iterator_t *dir = NULL;
		dir_tree_cfg_t cfg;
		int ret;

		memset(&cfg, 0, sizeof(cfg));
		cfg.flags = opt.dirscan_flags | DIR_SCAN_KEEP_UID |
			DIR_SCAN_KEEP_GID | DIR_SCAN_KEEP_MODE;
		cfg.def_mtime = sqfs.fs.defaults.mtime;

		dir = dir_tree_iterator_create(opt.packdir, &cfg);
		if (dir == NULL)
			goto out;

		ret = fstree_from_dir(&sqfs.fs, dir);
		sqfs_drop(dir);
		if (ret != 0)
			goto out;
	} else {
		if (read_fstree(&sqfs.fs, &opt, sqfs.xwr, sehnd, xattrmap))
			goto out;
	}

	if (opt.force_uid || opt.force_gid)
		override_owner_dfs(&opt, sqfs.fs.root);

	if (fstree_post_process(&sqfs.fs))
		goto out;

	if (opt.infile == NULL) {
		if (xattrs_from_dir(&sqfs.fs, opt.packdir, sehnd, xattrmap,
				    sqfs.xwr, opt.scan_xattr)) {
			goto out;
		}
	}

	if (sortfile != NULL) {
		if (fstree_sort_files(&sqfs.fs, sortfile))
			goto out;
	}

	if (pack_files(sqfs.data, &sqfs.fs, &opt))
		goto out;

	if (sqfs_writer_finish(&sqfs, &opt.cfg))
		goto out;

	status = EXIT_SUCCESS;
out:
	sqfs_writer_cleanup(&sqfs, status);
	if (sehnd != NULL)
		selinux_close_context_file(sehnd);
	if (sortfile != NULL)
		sqfs_drop(sortfile);
	free(opt.packdir);
	return status;
}
