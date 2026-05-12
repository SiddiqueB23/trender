#include "sixel_display.h"
#include "timer.h"
#include "tio.h"
#include "cglm/cglm.h"
#include <immintrin.h>
#include <utils.h>
tio_ctx_t ctx;

void cleanup(void) {
	tio_destroy(&ctx);
}

void get_barycentric_coordinates(float v0_x, float v0_y, float v1_x, float v1_y, float v2_x, float v2_y,
	float px, float py, float* u, float* v, float* w) {
	vec2 a = { v0_x, v0_y };
	vec2 b = { v1_x, v1_y };
	vec2 c = { v2_x, v2_y };
	vec2 p = { px, py };
	vec2 v0, v1, v2;
	glm_vec2_sub(b, a, v0);
	glm_vec2_sub(c, a, v1);
	glm_vec2_sub(p, a, v2);
	float d00 = glm_vec2_dot(v0, v0);
	float d01 = glm_vec2_dot(v0, v1);
	float d11 = glm_vec2_dot(v1, v1);
	float d20 = glm_vec2_dot(v2, v0);
	float d21 = glm_vec2_dot(v2, v1);
	float denom = d00 * d11 - d01 * d01;
	*v = (d11 * d20 - d01 * d21) / denom;
	*w = (d00 * d21 - d01 * d20) / denom;
	*u = 1.0f - *v - *w;
}

void draw_triangle(framebuffer_4i8* fb, float v0_x, float v0_y, float v1_x, float v1_y, float v2_x, float v2_y) {

	int min_x = (int)(glm_min(glm_min(v0_x, v1_x), v2_x) - 1.0f);
	int max_x = (int)(glm_max(glm_max(v0_x, v1_x), v2_x) + 1.0f);
	int min_y = (int)(glm_min(glm_min(v0_y, v1_y), v2_y) - 1.0f);
	int max_y = (int)(glm_max(glm_max(v0_y, v1_y), v2_y) + 1.0f);
	min_x = clamp_int(min_x, 0, fb->width - 1);
	max_x = clamp_int(max_x, 0, fb->width - 1);
	min_y = clamp_int(min_y, 0, fb->height - 1);
	max_y = clamp_int(max_y, 0, fb->height - 1);

	for (int y = min_y; y <= max_y; y++) {
		for (int x = min_x; x <= max_x; x++) {
			float alpha, beta, gamma;
			get_barycentric_coordinates(v0_x, v0_y, v1_x, v1_y, v2_x, v2_y, (float)x + 0.5f, (float)y + 0.5f, &alpha, &beta, &gamma);
			if (!(alpha >= 0 && beta >= 0 && gamma >= 0)) continue;

			unsigned char r = 255, g = 0, b = 0;
			r = (unsigned char)rintf(alpha * 255.0f);
			g = (unsigned char)rintf(beta * 255.0f);
			b = (unsigned char)rintf(gamma * 255.0f);
			set_pixel_4i8(fb, x, y, r, g, b, 255);
		}
	}
}

