/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ostream.h
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#ifndef IO_OSTREAM_H
#define IO_OSTREAM_H

#include "sqfs/predef.h"

/**
 * @struct ostream_t
 *
 * @extends sqfs_object_t
 *
 * @brief An append-only data stream.
 */
typedef struct ostream_t {
	sqfs_object_t base;

	int (*append)(struct ostream_t *strm, const void *data, size_t size);

	int (*append_sparse)(struct ostream_t *strm, size_t size);

	int (*flush)(struct ostream_t *strm);

	const char *(*get_filename)(struct ostream_t *strm);
} ostream_t;

enum {
	OSTREAM_OPEN_OVERWRITE = 0x01,
	OSTREAM_OPEN_SPARSE = 0x02,
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Append a block of data to an output stream.
 *
 * @memberof ostream_t
 *
 * @param strm A pointer to an output stream.
 * @param data A pointer to the data block to append.
 * @param size The number of bytes to append.
 *
 * @return Zero on success, -1 on failure.
 */
SQFS_INLINE int ostream_append(ostream_t *strm, const void *data,
			       size_t size)
{
	return strm->append(strm, data, size);
}

/**
 * @brief Append a number of zero bytes to an output stream.
 *
 * @memberof ostream_t
 *
 * If the unerlying implementation supports sparse files, this function can be
 * used to create a "hole". If the implementation does not support it, a
 * fallback is used that just appends a block of zeros manualy.
 *
 * @param strm A pointer to an output stream.
 * @param size The number of zero bytes to append.
 *
 * @return Zero on success, -1 on failure.
 */
SQFS_INTERNAL int ostream_append_sparse(ostream_t *strm, size_t size);

/**
 * @brief Process all pending, buffered data and flush it to disk.
 *
 * @memberof ostream_t
 *
 * If the stream performs some kind of transformation (e.g. transparent data
 * compression), flushing caues the wrapped format to insert a termination
 * token. Only call this function when you are absolutely DONE appending data,
 * shortly before destroying the stream.
 *
 * @param strm A pointer to an output stream.
 *
 * @return Zero on success, -1 on failure.
 */
SQFS_INLINE int ostream_flush(ostream_t *strm)
{
	return strm->flush(strm);
}

/**
 * @brief Get the underlying filename of a output stream.
 *
 * @memberof ostream_t
 *
 * @param strm The output stream to get the filename from.
 *
 * @return A string holding the underlying filename.
 */
SQFS_INLINE const char *ostream_get_filename(ostream_t *strm)
{
	return strm->get_filename(strm);
}

#ifdef __cplusplus
}
#endif

#endif /* IO_OSTREAM_H */
