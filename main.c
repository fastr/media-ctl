/*
 * Media controller test application
 *
 * Copyright (C) 2010 Ideas on board SPRL <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 */

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/types.h>
#include <linux/media.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>

#include "media.h"
#include "options.h"
#include "subdev.h"

/* -----------------------------------------------------------------------------
 * Links setup
 */

static struct media_entity_pad *parse_pad(struct media_device *media, const char *p, char **endp)
{
	unsigned int entity_id, pad;
	struct media_entity *entity;
	char *end;

	for (; isspace(*p); ++p);

	if (*p == '"') {
		for (end = (char *)p + 1; *end && *end != '"'; ++end);
		if (*end != '"')
			return NULL;

		entity = media_get_entity_by_name(media, p + 1, end - p - 1);
		if (entity == NULL)
			return NULL;

		++end;
	} else {
		entity_id = strtoul(p, &end, 10);
		entity = media_get_entity_by_id(media, entity_id);
		if (entity == NULL)
			return NULL;
	}
	for (; isspace(*end); ++end);

	if (*end != ':')
		return NULL;
	for (p = end + 1; isspace(*p); ++p);

	pad = strtoul(p, &end, 10);
	for (p = end; isspace(*p); ++p);

	if (pad >= entity->info.pads)
		return NULL;

	for (p = end; isspace(*p); ++p);
	if (endp)
		*endp = (char *)p;

	return &entity->pads[pad];
}

static struct media_entity_link *parse_link(struct media_device *media, const char *p, char **endp)
{
	struct media_entity_link *link;
	struct media_entity_pad *source;
	struct media_entity_pad *sink;
	unsigned int i;
	char *end;

	source = parse_pad(media, p, &end);
	if (source == NULL)
		return NULL;

	if (end[0] != '-' || end[1] != '>')
		return NULL;
	p = end + 2;

	sink = parse_pad(media, p, &end);
	if (sink == NULL)
		return NULL;

	*endp = end;

	for (i = 0; i < source->entity->info.links; i++) {
		link = &source->entity->links[i];

		if (link->source == source && link->sink == sink)
			return link;
	}

	return NULL;
}

static int setup_link(struct media_device *media, const char *p, char **endp)
{
	struct media_entity_link *link;
	__u32 flags;
	char *end;

	link = parse_link(media, p, &end);
	if (link == NULL) {
		printf("Unable to parse link\n");
		return -EINVAL;
	}

	p = end;
	if (*p++ != '[') {
		printf("Unable to parse link flags\n");
		return -EINVAL;
	}

	flags = strtoul(p, &end, 10);
	for (p = end; isspace(*p); p++);
	if (*p++ != ']') {
		printf("Unable to parse link flags\n");
		return -EINVAL;
	}

	for (; isspace(*p); p++);
	*endp = (char *)p;

	printf("Setting up link %u:%u -> %u:%u [%u]\n",
		link->source->entity->info.id, link->source->index,
		link->sink->entity->info.id, link->sink->index,
		flags);

	return media_setup_link(media, link->source, link->sink, flags);
}

static int setup_links(struct media_device *media, const char *p)
{
	char *end;
	int ret;

	do {
		ret = setup_link(media, p, &end);
		if (ret < 0)
			return ret;

		p = end + 1;
	} while (*end == ',');

	return *end ? -EINVAL : 0;
}

/* -----------------------------------------------------------------------------
 * Formats setup
 */

static int parse_format(struct v4l2_mbus_framefmt *format, const char *p, char **endp)
{
	enum v4l2_mbus_pixelcode code;
	unsigned int width, height;
	char *end;

	for (; isspace(*p); ++p);
	for (end = (char *)p; !isspace(*end) && *end != '\0'; ++end);

	code = string_to_pixelcode(p, end - p);
	if (code == (enum v4l2_mbus_pixelcode)-1)
		return -EINVAL;

	for (p = end; isspace(*p); ++p);
	width = strtoul(p, &end, 10);
	if (*end != 'x')
		return -EINVAL;

	p = end + 1;
	height = strtoul(p, &end, 10);
	*endp = end;

	memset(format, 0, sizeof(*format));
	format->width = width;
	format->height = height;
	format->code = code;

	return 0;
}

