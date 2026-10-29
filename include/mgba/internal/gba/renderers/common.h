/* Copyright (c) 2013-2019 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef GBA_RENDERER_COMMON_H
#define GBA_RENDERER_COMMON_H

#include <mgba-util/common.h>

CXX_GUARD_START

#include <mgba/internal/gba/video.h>

struct GBAVideoRendererSprite {
	struct GBAObj obj;
	int16_t y;
	int16_t endY;
	int16_t cycles;
	int8_t index;
};

int GBAVideoRendererCleanOAM(struct GBAObj* oam, struct GBAVideoRendererSprite* sprites, int offsetY);

/**
 * Like GBAVideoRendererCleanOAM, but also keeps the sprites that are only visible in [wideLeft] / [wideRight]
 * extra columns beside the GBA's screen. Those cost no sprite cycles: the hardware never drew them.
 */
int GBAVideoRendererCleanOAMWide(struct GBAObj* oam, struct GBAVideoRendererSprite* sprites, int offsetY, int wideLeft, int wideRight);

/**
 * A sprite's 9-bit X as a screen position. Values from 240 up to the right margin's end are positions to the
 * right of the screen (so sprites parked there show up in the margin), the rest of the upper half wraps to
 * negative as on the GBA. With no right margin this is exactly the GBA's signed 9-bit value.
 */
static inline int GBAVideoRendererObjX(int rawX, int wideRight) {
	int cutoff = GBA_VIDEO_HORIZONTAL_PIXELS + wideRight;
	if (cutoff < 256) {
		cutoff = 256;
	}
	return rawX >= cutoff ? rawX - 512 : rawX;
}

CXX_GUARD_END

#endif