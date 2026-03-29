#ifndef SIXEL_DISPLAY_H
#define SIXEL_DISPLAY_H

#include "bayer_patterns.h"
#include "framebuffer_4i8.h"
#include "utils.h"
#include <stdint.h>

typedef int sixel_palette_color[3];

enum sixel_palette_color_type {
	SIXEL_PALETTE_COLOR_TYPE_HLS = 1,
	SIXEL_PALETTE_COLOR_TYPE_RGB = 2,
};

typedef struct {
	int size;
	sixel_palette_color* colors;
	enum sixel_palette_color_type color_type;
} sixel_palette;

typedef struct {
	unsigned char* index_data;
	int width;
	int height;
	sixel_palette palette;
} sixel_indexed_bitmap;

void init_sixel_indexed_bitmap(sixel_indexed_bitmap* bitmap, int width, int height) {
	bitmap->width = width;
	bitmap->height = height;
	bitmap->index_data = (unsigned char*)malloc(sizeof(unsigned char) * width * height);
}

void free_sixel_indexed_bitmap(sixel_indexed_bitmap* bitmap) {
	if (bitmap->index_data) {
		free(bitmap->index_data);
		bitmap->index_data = NULL;
	}
}

void init_sixel_palette(sixel_palette* palette, int size, enum sixel_palette_color_type color_type) {
	palette->size = size;
	palette->color_type = color_type;
	palette->colors = (sixel_palette_color*)malloc(sizeof(sixel_palette_color) * size);
}

void free_sixel_palette(sixel_palette* palette) {
	if (palette->colors) {
		free(palette->colors);
		palette->colors = NULL;
	}
}

void init_sixel_palette_rgbuniform(sixel_palette* palette, int num_steps) {
	int size = (num_steps + 1) * (num_steps + 1) * (num_steps + 1);
	init_sixel_palette(palette, size, SIXEL_PALETTE_COLOR_TYPE_RGB);
	int color_num = 0;
	for (int i = 0; i <= num_steps; i++) {
		for (int j = 0; j <= num_steps; j++) {
			for (int k = 0; k <= num_steps; k++) {
				palette->colors[color_num][0] = (100 * i) / num_steps;
				palette->colors[color_num][1] = (100 * j) / num_steps;
				palette->colors[color_num][2] = (100 * k) / num_steps;
				color_num++;
			}
		}
	}
}

int closest_point(int x, int num_steps) {
	return ((2 * x * num_steps + 255) * num_steps) / 255 / 2 / num_steps;
}

void convert_4i8_to_sixel_indexed_bitmap_rgbuniform(sixel_indexed_bitmap* bitmap, framebuffer_4i8 fb, int num_steps) {
	int img_x = fb.width, img_y = fb.height, img_n = 4;
	unsigned char* image_buffer = fb.data;
	for (int y = 0; y < img_y; y++) {
		for (int x = 0; x < img_x; x++) {
			int r = image_buffer[y * img_n * img_x + x * img_n + 0];
			int g = image_buffer[y * img_n * img_x + x * img_n + 1];
			int b = image_buffer[y * img_n * img_x + x * img_n + 2];

			int r_x = closest_point(r, num_steps);
			int g_x = closest_point(g, num_steps);
			int b_x = closest_point(b, num_steps);

			int color_num = r_x * (num_steps + 1) * (num_steps + 1) + g_x * (num_steps + 1) + b_x;

			bitmap->index_data[y * img_x + x] = color_num;
		}
	}
}

//__declspec(noinline)
void convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering(sixel_indexed_bitmap* bitmap, framebuffer_4i8 fb, int num_steps) {
	int img_x = fb.width, img_y = fb.height, img_n = 4;
	unsigned char* image_buffer = fb.data;
	unsigned char divider = 256 / num_steps;
	const int step_plus_one = num_steps + 1;
	const int step_plus_one_squared = step_plus_one * step_plus_one;
	//ivec3 steps_vec = {step_plus_one_squared, step_plus_one, 1};

	for (int y = 0; y < img_y; y++) {
		unsigned char bayer_y = y % 16;
		for (int x = 0; x < img_x; x++) {
			int bayer_x = x % 16;
			int r = image_buffer[y * img_n * img_x + x * img_n + 0];
			int g = image_buffer[y * img_n * img_x + x * img_n + 1];
			int b = image_buffer[y * img_n * img_x + x * img_n + 2];
			//ivec3 color_vec = {r, g, b};

			int t = BAYER_PATTERN_16X16[bayer_y][bayer_x];
			int corr = (t / num_steps);
			//glm_ivec3_adds(color_vec, corr, color_vec);
			//glm_ivec3_divs(color_vec, divider, color_vec);
			int r_x = (r + corr) / divider;
			int g_x = (g + corr) / divider;
			int b_x = (b + corr) / divider;

			//r_x = clamp_int(r_x, 0, num_steps);
			//g_x = clamp_int(g_x, 0, num_steps);
			//b_x = clamp_int(b_x, 0, num_steps);

			int color_num = r_x * step_plus_one_squared + g_x * step_plus_one + b_x;
			//int color_num = glm_ivec3_dot(color_vec, steps_vec);
			// color_num = 215;
			bitmap->index_data[y * img_x + x] = color_num;
		}
	}
}