void draw_triangle2(framebuffer_f* alpha, framebuffer_f* beta, float v0_x, float v0_y, float v1_x, float v1_y, float v2_x, float v2_y)
{
	const int scale = 1;
	// 28.4 fixed-point coordinates
	const int Y1 = rintf((float)scale * v0_y);
	const int Y2 = rintf((float)scale * v1_y);
	const int Y3 = rintf((float)scale * v2_y);

	const int X1 = rintf((float)scale * v0_x);
	const int X2 = rintf((float)scale * v1_x);
	const int X3 = rintf((float)scale * v2_x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 * scale;
	const int FDX23 = DX23 * scale;
	const int FDX31 = DX31 * scale;

	const int FDY12 = DY12 * scale;
	const int FDY23 = DY23 * scale;
	const int FDY31 = DY31 * scale;

	// Bounding rectangle
	int minx = (min_int(min_int(X1, X2), X3) + (scale - 1)) / scale;
	int maxx = (max_int(max_int(X1, X2), X3) + (scale - 1)) / scale;
	int miny = (min_int(min_int(Y1, Y2), Y3) + (scale - 1)) / scale;
	int maxy = (max_int(max_int(Y1, Y2), Y3) + (scale - 1)) / scale;
	minx = clamp_int(minx, 0, alpha->width - 1);
	maxx = clamp_int(maxx, 0, alpha->width - 1);
	miny = clamp_int(miny, 0, alpha->height - 1);
	maxy = clamp_int(maxy, 0, alpha->height - 1);

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	int CY1 = C1 + DX12 * (miny * scale) - DY12 * (minx * scale);
	int CY2 = C2 + DX23 * (miny * scale) - DY23 * (minx * scale);
	int CY3 = C3 + DX31 * (miny * scale) - DY31 * (minx * scale);

	float inv_denom = 1.0f / ((float)CY1 + (float)CY2 + (float)CY3);

	float* alpha_buffer = alpha->data;
	float* beta_buffer = beta->data;
	for (int y = miny; y < maxy; y++)
	{
		alpha_buffer = alpha->data + (y * alpha->width + minx);
		beta_buffer = beta->data + (y * beta->width + minx);
		int CX1 = CY1;
		int CX2 = CY2;
		int CX3 = CY3;

		for (int x = minx; x < maxx; x++)
		{
			//if (CX3 < 0) {
			//	*alpha_buffer = 1.0f;
			//	*beta_buffer = 0.0f;
			//}
			//if ((CX1 | CX2 | CX3) > 0)
			if (((CX1>0) & (CX2>0) & (CX3>0)) | ((CX1<0) & (CX2<0) & (CX3<0)))
			{
				float CX2_float = (float)CX2;
				float CX3_float = (float)CX3;
				float alpha = CX2_float * inv_denom;
				float beta = CX3_float * inv_denom;
				*alpha_buffer = alpha;
				*beta_buffer = beta;
				//*alpha_buffer = 1.0f;
				//*beta_buffer = 0.0f;
			}

			CX1 -= FDY12;
			CX2 -= FDY23;
			CX3 -= FDY31;
			alpha_buffer++;
			beta_buffer++;
		}

		CY1 += FDX12;
		CY2 += FDX23;
		CY3 += FDX31;
	}
}

void draw_triangle3(framebuffer_4i8* fb, float v0_x, float v0_y, float v1_x, float v1_y, float v2_x, float v2_y)
{
	// 28.4 fixed-point coordinates
	const int Y1 = rintf(16.0f * v0_y);
	const int Y2 = rintf(16.0f * v1_y);
	const int Y3 = rintf(16.0f * v2_y);

	const int X1 = rintf(16.0f * v0_x);
	const int X2 = rintf(16.0f * v1_x);
	const int X3 = rintf(16.0f * v2_x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 << 4;
	const int FDX23 = DX23 << 4;
	const int FDX31 = DX31 << 4;

	const int FDY12 = DY12 << 4;
	const int FDY23 = DY23 << 4;
	const int FDY31 = DY31 << 4;

	// Bounding rectangle
	int minx = (min_int(min_int(X1, X2), X3) + 0xF) >> 4;
	int maxx = (max_int(max_int(X1, X2), X3) + 0xF) >> 4;
	int miny = (min_int(min_int(Y1, Y2), Y3) + 0xF) >> 4;
	int maxy = (max_int(max_int(Y1, Y2), Y3) + 0xF) >> 4;

	// Block size, standard 8x8 (must be power of two)
	const int q = 8;

	// Start in corner of 8x8 block
	minx &= ~(q - 1);
	miny &= ~(q - 1);

	//(char*&)colorBuffer += miny * stride;

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;
	float inv_denom = 1.0f / ((float)C1 + (float)C2 + (float)C3);

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	// Loop through blocks
	for (int y = miny; y < maxy; y += q)
	{
		for (int x = minx; x < maxx; x += q)
		{
			// Corners of block
			int x0 = x << 4;
			int x1 = (x + q - 1) << 4;
			int y0 = y << 4;
			int y1 = (y + q - 1) << 4;

			// Evaluate half-space functions
			bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
			bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
			bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
			bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
			int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

			bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
			bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
			bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
			bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
			int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

			bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
			bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
			bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
			bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
			int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

			// Skip block when outside an edge
			if (a == 0x0 || b == 0x0 || c == 0x0) continue;

			//unsigned int* buffer = colorBuffer;

			// Accept whole block when totally covered
			if (a == 0xF && b == 0xF && c == 0xF)
			{
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				for (int iy = 0; iy < q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int ix = x; ix < x + q; ix++)
					{
						if (CX1 > 0 && CX2 > 0 && CX3 > 0)
						{
							unsigned char r = 0, g = 255, b = 0;
							r = (unsigned char)rintf((float)CX1 * inv_denom * 255.0f);
							g = (unsigned char)rintf((float)CX2 * inv_denom * 255.0f);
							b = (unsigned char)rintf((float)CX3 * inv_denom * 255.0f);
							set_pixel_4i8(fb, ix, y + iy, r, g, b, 255);
							//buffer[ix] = 0x0000007F;   // Blue
						}

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					//(char*&)buffer += stride;
				}
			}
			else   // Partially covered block
			{
				int CY1 = C1 + DX12 * y0 - DY12 * x0;
				int CY2 = C2 + DX23 * y0 - DY23 * x0;
				int CY3 = C3 + DX31 * y0 - DY31 * x0;

				for (int iy = y; iy < y + q; iy++)
				{
					int CX1 = CY1;
					int CX2 = CY2;
					int CX3 = CY3;

					for (int ix = x; ix < x + q; ix++)
					{
						if (CX1 > 0 && CX2 > 0 && CX3 > 0)
						{
							unsigned char r = 0, g = 255, b = 0;
							r = (unsigned char)rintf((float)CX1 * inv_denom * 255.0f);
							g = (unsigned char)rintf((float)CX2 * inv_denom * 255.0f);
							b = (unsigned char)rintf((float)CX3 * inv_denom * 255.0f);
							set_pixel_4i8(fb, ix, iy, r, g, b, 255);
							//buffer[ix] = 0x0000007F;   // Blue
						}

						CX1 -= FDY12;
						CX2 -= FDY23;
						CX3 -= FDY31;
					}

					CY1 += FDX12;
					CY2 += FDX23;
					CY3 += FDX31;

					//(char*&)buffer += stride;
				}
			}
		}

		//(char*&)colorBuffer += q * stride;
	}
}

void draw_triangle4(framebuffer_f* alpha, framebuffer_f* beta, float v0_x, float v0_y, float v1_x, float v1_y, float v2_x, float v2_y)
{
	const int scale = 16;
	// 28.4 fixed-point coordinates
	const int Y1 = rintf((float)scale * v0_y);
	const int Y2 = rintf((float)scale * v1_y);
	const int Y3 = rintf((float)scale * v2_y);

	const int X1 = rintf((float)scale * v0_x);
	const int X2 = rintf((float)scale * v1_x);
	const int X3 = rintf((float)scale * v2_x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 * scale;
	const int FDX23 = DX23 * scale;
	const int FDX31 = DX31 * scale;

	const int FDY12 = DY12 * scale;
	const int FDY23 = DY23 * scale;
	const int FDY31 = DY31 * scale;

	// Bounding rectangle
	int minx = (min_int(min_int(X1, X2), X3) + (scale - 1)) / scale;
	int maxx = (max_int(max_int(X1, X2), X3) + (scale - 1)) / scale;
	int miny = (min_int(min_int(Y1, Y2), Y3) + (scale - 1)) / scale;
	int maxy = (max_int(max_int(Y1, Y2), Y3) + (scale - 1)) / scale;
	minx = minx - minx % 8;
	maxx = (maxx + 7) - (maxx + 7) % 8;

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	int CY1[8], CY2[8], CY3[8];
	for (int i = 0;i < 8;i++) {
		CY1[i] = C1 + DX12 * (miny * scale) - DY12 * (minx * scale) - FDY12 * i;
		CY2[i] = C2 + DX23 * (miny * scale) - DY23 * (minx * scale) - FDY23 * i;
		CY3[i] = C3 + DX31 * (miny * scale) - DY31 * (minx * scale) - FDY31 * i;
	}

	float inv_denom = 1.0f / ((float)CY1[0] + (float)CY2[0] + (float)CY3[0]);

	float* alpha_buffer = alpha->data;
	float* beta_buffer = beta->data;

	const __m256i fdx12_vec = _mm256_set1_epi32(FDX12);
	const __m256i fdx23_vec = _mm256_set1_epi32(FDX23);
	const __m256i fdx31_vec = _mm256_set1_epi32(FDX31);

	const __m256i eight_fdy12_vec = _mm256_set1_epi32(FDY12 * 8.0f);
	const __m256i eight_fdy23_vec = _mm256_set1_epi32(FDY23 * 8.0f);
	const __m256i eight_fdy31_vec = _mm256_set1_epi32(FDY31 * 8.0f);

	const __m256i zero_vec = _mm256_setzero_si256();
	const __m256 inv_denom_vec = _mm256_set1_ps(inv_denom);

	__m256i cy1_vec = _mm256_loadu_si256((__m256i*)CY1);
	__m256i cy2_vec = _mm256_loadu_si256((__m256i*)CY2);
	__m256i cy3_vec = _mm256_loadu_si256((__m256i*)CY3);

	for (int y = miny; y < maxy; y++)
	{
		// printf("%d\n", y);fflush(stdout);
		alpha_buffer = alpha->data + (y * alpha->width + minx);
		beta_buffer = beta->data + (y * beta->width + minx);

		__m256i cx1_vec = cy1_vec;
		__m256i cx2_vec = cy2_vec;
		__m256i cx3_vec = cy3_vec;

		for (int x = minx; x < maxx; x += 8) {
			// __m256i cmp1 = _mm256_cmpgt_epi32(cx1_vec, zero_vec);
			// __m256i cmp2 = _mm256_cmpgt_epi32(cx2_vec, zero_vec);
			// __m256i cmp3 = _mm256_cmpgt_epi32(cx3_vec, zero_vec);
			// __m256i mask = _mm256_and_si256(_mm256_and_si256(cmp1, cmp2), cmp3);
			// int mask_int = _mm256_movemask_epi8(mask);

			__m256i combined_signs = _mm256_or_si256(_mm256_or_si256(cx1_vec, cx2_vec), cx3_vec);
			__m256i mask = _mm256_cmpgt_epi32(combined_signs, zero_vec);
			int mask_int = _mm256_movemask_epi8(mask);

			if (mask_int != 0) {
				__m256 alpha_vec = _mm256_mul_ps(_mm256_cvtepi32_ps(cx2_vec), inv_denom_vec);
				__m256 beta_vec = _mm256_mul_ps(_mm256_cvtepi32_ps(cx3_vec), inv_denom_vec);
				if (mask_int == (int)0xFFFFFFFF) {
					_mm256_store_ps(alpha_buffer, alpha_vec);
					_mm256_store_ps(beta_buffer, beta_vec);
				}
				else {
					_mm256_maskstore_ps(alpha_buffer, mask, alpha_vec);
					_mm256_maskstore_ps(beta_buffer, mask, beta_vec);
				}
			}
			alpha_buffer += 8;
			beta_buffer += 8;

			cx1_vec = _mm256_sub_epi32(cx1_vec, eight_fdy12_vec);
			cx2_vec = _mm256_sub_epi32(cx2_vec, eight_fdy23_vec);
			cx3_vec = _mm256_sub_epi32(cx3_vec, eight_fdy31_vec);
		}
		cy1_vec = _mm256_add_epi32(cy1_vec, fdx12_vec);
		cy2_vec = _mm256_add_epi32(cy2_vec, fdx23_vec);
		cy3_vec = _mm256_add_epi32(cy3_vec, fdx31_vec);
	}
}

int main() {

	tio_init(&ctx);
	atexit(cleanup);
	printf("\x1b[2J");   // Clear screen
	printf("\x1b[H");    // Move cursor to home
	printf("\x1b[?25l"); // Hide cursor
	fflush(stdout);

	int rows, cols;
	if (tio_get_window_size(&ctx, &rows, &cols) == -1) {
		fprintf(stderr, "Unable to get window size\n");
		return 1;
	}
	cols = 960;
	rows = 540;
	framebuffer_4i8 fb = create_framebuffer_4i8(cols, rows);
	framebuffer_f alpha = create_framebuffer_f(cols, rows);
	framebuffer_f beta = create_framebuffer_f(cols, rows);
	//float v0_x = (float)cols / 2;
	//float v0_y = 10.0f;
	//float v1_x = 10.0f;
	//float v1_y = (float)rows - 10.0f;
	//float v2_x = (float)cols - 10.0f;
	//float v2_y = (float)rows - 10.0f;
	float v0_x = -16.881523, v0_y = -64.941948, v1_x = 320.140137, v1_y = -64.941948, v2_x = 320.140137, v2_y = 255.625458;
	sixel_display_ctx sixel_ctx;
	init_sixel_display_ctx(&sixel_ctx, cols, rows);
	init_sixel_indexed_bitmap(&sixel_ctx.bitmap, cols, rows);
	init_sixel_palette_rgbuniform(&sixel_ctx.bitmap.palette, 5);

	monotonic_timer_t timer, timer_whole;
	timer_start(&timer_whole);

	double total_draw_time = 0.0;
	double total_conversion_time = 0.0;
	double total_generation_time = 0.0;
	double total_display_time = 0.0;

	double draw_time = 0.0;
	double conversion_time = 0.0;
	double generation_time = 0.0;
	double display_time = 0.0;

	int draw_mode = 2;

	int num_frames = 1000;
	while (num_frames--) {
		int current_event_queue_bytes_size = tio_get_event_queue_byte_size(&ctx);
		int event_bytes_processed = 0;
		while (event_bytes_processed < current_event_queue_bytes_size) {
			tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
			int bytes_processed = tio_pop_event_queue(&ctx, &event);
			event_bytes_processed += bytes_processed;
			if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
				if (event.code == 'Q' || event.code == CTRL_Q) {
					goto end;
				}
				if (event.code == '1') {
					draw_mode = 1;
				}
				else if (event.code == '2') {
					draw_mode = 2;
				}
				else if (event.code == '3') {
					draw_mode = 3;
				}
				else if (event.code == '4') {
					draw_mode = 4;
				}
				else if (event.code == '5') {
					draw_mode = 5;
				}
			}
		}


		for (int i = 0;i < 4;i++) {
			clear_framebuffer_4i8(&fb, 0, 0, 0, 255);
			clear_framebuffer_f(&alpha, nanf(""));
			clear_framebuffer_f(&beta, nanf(""));

			timer_start(&timer);
			if (draw_mode == 1)draw_triangle(&fb, v0_x, v0_y, v1_x, v1_y, v2_x, v2_y);
			if (draw_mode == 2)draw_triangle2(&alpha, &beta, v0_x, v0_y, v1_x, v1_y, v2_x, v2_y);
			if (draw_mode == 3)draw_triangle3(&fb, v0_x, v0_y, v1_x, v1_y, v2_x, v2_y);
			if (draw_mode == 4)draw_triangle4(&alpha, &beta, v0_x, v0_y, v1_x, v1_y, v2_x, v2_y);
			draw_time = timer_elapsed_ms(&timer);
			total_draw_time += draw_time;
		}

		timer_start(&timer);
		if (draw_mode == 1)convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors(&sixel_ctx.bitmap, fb);
		if (draw_mode == 2 || draw_mode == 4)convert_alpha_beta_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors(&sixel_ctx.bitmap, alpha, beta);
		conversion_time = timer_elapsed_ms(&timer);
		total_conversion_time += conversion_time;

		timer_start(&timer);
		generate_sixel_display_data(&sixel_ctx);
		generation_time = timer_elapsed_ms(&timer);
		total_generation_time += generation_time;

		timer_start(&timer);
		if (tio_write(&ctx, sixel_ctx.data, sixel_ctx.data_size) == -1) {
			goto end;
		}
		double display_elapsed_ms = timer_elapsed_ms(&timer);
		total_display_time += display_elapsed_ms;

		printf("\x1b[H");    // Move cursor to home
		printf("\r\n");
		printf("Draw:          %0.2f\r\n", draw_time);
		printf("Conversion:    %0.2f\r\n", conversion_time);
		printf("Generation:    %0.2f\r\n", generation_time);
		printf("Display:       %0.2f\r\n", display_time);
		fflush(stdout);
	}

end:

	printf("\r\n");
	printf("Total times:\r\n");
	printf("Draw:          %0.2f\r\n", total_draw_time);
	printf("Conversion:    %0.2f\r\n", total_conversion_time);
	printf("Generation:    %0.2f\r\n", total_generation_time);
	printf("Display:       %0.2f\r\n", total_display_time);
	fflush(stdout);
	free_framebuffer_4i8(&fb);


	double whole_elapsed_ms = timer_elapsed_ms(&timer_whole);
	printf("\r\nTotal time for %d frames: %0.2f ms\r\n", 100, whole_elapsed_ms);
	printf("\x1b[?25h"); // Show cursor
	fflush(stdout);

	return 0;

}