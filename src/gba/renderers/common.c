/* Copyright (c) 2013-2019 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/renderers/common.h>

#include <mgba/gba/interface.h>

/**
 * The sprites that can be seen, in OAM order, with what they cost in sprite cycles. With [wide] set, sprites that only show in
 * the [wideLeft] / [wideRight] extra columns beside the GBA's screen are kept too, at no cost: the hardware never drew them.
 * Without it this is exactly the GBA's own list.
 */
static int _cleanOAM(struct GBAObj* oam, struct GBAVideoRendererSprite* sprites, int offsetY, bool wide, int wideLeft, int wideRight) {
	int i;
	int oamMax = 0;
	for (i = 0; i < 128; ++i) {
		struct GBAObj obj;
		LOAD_16LE(obj.a, 0, &oam[i].a);
		LOAD_16LE(obj.b, 0, &oam[i].b);
		LOAD_16LE(obj.c, 0, &oam[i].c);
		if (GBAObjAttributesAIsTransformed(obj.a) || !GBAObjAttributesAIsDisable(obj.a)) {
			int width = GBAVideoObjSizes[GBAObjAttributesAGetShape(obj.a) * 4 + GBAObjAttributesBGetSize(obj.b)][0];
			int height = GBAVideoObjSizes[GBAObjAttributesAGetShape(obj.a) * 4 + GBAObjAttributesBGetSize(obj.b)][1];
			int32_t x = (uint32_t) GBAObjAttributesBGetX(obj.b) << 23;
			x >>= 23;
			int cycles;
			bool culled = false; // not on the GBA's own screen
			if (GBAObjAttributesAIsTransformed(obj.a)) {
				height <<= GBAObjAttributesAGetDoubleSize(obj.a);
				width <<= GBAObjAttributesAGetDoubleSize(obj.a);
				cycles = 8 + width * 2;
				if (x < 0) {
					cycles += x;
				}
			} else {
				cycles = width - 2;
				if (x < 0) {
					if (x + width < 0) {
						culled = true;
					} else {
						cycles += x >> 1;
					}
				}
			}
			if (GBAObjAttributesAGetY(obj.a) >= GBA_VIDEO_VERTICAL_PIXELS && GBAObjAttributesAGetY(obj.a) + height < VIDEO_VERTICAL_TOTAL_PIXELS) {
				continue;
			}
			if (GBAObjAttributesBGetX(obj.b) >= GBA_VIDEO_HORIZONTAL_PIXELS && GBAObjAttributesBGetX(obj.b) + width < 512) {
				culled = true;
			}
			if (culled) {
				int wideX = GBAVideoRendererObjX(GBAObjAttributesBGetX(obj.b), wideRight);
				if (!wide || wideX + width <= -wideLeft || wideX >= GBA_VIDEO_HORIZONTAL_PIXELS + wideRight) {
					continue;
				}
				cycles = 0;
			}
			int y = GBAObjAttributesAGetY(obj.a) + offsetY;
			if (y + height > 256) {
				y -= 256;
			}
			sprites[oamMax].y = y;
			sprites[oamMax].endY = y + height;
			sprites[oamMax].cycles = cycles;
			sprites[oamMax].obj = obj;
			sprites[oamMax].index = i;
			++oamMax;
		}
	}
	return oamMax;
}

int GBAVideoRendererCleanOAM(struct GBAObj* oam, struct GBAVideoRendererSprite* sprites, int offsetY) {
	return _cleanOAM(oam, sprites, offsetY, false, 0, 0);
}

int GBAVideoRendererCleanOAMWide(struct GBAObj* oam, struct GBAVideoRendererSprite* sprites, int offsetY, int wideLeft, int wideRight) {
	return _cleanOAM(oam, sprites, offsetY, true, wideLeft, wideRight);
}