//__declspec(noinline)
void convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors(sixel_indexed_bitmap* bitmap, framebuffer_4i8 fb) {
	int img_x = fb.width, img_y = fb.height, img_n = 4;
	const int num_steps = 5;
	unsigned char* image_data_ptr = fb.data;
	unsigned char divider = 256 / num_steps;
	const int step_plus_one = num_steps + 1;
	const int step_plus_one_squared = step_plus_one * step_plus_one;
	unsigned char* index_data_ptr = bitmap->index_data;

	//for (int xy = 0; xy < img_x * img_y; xy++) {
		//int y = xy / img_x;
		//int x = xy % img_x;
	for (int y = 0;y < img_y;y++) {
		for (int x = 0;x < img_x;x++) {
			unsigned char bayer_y = y % 16;
			unsigned char bayer_x = x % 16;
			unsigned char r = *(image_data_ptr + 0);
			unsigned char g = *(image_data_ptr + 1);
			unsigned char b = *(image_data_ptr + 2);
			image_data_ptr += img_n;

			unsigned char t = BAYER_PATTERN_16X16[bayer_y][bayer_x];
			unsigned char corr = (t / num_steps);
			unsigned char r_x = (r + corr) / divider;
			unsigned char g_x = (g + corr) / divider;
			unsigned char b_x = (b + corr) / divider;

			unsigned char color_num = r_x * step_plus_one_squared + g_x * step_plus_one + b_x;
			*index_data_ptr = color_num;
			index_data_ptr++;
		}
	}
}


const unsigned char BAYER_PATTERN_16X16_DIVIDED_BY_5[16][16] = { //	16x16 Bayer Dithering Matrix.  Color levels: 256
{  0 ,  38 ,   9 ,  47 ,   2 ,  40 ,  12 ,  50 ,   0 ,  38 ,  10 ,  48 ,   3 ,  41 ,  12 ,  50 },
{ 25 ,  12 ,  35 ,  22 ,  27 ,  15 ,  37 ,  24 ,  26 ,  13 ,  35 ,  23 ,  28 ,  15 ,  38 ,  25 },
{  6 ,  44 ,   3 ,  41 ,   8 ,  47 ,   5 ,  43 ,   7 ,  45 ,   3 ,  42 ,   9 ,  47 ,   6 ,  44 },
{ 31 ,  19 ,  28 ,  16 ,  34 ,  21 ,  31 ,  18 ,  32 ,  19 ,  29 ,  16 ,  34 ,  22 ,  31 ,  19 },
{  1 ,  39 ,  11 ,  49 ,   0 ,  39 ,  10 ,  48 ,   2 ,  40 ,  11 ,  50 ,   1 ,  39 ,  11 ,  49 },
{ 27 ,  14 ,  36 ,  24 ,  26 ,  13 ,  35 ,  23 ,  27 ,  15 ,  37 ,  24 ,  26 ,  14 ,  36 ,  23 },
{  8 ,  46 ,   4 ,  43 ,   7 ,  45 ,   4 ,  42 ,   8 ,  46 ,   5 ,  43 ,   7 ,  46 ,   4 ,  42 },
{ 33 ,  20 ,  30 ,  17 ,  32 ,  20 ,  29 ,  16 ,  34 ,  21 ,  30 ,  18 ,  33 ,  20 ,  30 ,  17 },
{  0 ,  38 ,  10 ,  48 ,   2 ,  41 ,  12 ,  50 ,   0 ,  38 ,   9 ,  48 ,   2 ,  40 ,  12 ,  50 },
{ 25 ,  13 ,  35 ,  22 ,  28 ,  15 ,  37 ,  25 ,  25 ,  13 ,  35 ,  22 ,  28 ,  15 ,  37 ,  25 },
{  6 ,  45 ,   3 ,  41 ,   9 ,  47 ,   6 ,  44 ,   6 ,  44 ,   3 ,  41 ,   9 ,  47 ,   5 ,  44 },
{ 32 ,  19 ,  29 ,  16 ,  34 ,  22 ,  31 ,  18 ,  32 ,  19 ,  28 ,  16 ,  34 ,  21 ,  31 ,  18 },
{  2 ,  40 ,  11 ,  49 ,   1 ,  39 ,  10 ,  49 ,   1 ,  40 ,  11 ,  49 ,   1 ,  39 ,  10 ,  48 },
{ 27 ,  14 ,  37 ,  24 ,  26 ,  14 ,  36 ,  23 ,  27 ,  14 ,  36 ,  24 ,  26 ,  13 ,  36 ,  23 },
{  8 ,  46 ,   5 ,  43 ,   7 ,  45 ,   4 ,  42 ,   8 ,  46 ,   5 ,  43 ,   7 ,  45 ,   4 ,  42 },
{ 33 ,  21 ,  30 ,  18 ,  33 ,  20 ,  29 ,  17 ,  33 ,  21 ,  30 ,  17 ,  32 ,  20 ,  29 ,  17 },
};

