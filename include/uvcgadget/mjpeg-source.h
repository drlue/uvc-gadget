/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Slideshow video source
 *
 * Copyright (C) 2018 Paul Elder
 *
 * Contact: Paul Elder <paul.elder@ideasonboard.com>
 */
#ifndef __MJPEGPIPE_VIDEO_SOURCE_H__
#define __MJPEGPIPE_VIDEO_SOURCE_H__

#include "video-source.h"

struct events;
struct video_source;

struct video_source *mjpeg_video_source_create();
void mjpeg_video_source_init(struct video_source *src, struct events *events);

#endif /* __MJPEG_VIDEO_SOURCE_H__ */
