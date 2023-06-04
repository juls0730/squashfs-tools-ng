/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * istream.c
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#include "internal.h"


sqfs_s32 istream_read(istream_t *strm, void *data, size_t size)
{
	sqfs_s32 total = 0;
	size_t diff;

	if (size > 0x7FFFFFFF)
		size = 0x7FFFFFFF;

	while (size > 0) {
		if (strm->buffer_offset >= strm->buffer_used) {
			if (istream_precache(strm))
				return -1;

			if (strm->buffer_used == 0)
				break;
		}

		diff = strm->buffer_used - strm->buffer_offset;
		if (diff > size)
			diff = size;

		memcpy(data, strm->buffer + strm->buffer_offset, diff);
		data = (char *)data + diff;
		strm->buffer_offset += diff;
		size -= diff;
		total += diff;
	}

	return total;
}

int istream_skip(istream_t *strm, sqfs_u64 size)
{
	size_t diff;

	while (size > 0) {
		if (strm->buffer_offset >= strm->buffer_used) {
			if (istream_precache(strm))
				return -1;

			if (strm->buffer_used == 0) {
				fprintf(stderr, "%s: unexpected end-of-file\n",
					strm->get_filename(strm));
				return -1;
			}
		}

		diff = strm->buffer_used - strm->buffer_offset;
		if ((sqfs_u64)diff > size)
			diff = size;

		strm->buffer_offset += diff;
		size -= diff;
	}

	return 0;
}

sqfs_s32 istream_splice(istream_t *in, ostream_t *out, sqfs_u32 size)
{
	sqfs_s32 total = 0;
	size_t diff;

	if (size > 0x7FFFFFFF)
		size = 0x7FFFFFFF;

	while (size > 0) {
		if (in->buffer_offset >= in->buffer_used) {
			if (istream_precache(in))
				return -1;

			if (in->buffer_used == 0)
				break;
		}

		diff = in->buffer_used - in->buffer_offset;
		if (diff > size)
			diff = size;

		if (ostream_append(out, in->buffer + in->buffer_offset, diff))
			return -1;

		in->buffer_offset += diff;
		size -= diff;
		total += diff;
	}

	return total;
}