const unsigned char rg_lut[6][6] = {
{  0 ,   6 ,  12 ,  18 ,  24 ,  30 },
{ 36 ,  42 ,  48 ,  54 ,  60 ,  66 },
{ 72 ,  78 ,  84 ,  90 ,  96 , 102 },
{108 , 114 , 120 , 126 , 132 , 138 },
{144 , 150 , 156 , 162 , 168 , 174 },
{180 , 186 , 192 , 198 , 204 , 210 },
};

//__declspec(noinline)
void convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors2(sixel_indexed_bitmap* bitmap, framebuffer_4i8 fb) {
	int img_x = fb.width, img_y = fb.height, img_n = 4;
	const int num_steps = 5;
	unsigned char* image_data_ptr = fb.data;
	unsigned char divider = 256 / num_steps;
	const int step_plus_one = num_steps + 1;
	const int step_plus_one_squared = step_plus_one * step_plus_one;
	unsigned char* index_data_ptr = bitmap->index_data;

	for (int y = 0;y < img_y;y++) {
		for (int x = 0;x < img_x;x++) {
			unsigned char bayer_y = y % 16;
			unsigned char bayer_x = x % 16;
			unsigned char corr = BAYER_PATTERN_16X16_DIVIDED_BY_5[bayer_y][bayer_x];

			unsigned char image_data[4];
			memcpy(image_data, image_data_ptr, 4);
			for (int i = 0;i < 4;i++) {
				image_data[i] += corr;
				image_data[i] /= divider;
			}

			//unsigned char color_num =
			//	image_data[0] * step_plus_one_squared +
			//	image_data[1] * step_plus_one +
			//	image_data[2];
			//*index_data_ptr = color_num;
			*index_data_ptr = rg_lut[image_data[0]][image_data[1]] + image_data[2];
			image_data_ptr += img_n;
			index_data_ptr++;
		}
	}

	//for (int i = 0;i < 16;i++) {
	//	printf("{");
	//	for (int j = 0;j < 16;j++) {
	//		printf("%3d ", BAYER_PATTERN_16X16[i][j]/5);
	//		if (j < 15) {
	//			printf(", ");
	//		}
	//	}
	//	printf("},\r\n");
	//}
	//for (int r = 0; r < 6; r++) {
	//	printf("{");
	//	for (int g = 0; g < 6; g++) {
	//		printf("%3d ", r * 36 + g * 6);
	//		if (g < 5) {
	//			printf(", ");
	//		}
	//	}
	//	printf("},\r\n");
	//}
}


typedef struct {
	sixel_indexed_bitmap bitmap;
	char* data;
	char* header_data;
	char* sixel_data;
	int header_valid;
	int header_data_size;
	int data_size;
} sixel_display_ctx;

void init_sixel_display_ctx(sixel_display_ctx* ctx, int width, int height) {
	init_sixel_indexed_bitmap(&ctx->bitmap, width, height);
	ctx->data = (char*)malloc((width * height * 8) + 1024);
	ctx->header_data = ctx->data;
	ctx->sixel_data = NULL;
	ctx->header_valid = 0;
	ctx->header_data_size = 0;
	ctx->data_size = 0;
}

