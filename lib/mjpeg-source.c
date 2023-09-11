/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * JPEG still image video source
 *
 * Copyright (C) 2018 Paul Elder
 *
 * Contact: Paul Elder <paul.elder@ideasonboard.com>
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <linux/videodev2.h>

#include "events.h"
#include "timer.h"
#include "tools.h"
#include "v4l2.h"
#include "mjpeg-source.h"
#include "video-buffers.h"

const int BUFFER_SIZE = 256;

struct mjpeg_source
{
	struct video_source src;

	char *mjpegPipePath;
	char *mjpegPipeSignalPath;

	char *jpegBuffer;
	FILE *fd;
	int count;
	int data;
	int jpegIndex;
	struct timeval *start;
	struct timeval *tmp;

	unsigned int framerate;
	bool streaming;
};

#define to_mjpeg_source(s) container_of(s, struct mjpeg_source, src)

static int findEof(char buffer[], int length, int offset)
{
	int firstChar = buffer[offset] & 0xFF;
	for (int i = offset + 1; i < length; i++)
	{
		int secondChar = buffer[i] & 0xFF;

		// match condition

		if (firstChar == 0xFF && secondChar == 0xD9)
		{
			return i;
		}
		firstChar = secondChar;
	}
	return 0;
}

static void mjpeg_source_destroy(struct video_source *s)
{
	struct mjpeg_source *src = to_mjpeg_source(s);

	if (src->jpegBuffer)
	{
		free(src->jpegBuffer);
	}
	if (src->fd)
	{
		fclose(src->fd);
	}
	if (src->start)
	{
		free(src->start);
	}
	if (src->tmp)
	{
		free(src->tmp);
	}
	free(src);
}

static int mjpeg_source_set_format(struct video_source *s __attribute__((unused)),
								   struct v4l2_pix_format *fmt)
{
	if (fmt->pixelformat != v4l2_fourcc('M', 'J', 'P', 'G'))
	{
		printf("jpg-source: unsupported fourcc\n");
		return -EINVAL;
	}

	return 0;
}

static int mjpeg_source_set_frame_rate(struct video_source *s, unsigned int fps)
{
	struct mjpeg_source *src = to_mjpeg_source(s);
	src->framerate = fps;
	return 0;
}

static int mjpeg_source_free_buffers(struct video_source *s __attribute__((unused)))
{
	return 0;
}

static int mjpeg_source_stream_on(struct video_source *s)
{
	struct mjpeg_source *src = to_mjpeg_source(s);
	// int ret;

	// ret = timer_arm(src->timer);
	// if (ret)
	// 	return ret;

	src->streaming = true;
	return 0;
}

static int mjpeg_source_stream_off(struct video_source *s)
{
	struct mjpeg_source *src = to_mjpeg_source(s);
	// int ret;

	// /*
	//  * No error check here, because we want to flag that streaming is over
	//  * even if the timer is still running due to the failure.
	//  */
	// ret = timer_disarm(src->timer);
	src->streaming = false;

	return 0;
}

static void stats(struct mjpeg_source *src)
{
	src->count++;

	if (src->count % 50 == 0)
	{
		gettimeofday(src->tmp, NULL);

		long tmp1 = src->start->tv_sec * 1000L + src->start->tv_usec / 1000;
		long tmp2 = src->tmp->tv_sec * 1000L + src->tmp->tv_usec / 1000;
		double elapsedInSeconds = (tmp2 - tmp1) / 1000.0;

		// float now = clock();
		// float elapsedInSeconds = (now - start) / CLOCKS_PER_SEC * 1000;
		printf("FPS: %f, Mb/s: %f\n", src->count / elapsedInSeconds, (src->data / 1024.0 / 1024.0) / elapsedInSeconds);
		gettimeofday(src->start, NULL);
		src->count = 0;
		src->data = 0;
	}
}

static void readFrame(struct mjpeg_source *src, struct video_buffer *buf)
{
	while (true)
	{
		int read = fread(&src->jpegBuffer[src->jpegIndex], BUFFER_SIZE, 1, src->fd);
		if (read == 0)
		{
			return;
		}

		int eof = findEof(src->jpegBuffer, src->jpegIndex + BUFFER_SIZE, src->jpegIndex < 10 ? 0 : src->jpegIndex - 10);

		if (eof != 0)
		{
			memcpy(buf->mem, &src->jpegBuffer, eof + 1);
			buf->bytesused = eof + 1;

			src->data += eof + 1;
			int leftToCopy = (src->jpegIndex + BUFFER_SIZE) - (eof + 1);
			memcpy(&src->jpegBuffer[0], &src->jpegBuffer[eof + 1], leftToCopy);

			src->jpegIndex = leftToCopy;

			stats(src);
			return;
		}
		else
		{
			src->jpegIndex += BUFFER_SIZE;
		}
	}
}

static void setUp(struct mjpeg_source *src)
{
	printf("SetUp...\n");
	src->jpegBuffer = (char *)malloc(2 * 1024 * 1024);
	src->start = (struct timeval *)malloc(sizeof(struct timeval));
	src->tmp = (struct timeval *)malloc(sizeof(struct timeval));
	gettimeofday(src->start, NULL);
	fclose(fopen(src->mjpegPipeSignalPath, "w"));
	src->fd = fopen(src->mjpegPipeSignalPath, "rb");
	if (src->fd == NULL)
	{
		printf("Opening pipe failed!!!\n");
	}
}

static void mjpeg_source_fill_buffer(struct video_source *s,
									 struct video_buffer *buf)
{
	struct mjpeg_source *src = to_mjpeg_source(s);

	if (src->fd == NULL)
	{
		setUp(src);
	}
	readFrame(src, buf);
}

static const struct video_source_ops mjpeg_source_ops = {
	.destroy = mjpeg_source_destroy,
	.set_format = mjpeg_source_set_format,
	.set_frame_rate = mjpeg_source_set_frame_rate,
	.alloc_buffers = NULL,
	.export_buffers = NULL,
	.free_buffers = mjpeg_source_free_buffers,
	.stream_on = mjpeg_source_stream_on,
	.stream_off = mjpeg_source_stream_off,
	.queue_buffer = NULL,
	.fill_buffer = mjpeg_source_fill_buffer,
};

struct video_source *mjpeg_video_source_create(char *mjpegPipePath, char *mjpegSignalPipePath)
{
	struct mjpeg_source *src = malloc(sizeof *src);

	src = malloc(sizeof *src);
	if (!src)
		return NULL;

	memset(src, 0, sizeof *src);
	src->src.ops = &mjpeg_source_ops;

	src->mjpegPipePath = mjpegPipePath;
	src->mjpegPipeSignalPath = mjpegSignalPipePath;

	return &src->src;
}

void mjpeg_video_source_init(struct video_source *s, struct events *events)
{
	struct mjpeg_source *src = to_mjpeg_source(s);

	src->src.events = events;
}
