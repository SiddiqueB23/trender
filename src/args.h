#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tio_gfx_backend, tio_gfx_halfblock_color_mode, tio_gfx_iterm_encode_fmt
 * are provided by trender.h (via tio_gfx.h) which includes this file. */

typedef struct {
	char obj_path[512];
	char testmodel[64];      /* name of a built-in test model, e.g. "bmw" */
	int  width;              /* output pixel width  (0 = auto)            */
	int  height;             /* output pixel height (0 = auto)            */
	int  frames;             /* frames to render    (0 = auto)            */
	int  interactive;        /* enable camera controls + HUD              */
	int  rotate;             /* auto-rotate model each frame              */
	int  verbose;            /* print diagnostic / timing output          */
	int  center;             /* translate model so bounding box is at origin */
	int  autofit;            /* center + scale model and set camera dist  */
	int  threads;            /* number of OMP threads (default 2)         */
	int  buffers;            /* triple-buffer count   (default 3)         */
	tio_gfx_backend display_mode;               /* sixel, halfblock, or iterm (required)    */
	tio_gfx_halfblock_color_mode hb_color_mode; /* 216 or 24bpp; ignored for non-halfblock */
	tio_gfx_iterm_encode_fmt iterm_encode_fmt;  /* BMP, PNG, or JPEG; ignored for non-iterm */
	int  iterm_jpeg_quality;                    /* 1–100; itermjpeg only                    */
	int  iterm_png_compression;                 /* 0–9; iterm/itermpng only                 */
	int  upscale_x;                             /* display upscale X (1=normal, 2=2× wide)  */
	int  upscale_y;                             /* display upscale Y (1=normal, 2=2× tall)  */
	int  cell_height_px;                        /* terminal cell height in px               */
	int  cell_width_px;                         /* terminal cell width in px                */
	int  display_mode_set;                      /* 0 if --display was not provided          */
} cli_args_t;

static void print_usage(const char* prog) {
	fprintf(stderr, "Usage: %s -d sixel|halfblock[216|24bpp]|iterm [options] model.obj\r\n", prog);
	fprintf(stderr, "       %s -d sixel|halfblock[216|24bpp]|iterm [options] --testmodel=<name>\r\n", prog);
	fprintf(stderr, "Options:\r\n");
	fprintf(stderr, "  -d, --display MODE    Display mode (required):\r\n");
	fprintf(stderr, "                          sixel          — sixel graphics\r\n");
	fprintf(stderr, "                          halfblock      — halfblock, 216-color (default)\r\n");
	fprintf(stderr, "                          halfblock216   — halfblock, 216-color\r\n");
	fprintf(stderr, "                          halfblock24bpp — halfblock, 24-bit true color\r\n");
	fprintf(stderr, "                          iterm          — iTerm2 inline image, PNG (default)\r\n");
	fprintf(stderr, "                          itermpng       — iTerm2 inline image, PNG\r\n");
	fprintf(stderr, "                          itermbmp       — iTerm2 inline image, BMP\r\n");
	fprintf(stderr, "                          itermjpeg      — iTerm2 inline image, JPEG\r\n");
	fprintf(stderr, "                          kitty          — Kitty graphics protocol, RGB\r\n");
	fprintf(stderr, "  -W, --width N         Output width in pixels  (default: auto from terminal)\r\n");
	fprintf(stderr, "  -H, --height N        Output height in pixels (default: auto from terminal)\r\n");
	fprintf(stderr, "  -n, --frames N        Number of frames to render (default: 1)\r\n");
	fprintf(stderr, "  -i, --interactive     Interactive mode with camera controls\r\n");
	fprintf(stderr, "  -r, --rotate          Auto-rotate the model\r\n");
	fprintf(stderr, "  -v, --verbose         Print diagnostic and timing output\r\n");
	fprintf(stderr, "  -c, --center          Translate model so its bounding box is at origin\r\n");
	fprintf(stderr, "  -f, --autofit         Center, scale model and set camera distance\r\n");
	fprintf(stderr, "  -t, --threads N       Number of threads (default: 2)\r\n");
	fprintf(stderr, "  -B, --buffers N       Buffer count      (default: 3)\r\n");
	fprintf(stderr, "      --jpeg-quality N  JPEG quality 1–100 (default: 90; higher=better quality/larger; itermjpeg only)\r\n");
	fprintf(stderr, "      --png-compression N  PNG compression 0–9 (default: 8; higher=smaller file/slower; iterm/itermpng only)\r\n");
	fprintf(stderr, "  --display-upscale-x N, -dux N\r\n");
	fprintf(stderr, "                        Upscale display N× horizontally (default: 1; all pixel backends)\r\n");
	fprintf(stderr, "  --display-upscale-y N, -duy N\r\n");
	fprintf(stderr, "                        Upscale display N× vertically   (default: 1; all pixel backends)\r\n");
	fprintf(stderr, "      --testmodel=NAME  Use a built-in test model (e.g. bmw)\r\n");
	fprintf(stderr, "  -h, --help            Show this help\r\n");
}