void free_sixel_display_ctx(sixel_display_ctx* ctx) {
	if (ctx->data) {
		free(ctx->data);
		ctx->data = NULL;
	}
}

char* fast_itoa(char* buffer, int value) {
	if (value == 0) {
		*buffer++ = '0';
		return buffer;
	}

	// A temporary buffer to store the digits in reverse order.
	// An int can have at most 10 digits, plus a sign.
	char temp[12];
	char* p = temp;

	int is_negative = (value < 0);
	if (is_negative) {
		value = -value;
	}

	// Extract digits in reverse order
	while (value > 0) {
		*p++ = '0' + (value % 10);
		value /= 10;
	}

	if (is_negative) {
		*p++ = '-';
	}

	// Reverse the temporary string and copy it to the output buffer
	while (p > temp) {
		*buffer++ = *--p;
	}

	return buffer;
}

char* fast_itoa_less_than_1000(char* buffer, int num) {
	const hundreds = ~~(num / 100);
	*buffer = 0x30 + hundreds;
	buffer += +(hundreds != 0);
	num -= hundreds * 100;

	const tens = ~~(num / 10);
	*buffer = 0x30 + tens;
	buffer += +(hundreds + tens != 0);
	num -= tens * 10;

	*buffer++ = 0x30 + num;
	return buffer;
}

static inline char* encode_sixel_data_header(char* buffer) {
	return buffer + sprintf(buffer, "\x1bP9;1;;q");
}

static inline char* encode_sixel_data_terminator(char* buffer) {
	return buffer + sprintf(buffer, "\x1b\\");
}

static inline char* encode_sixel_data_modify_color(char* buffer,
	int color_number,
	enum sixel_palette_color_type color_type,
	sixel_palette_color color) {
	buffer += sprintf(buffer, "#%d;%d;%d;%d;%d",
		color_number,
		color_type,
		color[0],
		color[1],
		color[2]);
	return buffer;
}

static inline char* encode_sixel_data_set_color(char* buffer, int color_number) {
	*buffer++ = '#';
	buffer = fast_itoa_less_than_1000(buffer, color_number);
	return buffer;
}

static inline char* encode_sixel_data_character(char* buffer, int mask) {
	*buffer++ = (char)(0x3F + mask);
	return buffer;
}

static inline char* encode_sixel_data_character_repeated(char* buffer, int mask, int count) {
	char character = (char)(0x3F + mask);
	if (count > 3) {
		*buffer++ = '!';
		buffer = fast_itoa_less_than_1000(buffer, count);
		*buffer++ = character;
		return buffer;
	}
	*(buffer + 0) = character;
	*(buffer + 1) = character;
	*(buffer + 2) = character;
	return buffer + count;
}

static inline char* encode_sixel_data_new_line(char* buffer) {
	*buffer++ = '-';
	return buffer;
}

static inline char* encode_sixel_data_carriage_return(char* buffer) {
	*buffer++ = '$';
	return buffer;
}

static inline char* encode_sixel_row(sixel_display_ctx* ctx, char* buffer, int y_start, int width, int height) {
	for (int y = 0;y < height; y++) {
		for (int x = 0;x < width;x++) {
			int color_num = ctx->bitmap.index_data[(y_start + y) * ctx->bitmap.width + x];
			buffer = encode_sixel_data_set_color(buffer, color_num);
			buffer = encode_sixel_data_character(buffer, 1 << y);
		}
		buffer = encode_sixel_data_carriage_return(buffer);
	}
	return buffer;
}

static inline int get_color_mask_for_column(sixel_display_ctx* ctx, int x, int y_start, int height, int search_color) {
	int mask = 0;
	for (int y = 0;y < height; y++) {
		int color_num = ctx->bitmap.index_data[(y_start + y) * ctx->bitmap.width + x];
		if (color_num == search_color) {
			mask |= (1 << y);
		}
	}
	return mask;
}

