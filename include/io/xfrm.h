/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * xfrm.h
 *
 * Copyright (C) 2019 David Oberhollenzer <goliath@infraroot.at>
 */
#ifndef IO_XFRM_H
#define IO_XFRM_H

#include "io/istream.h"
#include "io/ostream.h"
#include "xfrm/stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create an input stream that transparently decodes data.
 *
 * @memberof istream_t
 *
 * This function creates an input stream that wraps an underlying input stream
 * that is encoded/compressed and transparently decodes the data when reading
 * from it.
 *
 * @param strm A pointer to another stream that should be wrapped.
 * @param xfrm The transformation stream to use.
 *
 * @return A pointer to an input stream on success, NULL on failure.
 */
SQFS_INTERNAL istream_t *istream_xfrm_create(istream_t *strm,
					     xfrm_stream_t *xfrm);

/**
 * @brief Create an output stream that transparently encodes data.
 *
 * @memberof ostream_t
 *
 * This function creates an output stream that transparently encodes
 * (e.g. compresses) all data appended to it and writes it to an
 * underlying, wrapped output stream.
 *
 * @param strm A pointer to another stream that should be wrapped.
 * @param xfrm The transformation stream to use.
 *
 * @return A pointer to an output stream on success, NULL on failure.
 */
SQFS_INTERNAL ostream_t *ostream_xfrm_create(ostream_t *strm,
					     xfrm_stream_t *xfrm);

#ifdef __cplusplus
}
#endif

#endif /* IO_XFRM_H */