static int parse_args(int argc, char* argv[], cli_args_t* args) {
	args->obj_path[0]      = '\0';
	args->testmodel[0]     = '\0';
	args->width            = 0;
	args->height           = 0;
	args->frames           = 0;
	args->interactive      = 0;
	args->rotate           = 0;
	args->verbose          = 0;
	args->center           = 0;
	args->autofit          = 0;
	args->threads          = 2;
	args->buffers          = 3;
	args->display_mode     = TIO_GFX_BACKEND_SIXEL;
	args->hb_color_mode    = TIO_GFX_HALFBLOCK_COLOR_216;
	args->iterm_encode_fmt    = TIO_GFX_ITERM_FMT_PNG;
	args->iterm_jpeg_quality    = 90;
	args->iterm_png_compression = 8;
	args->upscale_x             = 1;
	args->upscale_y             = 1;
	args->cell_height_px        = 1;
	args->cell_width_px         = 1;
	args->display_mode_set = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			return -1;
		} else if ((strcmp(argv[i], "--display") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
			i++;
			if (strcmp(argv[i], "sixel") == 0) {
				args->display_mode = TIO_GFX_BACKEND_SIXEL;
			} else if (strcmp(argv[i], "halfblock") == 0 || strcmp(argv[i], "halfblock216") == 0) {
				args->display_mode  = TIO_GFX_BACKEND_HALFBLOCK;
				args->hb_color_mode = TIO_GFX_HALFBLOCK_COLOR_216;
			} else if (strcmp(argv[i], "halfblock24bpp") == 0) {
				args->display_mode  = TIO_GFX_BACKEND_HALFBLOCK;
				args->hb_color_mode = TIO_GFX_HALFBLOCK_COLOR_24BIT;
			} else if (strcmp(argv[i], "iterm") == 0 || strcmp(argv[i], "itermpng") == 0) {
				args->display_mode     = TIO_GFX_BACKEND_ITERM;
				args->iterm_encode_fmt = TIO_GFX_ITERM_FMT_PNG;
			} else if (strcmp(argv[i], "itermbmp") == 0) {
				args->display_mode     = TIO_GFX_BACKEND_ITERM;
				args->iterm_encode_fmt = TIO_GFX_ITERM_FMT_BMP;
			} else if (strcmp(argv[i], "itermjpeg") == 0) {
				args->display_mode     = TIO_GFX_BACKEND_ITERM;
				args->iterm_encode_fmt = TIO_GFX_ITERM_FMT_JPEG;
			} else if (strcmp(argv[i], "kitty") == 0) {
				args->display_mode = TIO_GFX_BACKEND_KITTY;
			} else {
				fprintf(stderr, "trender: unknown display mode '%s' (use 'sixel', 'halfblock', 'halfblock216', 'halfblock24bpp', 'iterm', 'itermpng', 'itermbmp', 'itermjpeg', or 'kitty')\r\n", argv[i]);
				return -1;
			}
			args->display_mode_set = 1;
		} else if ((strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "-W") == 0) && i + 1 < argc) {
			args->width = atoi(argv[++i]);
		} else if ((strcmp(argv[i], "--height") == 0 || strcmp(argv[i], "-H") == 0) && i + 1 < argc) {
			args->height = atoi(argv[++i]);
		} else if ((strcmp(argv[i], "--frames") == 0 || strcmp(argv[i], "-n") == 0) && i + 1 < argc) {
			args->frames = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--interactive") == 0 || strcmp(argv[i], "-i") == 0) {
			args->interactive = 1;
		} else if (strcmp(argv[i], "--rotate") == 0 || strcmp(argv[i], "-r") == 0) {
			args->rotate = 1;
		} else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
			args->verbose = 1;
		} else if (strcmp(argv[i], "--center") == 0 || strcmp(argv[i], "-c") == 0) {
			args->center = 1;
		} else if (strcmp(argv[i], "--autofit") == 0 || strcmp(argv[i], "-f") == 0) {
			args->autofit = 1;
		} else if ((strcmp(argv[i], "--threads") == 0 || strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
			args->threads = atoi(argv[++i]);
		} else if ((strcmp(argv[i], "--buffers") == 0 || strcmp(argv[i], "-B") == 0) && i + 1 < argc) {
			args->buffers = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--jpeg-quality") == 0 && i + 1 < argc) {
			args->iterm_jpeg_quality = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--png-compression") == 0 && i + 1 < argc) {
			args->iterm_png_compression = atoi(argv[++i]);
		} else if ((strcmp(argv[i], "--display-upscale-x") == 0 || strcmp(argv[i], "-dux") == 0) && i + 1 < argc) {
			args->upscale_x = atoi(argv[++i]);
			if (args->upscale_x < 1) args->upscale_x = 1;
		} else if ((strcmp(argv[i], "--display-upscale-y") == 0 || strcmp(argv[i], "-duy") == 0) && i + 1 < argc) {
			args->upscale_y = atoi(argv[++i]);
			if (args->upscale_y < 1) args->upscale_y = 1;
		} else if (strncmp(argv[i], "--testmodel=", 12) == 0) {
			strncpy(args->testmodel, argv[i] + 12, sizeof(args->testmodel) - 1);
			args->testmodel[sizeof(args->testmodel) - 1] = '\0';
		} else if (argv[i][0] != '-') {
			strncpy(args->obj_path, argv[i], sizeof(args->obj_path) - 1);
			args->obj_path[sizeof(args->obj_path) - 1] = '\0';
		} else {
			fprintf(stderr, "trender: unknown option '%s'\r\n", argv[i]);
			return -1;
		}
	}

	if (!args->display_mode_set) {
		fprintf(stderr, "trender: --display is required\r\n");
		return -1;
	}

	if (args->testmodel[0] != '\0' && args->obj_path[0] == '\0') {
		snprintf(args->obj_path, sizeof(args->obj_path),
		         RESOURCES_PATH "%s/%s.obj", args->testmodel, args->testmodel);
	}

	if (args->obj_path[0] == '\0') {
		fprintf(stderr, "trender: no input file specified\r\n");
		return -1;
	}

	if (args->frames == 0)
		args->frames = args->interactive ? 100000 : 1;

	if (args->threads == 1) {
		args->buffers = 1;
	} else if (args->buffers < 3) {
		fprintf(stderr, "trender: --buffers must be at least 3 when using multiple threads (deadlock risk)\r\n");
		return -1;
	}

	return 0;
}