static inline char* encode_sixel_row_alt(sixel_display_ctx* ctx, char* buffer, int y_start, int width, int height) {
	static uint8_t painted[1024];
	for (int i = 0;i < 1024 && i < width;i++)
		painted[i] = 0;
	char* buffer_start = buffer;
	int current_color = ctx->bitmap.index_data[(y_start)*ctx->bitmap.width];
	int lookahead = 4;
	int x = 0;
	buffer = encode_sixel_data_set_color(buffer, current_color);
	int full_column = 0;
	for (int i = 0;i < height;i++) {
		full_column |= (1 << i);
	}
	int startx = x;
	int endx = x;
	while (1) {
		int current_mask = 0;
		uint8_t painted_mask = 0;
		for (int i = x;i < x + lookahead && i < width;i++) {
			current_mask = get_color_mask_for_column(ctx, i, y_start, height, current_color);
			current_mask &= ~painted[i];
			if (current_mask != 0) {

				buffer = encode_sixel_data_character_repeated(buffer, 0, i - x);
				x = i;
				startx = x;
				endx = x;
				break;
			}
		}
		if (current_mask == 0) {
			for (int i = x;i < width;i++) {
				if (painted[i] != full_column) {
					buffer = encode_sixel_data_character_repeated(buffer, 0, i - x);
					x = i;
					startx = x;
					endx = x;
					for (int j = 0;j < height;j++) {
						if (((~painted[i]) & (1 << j)) != 0) {
							current_color = ctx->bitmap.index_data[(y_start + j) * ctx->bitmap.width + i];
							break;
						}
					}
					buffer = encode_sixel_data_set_color(buffer, current_color);
					current_mask = get_color_mask_for_column(ctx, x, y_start, height, current_color);
					break;
				}
			}
		}
		if (current_mask == 0) {
			if (x == 0) {
				break;
			}
			else {
				buffer = encode_sixel_data_carriage_return(buffer);
				x = 0;
				continue;
			}
		}
		for (int i = x;i < endx + lookahead && i < width;i++) {
			int column_mask = get_color_mask_for_column(ctx, i, y_start, height, current_color);
			painted_mask |= painted[i];
			if ((painted_mask & (current_mask | column_mask)) == 0) {
				current_mask |= column_mask;
				painted[i] |= column_mask;
				if (column_mask)
					endx = i;
			}
			else {
				break;
			}
		}
		buffer = encode_sixel_data_character_repeated(buffer, current_mask, endx - startx + 1);
		x = endx + 1;
		if (x == width) {
			buffer = encode_sixel_data_carriage_return(buffer);
			x = 0;
		}
	}
	return buffer;
}

//void print_binary(sixel_column_t n) {
//	for (int i = 63; i >= 0; i--) {
//		printf("%d", (n >> i) & 1);
//		if (i % 8 == 0)printf(" ");
//	}
//}

typedef uint64_t sixel_column_t;
const sixel_column_t LSB_SET_EACH_BYTE = 0x0101010101010101ULL;
const sixel_column_t MSB_SET_EACH_BYTE = 0x8080808080808080ULL;
static inline int get_color_mask_for_column_opt(sixel_column_t* transposed, int x, int search_color, int full_column, sixel_column_t packed_color) {
	sixel_column_t column = transposed[x];
	int mask_alt = 0;
	/* XOR column with packed color sets byte to 0x00 if color matches else its not 0x00 */
	sixel_column_t column_match = column ^ packed_color;
	/*
	Bit twiddling hack to test each byte for zero in parallel:
	- Set highest bit of all bytes to stop carry from propagating between them.
	- Subtract lower bits to clear the carry if any bits are set.
	- AND with original highest bits flipped, to clear carry if its bit was set.
	Bytes that were 0x00 (color matches) become 0x80, others 0x00.
	*/
	sixel_column_t z = (MSB_SET_EACH_BYTE - (column_match & ~MSB_SET_EACH_BYTE)) & ~column_match & MSB_SET_EACH_BYTE;
	/* Extracting the 8 high bits into a single 8-bit mask*/
	mask_alt = (z * 0x0002040810204081ULL) >> 56;
	/* Mask with full column mask to ignore bytes outside height range*/
	mask_alt &= full_column;
	return mask_alt;
}

