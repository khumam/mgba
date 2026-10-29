#include "scene.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, ...) \
	do { \
		++checks; \
		if (!(cond)) { \
			++failures; \
			printf("FAIL %s:%d: ", __FILE__, __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

#define NARROW (GBA_VIDEO_HORIZONTAL_PIXELS * SCENE_HEIGHT)

static struct Scene scene;
static uint32_t a[SCENE_MAX_WIDTH * SCENE_HEIGHT];
static uint32_t b[SCENE_MAX_WIDTH * SCENE_HEIGHT];

#define GOLDEN_SEEDS 20
#define GOLDEN_FILE GOLDEN_DIR "/stock.txt"

static uint64_t fnv1a(const uint32_t* pixels, size_t count) {
	uint64_t hash = 1469598103934665603ULL;
	size_t i;
	for (i = 0; i < count; ++i) {
		int shift;
		for (shift = 0; shift < 32; shift += 8) {
			hash = (hash ^ ((pixels[i] >> shift) & 0xFF)) * 1099511628211ULL;
		}
	}
	return hash;
}

/** Hash of stock mGBA's render for [mode]/[seed], recorded by --write-golden before the renderer was touched. */
static int goldenHash(int mode, int seed, uint64_t* hash) {
	FILE* f = fopen(GOLDEN_FILE, "r");
	if (!f) {
		return 0;
	}
	int m, s;
	unsigned long long h;
	int found = 0;
	while (fscanf(f, "%d %d %llx", &m, &s, &h) == 3) {
		if (m == mode && s == seed) {
			*hash = h;
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

static void writeGoldens(void) {
	FILE* f = fopen(GOLDEN_FILE, "w");
	if (!f) {
		printf("cannot write %s\n", GOLDEN_FILE);
		exit(2);
	}
	int mode, seed;
	for (mode = 0; mode <= 5; ++mode) {
		for (seed = 1; seed <= GOLDEN_SEEDS; ++seed) {
			sceneRandom(&scene, seed, mode);
			renderScene(&scene, 0, 0, a);
			fprintf(f, "%d %d %016llx\n", mode, seed, (unsigned long long) fnv1a(a, NARROW));
		}
	}
	fclose(f);
	printf("wrote %d goldens\n", 6 * GOLDEN_SEEDS);
}

static void narrowRenderIsDeterministic(void) {
	int mode, seed;
	for (mode = 0; mode <= 5; ++mode) {
		int busy = 0;
		for (seed = 1; seed <= 50; ++seed) {
			sceneRandom(&scene, seed, mode);
			renderScene(&scene, 0, 0, a);
			renderScene(&scene, 0, 0, b);
			CHECK(memcmp(a, b, NARROW * sizeof(uint32_t)) == 0, "mode %d seed %d differs between runs", mode, seed);
			int i;
			for (i = 1; i < NARROW; ++i) {
				if (a[i] != a[0]) {
					++busy;
					break;
				}
			}
		}
		CHECK(busy > 20, "mode %d: only %d of 50 scenes drew more than one colour", mode, busy);
	}
}

static void zeroMarginsMatchStock(void) {
	int mode, seed;
	for (mode = 0; mode <= 5; ++mode) {
		for (seed = 1; seed <= GOLDEN_SEEDS; ++seed) {
			uint64_t expected;
			CHECK(goldenHash(mode, seed, &expected), "no golden for mode %d seed %d", mode, seed);
			sceneRandom(&scene, seed, mode);
			renderScene(&scene, 0, 0, a);
			CHECK(fnv1a(a, NARROW) == expected, "mode %d seed %d differs from stock mGBA", mode, seed);
		}
	}
}

static void createStartsWithoutMargins(void) {
	static struct GBAVideoSoftwareRenderer renderer;
	memset(&renderer, 0xAA, sizeof(renderer)); // the core allocates its renderer with malloc
	GBAVideoSoftwareRendererCreate(&renderer);
	CHECK(renderer.wideLeft == 0 && renderer.wideRight == 0, "no margins until asked for, got %d/%d", renderer.wideLeft, renderer.wideRight);
}

static void setMarginsClamps(void) {
	static struct GBAVideoSoftwareRenderer renderer;
	GBAVideoSoftwareRendererCreate(&renderer);
	GBAVideoSoftwareRendererSetWideMargins(&renderer, 100, -5);
	CHECK(renderer.wideLeft == 72, "left clamps to 72, got %d", renderer.wideLeft);
	CHECK(renderer.wideRight == 0, "right clamps to 0, got %d", renderer.wideRight);
}

#define LAYER_BG0 0x0100
#define LAYER_BG1 0x0200
#define LAYER_BG2 0x0400
#define LAYER_BG3 0x0800

static const int MARGIN_SETS[][2] = { { 40, 40 }, { 72, 72 }, { 22, 22 }, { 1, 1 }, { 0, 40 }, { 40, 0 } };
#define N_MARGIN_SETS (int) (sizeof(MARGIN_SETS) / sizeof(*MARGIN_SETS))

/** Copies the GBA's own 240 columns out of a [left] + 240 + [right] wide render. */
static void centreOf(const uint32_t* wide, int left, int right, uint32_t* out) {
	int width = GBA_VIDEO_HORIZONTAL_PIXELS + left + right;
	int y;
	for (y = 0; y < SCENE_HEIGHT; ++y) {
		memcpy(&out[y * GBA_VIDEO_HORIZONTAL_PIXELS], &wide[y * width + left], GBA_VIDEO_HORIZONTAL_PIXELS * sizeof(uint32_t));
	}
}

/** Checks the GBA's own columns of a margins render of [scene] against the narrow render. */
static void checkCentre(const struct Scene* scene, int left, int right, const char* what, int mode, int seed) {
	static uint32_t narrow[NARROW];
	static uint32_t centre[NARROW];
	renderScene(scene, 0, 0, narrow);
	renderScene(scene, left, right, b);
	centreOf(b, left, right, centre);
	CHECK(memcmp(narrow, centre, sizeof(narrow)) == 0, "%s: mode %d seed %d margins %d/%d: centre differs from stock", what, mode, seed, left, right);
}

static void shiftBgScroll(struct Scene* s, int dx) {
	int i;
	for (i = 0x10; i <= 0x1C; i += 4) {
		s->io[i >> 1] = (s->io[i >> 1] + dx) & 0x1FF;
	}
}

/** Columns [from, from + count) of a [width]-wide render, row [y]. */
static const uint32_t* columnsAt(const uint32_t* render, int width, int y, int from) {
	return &render[y * width + from];
}

static void textBgCentreUnchanged(void) {
	int seed, m;
	for (seed = 1; seed <= 100; ++seed) {
		for (m = 0; m < N_MARGIN_SETS; ++m) {
			sceneRandom(&scene, seed, 0);
			sceneOnly(&scene, LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3);
			checkCentre(&scene, MARGIN_SETS[m][0], MARGIN_SETS[m][1], "text bg", 0, seed);
			sceneRandom(&scene, seed, 1);
			sceneOnly(&scene, LAYER_BG0 | LAYER_BG1);
			checkCentre(&scene, MARGIN_SETS[m][0], MARGIN_SETS[m][1], "text bg", 1, seed);
		}
	}
}

/**
 * The left margin is what the picture would be if the screen had scrolled left; the right, if it had scrolled right.
 * Stock mGBA draws the first and last partial tile of a span through paths that treat a tile number past the end
 * of VRAM differently, so with scroll not on a tile boundary the first 8 columns of each side are left out.
 */
static void textBgMarginsMatchShiftedNarrow(void) {
	static uint32_t shifted[NARROW];
	int seed, y, aligned, left = 40, right = 40, width = 240 + 80;
	for (aligned = 0; aligned <= 1; ++aligned) {
		int skip = aligned ? 0 : 8;
		for (seed = 1; seed <= 100; ++seed) {
			sceneRandom(&scene, seed, 0);
			sceneOnly(&scene, LAYER_BG0 | LAYER_BG1 | LAYER_BG2 | LAYER_BG3);
			scene.io[0x4C >> 1] = 0; // no mosaic: its blocks are anchored, so a scroll isn't the same picture
			if (aligned) {
				int i;
				for (i = 0x10; i <= 0x1C; i += 4) {
					scene.io[i >> 1] &= ~7;
				}
			}
			renderScene(&scene, left, right, b);

			struct Scene moved = scene;
			shiftBgScroll(&moved, -left);
			renderScene(&moved, 0, 0, shifted);
			for (y = 0; y < SCENE_HEIGHT; ++y) {
				CHECK(memcmp(columnsAt(b, width, y, skip), columnsAt(shifted, 240, y, skip), (left - skip) * sizeof(uint32_t)) == 0,
				      "aligned %d seed %d row %d: left margin isn't the scrolled-left picture", aligned, seed, y);
			}
			moved = scene;
			shiftBgScroll(&moved, 240);
			renderScene(&moved, 0, 0, shifted);
			for (y = 0; y < SCENE_HEIGHT; ++y) {
				CHECK(memcmp(columnsAt(b, width, y, left + 240 + skip), columnsAt(shifted, 240, y, skip), (right - skip) * sizeof(uint32_t)) == 0,
				      "aligned %d seed %d row %d: right margin isn't the scrolled-right picture", aligned, seed, y);
			}
		}
	}
}

static void textBgMosaicAnchoredAtGbaZero(void) {
	static uint32_t shifted[NARROW];
	int seed, y;
	for (seed = 1; seed <= 30; ++seed) {
		sceneRandom(&scene, seed, 0);
		sceneOnly(&scene, LAYER_BG0);
		scene.io[0x4C >> 1] = 0x0003; // BG mosaic 4 wide, 1 tall
		scene.io[0x08 >> 1] |= 0x0040; // BG0 uses mosaic
		scene.io[0x10 >> 1] = 0;
		scene.io[0x12 >> 1] = 0;
		scene.io[0x50 >> 1] = 0; // no blending
		renderScene(&scene, 2, 2, b);
		renderScene(&scene, 0, 0, a);
		struct Scene moved = scene;
		moved.io[0x10 >> 1] = (0 - 4) & 0x1FF;
		renderScene(&moved, 0, 0, shifted);
		for (y = 0; y < SCENE_HEIGHT; y += 4) {
			const uint32_t* row = columnsAt(b, 244, y, 0);
			CHECK(row[0] == row[1], "seed %d row %d: x -2 and -1 are in one block", seed, y);
			CHECK(row[0] == shifted[y * 240], "seed %d row %d: block [-4,-1] shows the picture at x -4", seed, y);
			CHECK(row[2] == row[3] && row[3] == row[4] && row[4] == row[5], "seed %d row %d: block [0,3] is even", seed, y);
			CHECK(row[2] == a[y * 240], "seed %d row %d: block [0,3] matches the stock picture", seed, y);
		}
	}
}

static int32_t refPoint(const struct Scene* s, int regLo) {
	int32_t v = (s->io[regLo >> 1] | (s->io[(regLo + 2) >> 1] << 16)) & 0x0FFFFFFF;
	return (v << 4) >> 4; // 28-bit signed
}

static void setRefPoint(struct Scene* s, int regLo, int32_t v) {
	s->io[regLo >> 1] = v & 0xFFFF;
	s->io[(regLo + 2) >> 1] = (v >> 16) & 0x0FFF;
}

static void affineCentreUnchanged(void) {
	int seed, m;
	for (seed = 1; seed <= 100; ++seed) {
		for (m = 0; m < N_MARGIN_SETS; ++m) {
			sceneRandom(&scene, seed, 1);
			sceneOnly(&scene, LAYER_BG2);
			checkCentre(&scene, MARGIN_SETS[m][0], MARGIN_SETS[m][1], "affine bg", 1, seed);
			sceneRandom(&scene, seed, 2);
			sceneOnly(&scene, LAYER_BG2 | LAYER_BG3);
			checkCentre(&scene, MARGIN_SETS[m][0], MARGIN_SETS[m][1], "affine bg", 2, seed);
		}
	}
}

static void bitmapCentreUnchanged(void) {
	int seed, m, mode;
	for (seed = 1; seed <= 100; ++seed) {
		for (m = 0; m < N_MARGIN_SETS; ++m) {
			for (mode = 3; mode <= 5; ++mode) {
				sceneRandom(&scene, seed, mode);
				sceneOnly(&scene, LAYER_BG2);
				checkCentre(&scene, MARGIN_SETS[m][0], MARGIN_SETS[m][1], "bitmap bg", mode, seed);
			}
		}
	}
}

/** The margins carry the rotation/scaling matrix on past the screen's edges: same as moving the reference point. */
static void affineMarginsMatchMovedReference(void) {
	static uint32_t moved[NARROW];
	int seed, y, mode, bg, left = 40, right = 40, width = 240 + 80;
	for (mode = 1; mode <= 2; ++mode) {
		for (bg = (mode == 1 ? 2 : 2); bg <= (mode == 1 ? 2 : 3); ++bg) {
			int base = bg == 2 ? 0x20 : 0x30; // BGxPA; X at +8, Y at +12
			uint16_t layer = bg == 2 ? LAYER_BG2 : LAYER_BG3;
			for (seed = 1; seed <= 100; ++seed) {
				sceneRandom(&scene, seed, mode);
				sceneOnly(&scene, layer);
				scene.io[0x4C >> 1] = 0;
				scene.io[0x50 >> 1] = 0;
				renderScene(&scene, left, right, b);
				int32_t pa = (int16_t) scene.io[base >> 1];
				int32_t pc = (int16_t) scene.io[(base + 4) >> 1];
				int32_t x0 = refPoint(&scene, base + 8);
				int32_t y0 = refPoint(&scene, base + 12);

				struct Scene s2 = scene;
				setRefPoint(&s2, base + 8, x0 - left * pa);
				setRefPoint(&s2, base + 12, y0 - left * pc);
				renderScene(&s2, 0, 0, moved);
				for (y = 0; y < SCENE_HEIGHT; ++y) {
					CHECK(memcmp(columnsAt(b, width, y, 0), columnsAt(moved, 240, y, 0), left * sizeof(uint32_t)) == 0,
					      "mode %d bg%d seed %d row %d: left margin isn't the matrix continued", mode, bg, seed, y);
				}
				s2 = scene;
				setRefPoint(&s2, base + 8, x0 + 240 * pa);
				setRefPoint(&s2, base + 12, y0 + 240 * pc);
				renderScene(&s2, 0, 0, moved);
				for (y = 0; y < SCENE_HEIGHT; ++y) {
					CHECK(memcmp(columnsAt(b, width, y, left + 240), columnsAt(moved, 240, y, 0), right * sizeof(uint32_t)) == 0,
					      "mode %d bg%d seed %d row %d: right margin isn't the matrix continued", mode, bg, seed, y);
				}
			}
		}
	}
}

/** The bitmap modes only have a picture on the GBA's columns: the margins show the backdrop. */
static void bitmapMarginsAreBackdrop(void) {
	static uint32_t backdrop[NARROW];
	int seed, y, x, mode, left = 40, right = 40, width = 240 + 80;
	for (mode = 3; mode <= 5; ++mode) {
		for (seed = 1; seed <= 50; ++seed) {
			sceneRandom(&scene, seed, mode);
			sceneOnly(&scene, LAYER_BG2);
			scene.io[0x20 >> 1] = 0x100; // identity matrix
			scene.io[0x22 >> 1] = 0;
			scene.io[0x24 >> 1] = 0;
			scene.io[0x26 >> 1] = 0x100;
			setRefPoint(&scene, 0x28, 0);
			setRefPoint(&scene, 0x2C, 0);
			scene.io[0x4C >> 1] = 0;
			scene.io[0x50 >> 1] = 0;
			renderScene(&scene, left, right, b);
			struct Scene empty = scene;
			sceneOnly(&empty, 0);
			renderScene(&empty, 0, 0, backdrop);
			for (y = 0; y < SCENE_HEIGHT; ++y) {
				for (x = 0; x < left; ++x) {
					CHECK(b[y * width + x] == backdrop[0], "mode %d seed %d (%d,%d): left margin isn't the backdrop", mode, seed, x, y);
				}
				for (x = 0; x < right; ++x) {
					CHECK(b[y * width + left + 240 + x] == backdrop[0], "mode %d seed %d (%d,%d): right margin isn't the backdrop", mode, seed, x, y);
				}
			}
		}
	}
}

#define LAYER_OBJ 0x1000

static void spriteSetup(struct Scene* s) {
	int i;
	sceneBlank(s, 0);
	s->io[0] = 0x1040; // mode 0, sprites on, one-dimensional tile layout
	for (i = 0; i < 128; ++i) {
		s->oam.obj[i].a = 0x0200; // disabled
	}
	for (i = 0; i < 0x1000; ++i) {
		s->vram[0x8000 + i] = 0x1111; // sprite tiles: palette entry 1 everywhere
	}
	s->palette[0x100 + 1] = 0x7FFF;
}

static void setSprite(struct Scene* s, int i, int y, int rawX, int shape, int size, int doubleSize) {
	s->oam.obj[i].a = (y & 0xFF) | (doubleSize ? 0x0300 : 0) | (shape << 14);
	s->oam.obj[i].b = (rawX & 0x1FF) | (size << 14);
	s->oam.obj[i].c = 0;
	if (doubleSize) {
		s->oam.mat[0].a = 0x100;
		s->oam.mat[0].b = 0;
		s->oam.mat[0].c = 0;
		s->oam.mat[0].d = 0x100;
	}
}

/** First and last internal column in rows [y0, y1) holding something other than [backdrop]; -1 for none. */
static void drawnSpan(const uint32_t* render, int width, int y0, int y1, uint32_t backdrop, int* first, int* last) {
	int x, y;
	*first = *last = -1;
	for (y = y0; y < y1; ++y) {
		for (x = 0; x < width; ++x) {
			if (render[y * width + x] != backdrop) {
				if (*first < 0 || x < *first) *first = x;
				if (x > *last) *last = x;
			}
		}
	}
}

static void spritesCentreUnchanged(void) {
	int seed, m, mode;
	for (seed = 1; seed <= 100; ++seed) {
		for (m = 0; m < N_MARGIN_SETS; ++m) {
			for (mode = 0; mode <= 5; ++mode) {
				sceneRandom(&scene, seed, mode);
				sceneOnly(&scene, LAYER_OBJ);
				checkCentre(&scene, MARGIN_SETS[m][0], MARGIN_SETS[m][1], "sprites", mode, seed);
			}
		}
	}
}

static void spriteHalfOffLeftDrawsInMargin(void) {
	int first, last;
	spriteSetup(&scene);
	setSprite(&scene, 0, 20, 504, 0, 1, 0); // 16x16, x = -8
	renderScene(&scene, 40, 40, b);
	drawnSpan(b, 320, 20, 36, b[0], &first, &last);
	CHECK(first == 32 && last == 47, "x -8 sprite spans internal columns 32..47, got %d..%d", first, last);
	drawnSpan(b, 320, 0, 20, b[0], &first, &last);
	CHECK(first == -1, "nothing above the sprite, got %d..%d", first, last);
}

static void spriteParkedAt240DrawsInRightMargin(void) {
	int first, last;
	spriteSetup(&scene);
	setSprite(&scene, 0, 20, 240, 0, 1, 0);
	renderScene(&scene, 40, 40, b);
	drawnSpan(b, 320, 20, 36, b[0], &first, &last);
	CHECK(first == 280 && last == 295, "parked sprite spans internal columns 280..295, got %d..%d", first, last);
	renderScene(&scene, 0, 0, a);
	drawnSpan(a, 240, 0, 160, a[0], &first, &last);
	CHECK(first == -1, "without margins it is not seen, got %d..%d", first, last);
}

static void doubleSizeAffineSpriteAtMaxMargins(void) {
	int first, last;
	spriteSetup(&scene);
	setSprite(&scene, 0, 0, 250, 0, 3, 1); // 64x64 drawn in a 128x128 box; its picture sits 32 in
	renderScene(&scene, 72, 72, b);
	drawnSpan(b, 384, 32, 96, b[0], &first, &last);
	CHECK(first == 72 + 282 && last == 383, "picture at x 282.. fills to the right edge, got %d..%d", first, last);
	setSprite(&scene, 0, 0, 312, 0, 3, 1); // first value read as negative: x = -200
	renderScene(&scene, 72, 72, b);
	drawnSpan(b, 384, 0, 160, b[0], &first, &last);
	CHECK(first == -1, "x -200 reaches no visible column, got %d..%d", first, last);
}

static void marginSpritesDoNotConsumeCycles(void) {
	int i, first, last;
	spriteSetup(&scene);
	for (i = 0; i < 100; ++i) {
		setSprite(&scene, i, 0, 260, 0, 3, 0); // 64 wide, only in the right margin
	}
	setSprite(&scene, 100, 10, 100, 0, 0, 0); // 8x8 in the middle
	renderScene(&scene, 40, 40, b);
	drawnSpan(b, 320, 10, 18, b[0], &first, &last);
	CHECK(first >= 0 && last >= 0, "the sprite in the middle still draws");
	CHECK(first == 140, "it starts at internal column 140, got %d", first);
	CHECK(b[10 * 320 + 147] != b[0] && b[10 * 320 + 148] == b[0], "and is 8 wide");
}

/** BG0 (priority 0, solid palette 1) over BG1 (solid palette 2), WIN0 on the whole height; WININ shows only BG1. */
static void windowSetup(struct Scene* s, uint16_t win0h) {
	int i;
	sceneBlank(s, 0);
	s->io[0] = 0x0300 | 0x2000;
	s->io[0x08 >> 1] = 0x0800; // BG0: screen block 8, priority 0
	s->io[0x0A >> 1] = 0x0901; // BG1: screen block 9, priority 1
	for (i = 0; i < 16; ++i) {
		s->vram[16 + i] = 0x1111; // tile 1
		s->vram[32 + i] = 0x2222; // tile 2
	}
	for (i = 0; i < 0x400; ++i) {
		s->vram[0x2000 + i] = 1;
		s->vram[0x2400 + i] = 2;
	}
	s->palette[1] = 0x001F;
	s->palette[2] = 0x03E0;
	s->io[0x40 >> 1] = win0h;
	s->io[0x44 >> 1] = 0x00A0; // rows 0..159
	s->io[0x48 >> 1] = 0x0002; // inside WIN0: BG1 only
	s->io[0x4A >> 1] = 0x0001; // outside: BG0 only
}

/** Colours of BG0 alone and of BG1 alone, for the window tests to compare against. */
static void windowColours(uint32_t* onlyBg0, uint32_t* onlyBg1) {
	struct Scene ref;
	windowSetup(&ref, 0);
	sceneOnly(&ref, LAYER_BG0);
	renderScene(&ref, 0, 0, a);
	*onlyBg0 = a[80 * 240 + 100];
	sceneOnly(&ref, LAYER_BG1);
	renderScene(&ref, 0, 0, a);
	*onlyBg1 = a[80 * 240 + 100];
}

static void windowTouchingEdgesExtends(void) {
	uint32_t c0, c1;
	windowColours(&c0, &c1);
	windowSetup(&scene, (0 << 8) | 240);
	renderScene(&scene, 40, 40, b);
	CHECK(b[80 * 320 + 0] == c1 && b[80 * 320 + 39] == c1, "left margin is inside a window that starts at 0");
	CHECK(b[80 * 320 + 280] == c1 && b[80 * 320 + 319] == c1, "right margin is inside a window that ends at 240");
	CHECK(b[80 * 320 + 160] == c1, "and so is the middle");
}

static void windowInsideStaysInside(void) {
	uint32_t c0, c1;
	windowColours(&c0, &c1);
	windowSetup(&scene, (40 << 8) | 200);
	renderScene(&scene, 40, 40, b);
	CHECK(b[80 * 320 + 0] == c0 && b[80 * 320 + 39] == c0, "left margin is outside a window that starts at 40");
	CHECK(b[80 * 320 + 280] == c0 && b[80 * 320 + 319] == c0, "right margin is outside a window that ends at 200");
	CHECK(b[80 * 320 + 40 + 39] == c0 && b[80 * 320 + 40 + 40] == c1, "its left edge is still at x 40");
	CHECK(b[80 * 320 + 40 + 199] == c1 && b[80 * 320 + 40 + 200] == c0, "its right edge is still at x 200");
}

static void wrappedWindowCoversBothMargins(void) {
	uint32_t c0, c1;
	windowColours(&c0, &c1);
	windowSetup(&scene, (200 << 8) | 40);
	renderScene(&scene, 40, 40, b);
	CHECK(b[80 * 320 + 0] == c1 && b[80 * 320 + 39] == c1, "left margin is inside a wrapped window");
	CHECK(b[80 * 320 + 280] == c1 && b[80 * 320 + 319] == c1, "right margin is inside a wrapped window");
	CHECK(b[80 * 320 + 40 + 100] == c0, "the middle between its two parts is outside");
}

static void emptyWindowAtZeroStaysEmpty(void) {
	uint32_t c0, c1;
	windowColours(&c0, &c1);
	windowSetup(&scene, 0); // start 0, end 0
	renderScene(&scene, 40, 40, b);
	CHECK(b[80 * 320 + 0] == c0 && b[80 * 320 + 100] == c0 && b[80 * 320 + 319] == c0, "an empty window shows nothing, margins included");
}

static void fullFuzzCentreUnchanged(void) {
	static const int margins[][2] = { { 1, 1 }, { 22, 22 }, { 40, 40 }, { 72, 72 }, { 0, 40 }, { 40, 0 } };
	int seed, m, mode;
	for (seed = 1; seed <= 500; ++seed) {
		for (mode = 0; mode <= 5; ++mode) {
			sceneRandom(&scene, seed, mode);
			for (m = 0; m < 6; ++m) {
				checkCentre(&scene, margins[m][0], margins[m][1], "everything", mode, seed);
			}
		}
	}
}

static void pixelsAccessorsUseTheGbaColumns(void) {
	static struct GBAVideoSoftwareRenderer renderer;
	static uint32_t buffer[SCENE_MAX_WIDTH * SCENE_HEIGHT];
	static uint32_t frame[NARROW];
	static struct Scene copy;
	size_t stride = 0;
	const void* pixels = NULL;
	int y, x;
	copy = scene;
	memset(&renderer, 0, sizeof(renderer));
	GBAVideoSoftwareRendererCreate(&renderer);
	renderer.d.vram = copy.vram;
	renderer.d.palette = copy.palette;
	renderer.d.oam = &copy.oam;
	renderer.outputBuffer = buffer;
	renderer.outputBufferStride = 320;
	renderer.d.init(&renderer.d);
	GBAVideoSoftwareRendererSetWideMargins(&renderer, 40, 40);
	for (y = 0; y < SCENE_HEIGHT; ++y) {
		for (x = 0; x < 320; ++x) {
			buffer[y * 320 + x] = 0xFF000000u | (y << 12) | x;
		}
	}
	renderer.d.getPixels(&renderer.d, &stride, &pixels);
	CHECK(stride == 320, "stride is the row length of the whole picture, got %d", (int) stride);
	CHECK(((const uint32_t*) pixels)[0] == buffer[40], "pixels start at the GBA's first column");
	CHECK(((const uint32_t*) pixels)[239] == buffer[40 + 239] && ((const uint32_t*) pixels)[stride] == buffer[320 + 40], "and run 240 columns, row by row");

	for (y = 0; y < SCENE_HEIGHT; ++y) {
		for (x = 0; x < 240; ++x) {
			frame[y * 240 + x] = 0xFE000000u | (y << 12) | x;
		}
	}
	memset(buffer, 0, sizeof(buffer));
	renderer.d.putPixels(&renderer.d, 240, frame);
	CHECK(buffer[40] == frame[0] && buffer[319 * 0 + 160 * 0 + 40 + 239] == frame[239], "a stored frame goes into the GBA's columns");
	CHECK(buffer[159 * 320 + 40 + 239] == frame[159 * 240 + 239], "all the way to the last pixel");
	CHECK(buffer[39] == 0 && buffer[280] == 0, "and leaves the margins alone");
}

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "--write-golden") == 0) {
		writeGoldens();
		return 0;
	}
	narrowRenderIsDeterministic();
	zeroMarginsMatchStock();
	createStartsWithoutMargins();
	setMarginsClamps();
	textBgCentreUnchanged();
	textBgMarginsMatchShiftedNarrow();
	textBgMosaicAnchoredAtGbaZero();
	affineCentreUnchanged();
	bitmapCentreUnchanged();
	affineMarginsMatchMovedReference();
	bitmapMarginsAreBackdrop();
	spritesCentreUnchanged();
	spriteHalfOffLeftDrawsInMargin();
	spriteParkedAt240DrawsInRightMargin();
	doubleSizeAffineSpriteAtMaxMargins();
	marginSpritesDoNotConsumeCycles();
	windowTouchingEdgesExtends();
	windowInsideStaysInside();
	wrappedWindowCoversBothMargins();
	emptyWindowAtZeroStaysEmpty();
	fullFuzzCentreUnchanged();
	pixelsAccessorsUseTheGbaColumns();
	printf("wide_render_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