static int parse_crop(struct v4l2_rect *crop, const char *p, char **endp)
{
	char *end;

	if (*p++ != '(')
		return -EINVAL;

	crop->left = strtoul(p, &end, 10);
	if (*end != ',')
		return -EINVAL;

	p = end + 1;
	crop->top = strtoul(p, &end, 10);
	if (*end++ != ')')
		return -EINVAL;
	if (*end != '/')
		return -EINVAL;

	p = end + 1;
	crop->width = strtoul(p, &end, 10);
	if (*end != 'x')
		return -EINVAL;

	p = end + 1;
	crop->height = strtoul(p, &end, 10);
	*endp = end;

	return 0;
}

static int parse_frame_interval(struct v4l2_fract *interval, const char *p, char **endp)
{
	char *end;

	for (; isspace(*p); ++p);

	interval->numerator = strtoul(p, &end, 10);

	for (p = end; isspace(*p); ++p);
	if (*p++ != '/')
		return -EINVAL;

	for (; isspace(*p); ++p);
	interval->denominator = strtoul(p, &end, 10);

	*endp = end;
	return 0;
}

static struct media_entity_pad *parse_pad_format(struct media_device *media,
	struct v4l2_mbus_framefmt *format, struct v4l2_rect *crop,
	struct v4l2_fract *interval, const char *p, char **endp)
{
	struct media_entity_pad *pad;
	char *end;
	int ret;

	for (; isspace(*p); ++p);

	pad = parse_pad(media, p, &end);
	if (pad == NULL)
		return NULL;

	for (p = end; isspace(*p); ++p);
	if (*p++ != '[')
		return NULL;

	if (isalnum(*p)) {
		ret = parse_format(format, p, &end);
		if (ret < 0)
			return NULL;

		for (p = end; isspace(*p); p++);
	}

	if (*p == '(') {
		ret = parse_crop(crop, p, &end);
		if (ret < 0)
			return NULL;

		for (p = end; isspace(*p); p++);
	}

	if (*p == '@') {
		ret = parse_frame_interval(interval, ++p, &end);
		if (ret < 0)
			return NULL;

		for (p = end; isspace(*p); p++);
	}

	if (*p != ']')
		return NULL;

	*endp = (char *)p + 1;
	return pad;
}

static int set_format(struct media_entity_pad *pad, struct v4l2_mbus_framefmt *format)
{
	int ret;

	if (format->width == 0 || format->height == 0)
		return 0;

	printf("Setting up format %s %ux%u on pad %s/%u\n",
		pixelcode_to_string(format->code), format->width, format->height,
		pad->entity->info.name, pad->index);

	ret = v4l2_subdev_set_format(pad->entity, format, pad->index,
				     V4L2_SUBDEV_FORMAT_ACTIVE);
	if (ret < 0) {
		printf("Unable to set format: %s (%d)\n", strerror(-ret), ret);
		return ret;
	}

	printf("Format set: %s %ux%u\n",
		pixelcode_to_string(format->code), format->width, format->height);

	return 0;
}

static int set_crop(struct media_entity_pad *pad, struct v4l2_rect *crop)
{
	int ret;

	if (crop->left == -1 || crop->top == -1)
		return 0;

	printf("Setting up crop rectangle (%u,%u)/%ux%u on pad %s/%u\n",
		crop->left, crop->top, crop->width, crop->height,
		pad->entity->info.name, pad->index);

	ret = v4l2_subdev_set_crop(pad->entity, crop, pad->index,
				   V4L2_SUBDEV_FORMAT_ACTIVE);
	if (ret < 0) {
		printf("Unable to set crop rectangle: %s (%d)", strerror(-ret), ret);
		return ret;
	}

	printf("Crop rectangle set: (%u,%u)/%ux%u\n",
		crop->left, crop->top, crop->width, crop->height);

	return 0;
}

static int set_frame_interval(struct media_entity *entity, struct v4l2_fract *interval)
{
	int ret;

	if (interval->numerator == 0)
		return 0;

	printf("Setting up frame interval %u/%u on entity %s\n",
		interval->numerator, interval->denominator, entity->info.name);

	ret = v4l2_subdev_set_frame_interval(entity, interval);
	if (ret < 0) {
		printf("Unable to set frame interval: %s (%d)", strerror(-ret), ret);
		return ret;
	}

	printf("Frame interval set: %u/%u\n",
		interval->numerator, interval->denominator);

	return 0;
}


