#include "scene.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rngState;

static uint32_t rnd(void) {
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

void sceneBlank(struct Scene* scene, int mode) {
	memset(scene, 0, sizeof(*scene));
	scene->io[0] = mode;
}

void sceneRandom(struct Scene* scene, uint32_t seed, int mode) {
	rngState = seed * 2654435761u + mode * 40503u + 1;
	int i;
	for (i = 0; i < 8; ++i) {
		rnd();
	}
	memset(scene, 0, sizeof(*scene));
	for (i = 0; i < 0xC000; ++i) {
		scene->vram[i] = rnd();
	}
	for (i = 0; i < 512; ++i) {
		scene->palette[i] = rnd() & 0x7FFF;
	}
	for (i = 0; i < 128; ++i) {
		uint16_t y = rnd() % 176;
		scene->oam.obj[i].a = (y & 0xFF) | ((rnd() & 3) << 8) | ((rnd() & 0xF) << 10) | ((rnd() & 3) << 14);
		scene->oam.obj[i].b = (rnd() % 512) | ((rnd() & 0x1F) << 9) | ((rnd() & 3) << 14);
		scene->oam.obj[i].c = rnd() & 0xFFFF;
	}
	for (i = 0; i < 32; ++i) {
		scene->oam.mat[i].a = (int16_t) ((rnd() & 0x3FF) - 0x200);
		scene->oam.mat[i].b = (int16_t) ((rnd() & 0x3FF) - 0x200);
		scene->oam.mat[i].c = (int16_t) ((rnd() & 0x3FF) - 0x200);
		scene->oam.mat[i].d = (int16_t) ((rnd() & 0x3FF) - 0x200);
	}

	uint16_t* io = scene->io;
	uint16_t layers = (rnd() & 0x1F) | 0x0F; // bg0-3 always on, obj mostly
	io[0x00 >> 1] = mode | ((rnd() & 1) << 4) | ((rnd() & 1) << 5) | ((rnd() & 1) << 6) |
	                (layers << 8) | ((rnd() & 7) << 13);
	for (i = 0x08; i <= 0x0E; i += 2) {
		io[i >> 1] = rnd();
	}
	for (i = 0x10; i <= 0x1E; i += 2) {
		io[i >> 1] = rnd() & 0x1FF;
	}
	for (i = 0x20; i <= 0x3E; i += 2) {
		// BG2PA, PB, PC, PD, X lo, X hi, Y lo, Y hi, then BG3's: matrix small and signed, reference points 28-bit
		int reg = (i - 0x20) & 0xF;
		if (reg < 8) {
			io[i >> 1] = (rnd() & 0x3FF) - 0x200;
		} else if (reg == 8 || reg == 0xA) {
			io[i >> 1] = rnd();
		} else {
			io[i >> 1] = rnd() & 0x0FFF;
		}
	}
	for (i = 0x40; i <= 0x46; i += 2) {
		io[i >> 1] = rnd();
	}
	io[0x48 >> 1] = rnd();
	io[0x4A >> 1] = rnd();
	io[0x4C >> 1] = rnd();
	io[0x50 >> 1] = rnd();
	io[0x52 >> 1] = rnd() & 0x1F1F;
	io[0x54 >> 1] = rnd() & 0x1F;
}

void sceneOnly(struct Scene* scene, uint16_t enable) {
	scene->io[0] = (scene->io[0] & 0x00FF) | (enable & 0xFF00);
}

void renderScene(const struct Scene* scene, int wideLeft, int wideRight, uint32_t* out) {
	static struct GBAVideoSoftwareRenderer renderer;
	static uint32_t buffer[SCENE_MAX_WIDTH * SCENE_HEIGHT];
	static struct Scene copy;
	copy = *scene;

	memset(&renderer, 0, sizeof(renderer));
	GBAVideoSoftwareRendererCreate(&renderer);
	renderer.d.vram = copy.vram;
	renderer.d.palette = copy.palette;
	renderer.d.oam = &copy.oam;
	renderer.d.cache = NULL;
	int width = GBA_VIDEO_HORIZONTAL_PIXELS + wideLeft + wideRight;
	renderer.outputBuffer = buffer;
	renderer.outputBufferStride = width;
	memset(buffer, 0, sizeof(buffer));

	renderer.d.init(&renderer.d);
	GBAVideoSoftwareRendererSetWideMargins(&renderer, wideLeft, wideRight);

	static const int addresses[] = {
		0x00, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E,
		0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E,
		0x40, 0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x50, 0x52, 0x54,
	};
	size_t i;
	for (i = 0; i < sizeof(addresses) / sizeof(*addresses); ++i) {
		renderer.d.writeVideoRegister(&renderer.d, addresses[i], copy.io[addresses[i] >> 1]);
	}
	int y;
	for (y = 0; y < SCENE_HEIGHT; ++y) {
		renderer.d.drawScanline(&renderer.d, y);
	}
	renderer.d.finishFrame(&renderer.d);
	memcpy(out, buffer, (size_t) width * SCENE_HEIGHT * sizeof(uint32_t));
	renderer.d.deinit(&renderer.d);
}