static inline char* encode_sixel_row_alt_opt(sixel_display_ctx* ctx, char* buffer, int y_start, int width, int height) {
	static int painted[1024];
	for (int i = 0;i < 1024 && i < width;i++)
		painted[i] = 0;
	static sixel_column_t transposed[1024];
	for (int i = 0;i < 1024 && i < width;i++) {
		sixel_column_t packed = 0;
		for (int j = 0;j < height;j++) {
			int color_num = ctx->bitmap.index_data[(y_start + j) * ctx->bitmap.width + i];
			packed |= ((sixel_column_t)color_num << (j * 8));
		}
		transposed[i] = packed;
	}
	char* buffer_start = buffer;
	int current_color = ctx->bitmap.index_data[(y_start)*ctx->bitmap.width];
	sixel_column_t packed_color = (sixel_column_t)current_color * LSB_SET_EACH_BYTE;
	int lookahead = 4;
	int x = 0;
	buffer = encode_sixel_data_set_color(buffer, current_color);
	int full_column = 0;
	for (int i = 0;i < height;i++) {
		full_column |= (1 << i);
	}
	int startx = x;
	int endx = x;
	while (1) {
		int current_mask = 0;
		int painted_mask = 0;
		for (int i = x;i < x + lookahead && i < width;i++) {
			current_mask = get_color_mask_for_column_opt(transposed, i, current_color, full_column, packed_color);
			current_mask &= ~painted[i];
			if (current_mask != 0) {

				buffer = encode_sixel_data_character_repeated(buffer, 0, i - x);
				x = i;
				startx = x;
				endx = x;
				break;
			}
		}
		if (current_mask == 0) {
			for (int i = x;i < width;i++) {
				if (painted[i] != full_column) {
					buffer = encode_sixel_data_character_repeated(buffer, 0, i - x);
					x = i;
					startx = x;
					endx = x;
					for (int j = 0;j < height;j++) {
						if (((~painted[i]) & (1 << j)) != 0) {
							current_color = ctx->bitmap.index_data[(y_start + j) * ctx->bitmap.width + i];
							break;
						}
					}
					buffer = encode_sixel_data_set_color(buffer, current_color);
					packed_color = (sixel_column_t)current_color * LSB_SET_EACH_BYTE;
					current_mask = get_color_mask_for_column_opt(transposed, i, current_color, full_column, packed_color);
					break;
				}
			}
		}
		if (current_mask == 0) {
			if (x == 0) {
				break;
			}
			else {
				buffer = encode_sixel_data_carriage_return(buffer);
				x = 0;
				continue;
			}
		}
		for (int i = x;i < endx + lookahead && i < width;i++) {
			int column_mask = get_color_mask_for_column_opt(transposed, i, current_color, full_column, packed_color);
			painted_mask |= painted[i];
			if ((painted_mask & (current_mask | column_mask)) == 0) {
				current_mask |= column_mask;
				painted[i] |= column_mask;
				if (column_mask)
					endx = i;
			}
			else {
				break;
			}
		}
		buffer = encode_sixel_data_character_repeated(buffer, current_mask, endx - startx + 1);
		x = endx + 1;
		if (x == width) {
			buffer = encode_sixel_data_carriage_return(buffer);
			x = 0;
		}
	}
	return buffer;
}

void generate_sixel_display_data(sixel_display_ctx* ctx) {
	if (!ctx->header_valid) {
		char* palette_ptr = ctx->header_data; // Reset pointer to the start
		palette_ptr += sprintf(palette_ptr, "\x1b[H");
		palette_ptr = encode_sixel_data_header(palette_ptr);

		for (int i = 0; i <= ctx->bitmap.palette.size; i++) {
			palette_ptr = encode_sixel_data_modify_color(palette_ptr, i, ctx->bitmap.palette.color_type, ctx->bitmap.palette.colors[i]);
		}

		ctx->header_data_size = palette_ptr - ctx->header_data; // Store the final length
		ctx->header_valid = 1;
		ctx->sixel_data = palette_ptr;
	}

	char* buffer_ptr = ctx->sixel_data;

	for (int row_start = 0; row_start < ctx->bitmap.height; row_start += 6) {
		int row_height = (row_start + 6 <= ctx->bitmap.height) ? 6 : (ctx->bitmap.height - row_start);
		//buffer_ptr = encode_sixel_row(ctx, buffer_ptr, row_start, ctx->bitmap.width, row_height);
		//buffer_ptr = encode_sixel_row_alt(ctx, buffer_ptr, row_start, ctx->bitmap.width, row_height);
		buffer_ptr = encode_sixel_row_alt_opt(ctx, buffer_ptr, row_start, ctx->bitmap.width, row_height);
		buffer_ptr = encode_sixel_data_new_line(buffer_ptr);
	}

	// Append Sixel terminator to the pixel buffer
	buffer_ptr = encode_sixel_data_terminator(buffer_ptr);
	*buffer_ptr = '\0';
	ctx->data_size = buffer_ptr - ctx->data;
}

#endif // SIXEL_DISPLAY_H