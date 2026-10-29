#ifndef WIDE_TEST_SCENE_H
#define WIDE_TEST_SCENE_H

#include <mgba/internal/gba/renderers/video-software.h>

#define SCENE_HEIGHT 160
#define SCENE_MAX_WIDTH 384

struct Scene {
	uint16_t vram[0xC000];
	uint16_t palette[512];
	union GBAOAM oam;
	uint16_t io[0x30]; // registers 0x00..0x5F, in 16-bit units
};

/** Fills [scene] with a deterministic pseudo-random picture for [seed]; [mode] is the BG mode (0..5). */
void sceneRandom(struct Scene* scene, uint32_t seed, int mode);

/** All-zero scene in [mode] (backdrop black); for hand-built tests. Registers are set through scene->io. */
void sceneBlank(struct Scene* scene, int mode);

/** Keeps only the DISPCNT layer/window bits in [enable] (bits 8..15: BG0-3, OBJ, WIN0, WIN1, OBJWIN). */
void sceneOnly(struct Scene* scene, uint16_t enable);

/** Draws [scene] into [out] ((240 + left + right) x 160 pixels, packed rows). */
void renderScene(const struct Scene* scene, int wideLeft, int wideRight, uint32_t* out);

#endif