static int setup_format(struct media_device *media, const char *p, char **endp)
{
	struct v4l2_mbus_framefmt format = { 0, 0, 0 };
	struct media_entity_pad *pad;
	struct v4l2_rect crop = { -1, -1, -1, -1 };
	struct v4l2_fract interval = { 0, 0 };
	unsigned int i;
	char *end;
	int ret;

	pad = parse_pad_format(media, &format, &crop, &interval, p, &end);
	if (pad == NULL) {
		printf("Unable to parse format\n");
		return -EINVAL;
	}

	if (pad->type == MEDIA_PAD_TYPE_OUTPUT) {
		ret = set_crop(pad, &crop);
		if (ret < 0)
			return ret;
	}

	ret = set_format(pad, &format);
	if (ret < 0)
		return ret;

	if (pad->type == MEDIA_PAD_TYPE_INPUT) {
		ret = set_crop(pad, &crop);
		if (ret < 0)
			return ret;
	}

	ret = set_frame_interval(pad->entity, &interval);
	if (ret < 0)
		return ret;


	/* If the pad is an output pad, automatically set the same format on
	 * the remote subdev input pads, if any.
	 */
	if (pad->type == MEDIA_PAD_TYPE_OUTPUT) {
		for (i = 0; i < pad->entity->info.links; ++i) {
			struct media_entity_link *link = &pad->entity->links[i];
			struct v4l2_mbus_framefmt remote_format;

			if (!(link->flags & MEDIA_LINK_FLAG_ACTIVE))
				continue;

			if (link->source == pad &&
			    link->sink->entity->info.type == MEDIA_ENTITY_TYPE_SUBDEV) {
				remote_format = format;
				set_format(link->sink, &remote_format);
			}
		}
	}

	*endp = end;
	return 0;
}

static int setup_formats(struct media_device *media, const char *p)
{
	char *end;
	int ret;

	do {
		ret = setup_format(media, p, &end);
		if (ret < 0)
			return ret;

		p = end + 1;
	} while (*end == ',');

	return *end ? -EINVAL : 0;
}

int main(int argc, char **argv)
{
	struct media_device *media;
	int ret = -1;

	if (parse_cmdline(argc, argv))
		return EXIT_FAILURE;

	/* Open the media device and enumerate entities, pads and links. */
	media = media_open(media_opts.devname, media_opts.verbose);
	if (media == NULL)
		goto out;

	if (media_opts.entity) {
		struct media_entity *entity;

		entity = media_get_entity_by_name(media, media_opts.entity,
						  strlen(media_opts.entity));
		if (entity == NULL) {
			printf("Entity '%s' not found\n", media_opts.entity);
			goto out;
		}

		printf("%s\n", entity->devname);
	}

	if (media_opts.pad) {
		struct media_entity_pad *pad;

		pad = parse_pad(media, media_opts.pad, NULL);
		if (pad == NULL) {
			printf("Pad '%s' not found\n", media_opts.pad);
			goto out;
		}

		v4l2_subdev_print_format(pad->entity, pad->index,
					 V4L2_SUBDEV_FORMAT_ACTIVE);
		printf("\n");
	}

	if (media_opts.print || media_opts.print_dot) {
		media_print_topology(media, media_opts.print_dot);
		printf("\n");
	}

	if (media_opts.reset) {
		printf("Resetting all links to inactive\n");
		media_reset_links(media);
	}

	if (media_opts.links)
		setup_links(media, media_opts.links);

	if (media_opts.formats)
		setup_formats(media, media_opts.formats);

	if (media_opts.interactive) {
		while (1) {
			char buffer[32];
			char *end;

			printf("Enter a link to modify or enter to stop\n");
			if (fgets(buffer, sizeof buffer, stdin) == NULL)
				break;

			if (buffer[0] == '\n')
				break;

			setup_link(media, buffer, &end);
		}
	}

	ret = 0;

out:
	if (media)
		media_close(media);

	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

