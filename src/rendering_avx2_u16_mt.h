#ifndef RENDERING_H
#define RENDERING_H

#include "cglm/cglm.h"
#include "framebuffer_4i8.h"
#include "framebuffer_i32.h"
#include "framebuffer_u16.h"
#include "framebuffer_f.h"
#include "mesh_loading.h"
#include "utils.h"
#include "timer.h"
#include "textures.h"
#include "omp.h"

float near_plane = 1.0f;
float far_plane = 100.0f;

const int fixed_point_scale = 16;

typedef struct {
	float position_x, position_y, position_z;
	float texcoord_u, texcoord_v;
} raw_vertex_t;

typedef struct {
	float homogenous_position_x, homogenous_position_y, homogenous_position_z, homogenous_position_w;
	float texcoord_u, texcoord_v;
} processed_vertex_t;

typedef struct {
	processed_vertex_t v0, v1, v2;
	int32_t diffuse_atlas_offset;
	int tex_width;
	int tex_height;
} processed_triangle_t;

typedef struct {
	mat3 normal_transform; 
	mat4 model_view_projection; 
	mat4 model_view;
}render_params_t;

void render_params_copy(render_params_t* dest, render_params_t* src) {
	glm_mat3_copy(src->normal_transform, dest->normal_transform);
	glm_mat4_copy(src->model_view_projection, dest->model_view_projection);
	glm_mat4_copy(src->model_view, dest->model_view);
}

typedef struct {
	int starty, endy;
	int width, height;
	double total_clear_time;
	double total_rasterisation_time;
	double total_texture_sampling_time;
	monotonic_timer_t timer;
	framebuffer_i32 index_buffer;
	framebuffer_f depth_buffer;
	framebuffer_u16 output_buffer;
	render_params_t params;
} rendering_ctx_t;

rendering_ctx_t create_rendering_ctx(int width, int height, int starty, int endy) {
	rendering_ctx_t ctx;
	int buffer_height = endy - starty;
	int buffer_width = width;
	ctx.index_buffer = create_framebuffer_i32(buffer_width, buffer_height);
	ctx.depth_buffer = create_framebuffer_f(buffer_width, buffer_height);
	ctx.output_buffer = create_framebuffer_u16(buffer_width, buffer_height);
	ctx.height = height;
	ctx.width = width;
	ctx.starty = starty;
	ctx.endy = endy;
	ctx.total_clear_time = 0.0;
	ctx.total_rasterisation_time = 0.0;
	ctx.total_texture_sampling_time = 0.0;
	return ctx;
}

void free_rendering_ctx(rendering_ctx_t* ctx) {
	free_framebuffer_i32(&ctx->index_buffer);
	free_framebuffer_f(&ctx->depth_buffer);
	free_framebuffer_u16(&ctx->output_buffer);
}

/* =========================================================== */
/*  Vertex Processing                                          */
/* =========================================================== */
raw_vertex_t create_vertex(tinyobj_attrib_t attrib, tinyobj_vertex_index_t idx) {
	raw_vertex_t input;
	input.position_x = attrib.vertices[3 * idx.v_idx + 0];
	input.position_y = attrib.vertices[3 * idx.v_idx + 1];
	input.position_z = attrib.vertices[3 * idx.v_idx + 2];
	if (idx.vt_idx >= 0) {
		input.texcoord_u = attrib.texcoords[2 * idx.vt_idx + 0];
		input.texcoord_v = attrib.texcoords[2 * idx.vt_idx + 1];
	}
	return input;
}

processed_vertex_t process_vertex(raw_vertex_t input, mat4 model_view_projection) {
	vec4 position = { input.position_x, input.position_y, input.position_z, 1.0f };
	vec4 homogenous_position;
	glm_mat4_mulv(model_view_projection, position, homogenous_position);
	processed_vertex_t processed_vertex = {
		.homogenous_position_x = homogenous_position[0],
		.homogenous_position_y = homogenous_position[1],
		.homogenous_position_z = homogenous_position[2],
		.homogenous_position_w = homogenous_position[3],
		.texcoord_u = input.texcoord_u,
		.texcoord_v = input.texcoord_v,
	};
	return processed_vertex;
}

processed_vertex_t process_vertex_2(vec4* ndc_arr, tinyobj_attrib_t attrib, tinyobj_vertex_index_t idx) {
	processed_vertex_t processed_vertex = {
		.homogenous_position_x = ndc_arr[idx.v_idx][0],
		.homogenous_position_y = ndc_arr[idx.v_idx][1],
		.homogenous_position_z = ndc_arr[idx.v_idx][2],
		.homogenous_position_w = ndc_arr[idx.v_idx][3],
		.texcoord_u = attrib.texcoords[2 * idx.vt_idx + 0],
		.texcoord_v = attrib.texcoords[2 * idx.vt_idx + 1],
	};
	return processed_vertex;
}


/* =========================================================== */
/*  Triangle Clipping                                          */
/* =========================================================== */
static inline int is_inside_near_plane(processed_vertex_t v) {
	return v.homogenous_position_z >= -v.homogenous_position_w;
}

static inline int compute_outcode(processed_vertex_t v) {
	float x = v.homogenous_position_x;
	float y = v.homogenous_position_y;
	float z = v.homogenous_position_z;
	float w = v.homogenous_position_w;
	return ((z <= w))
		| ((z >= -w) << 1)
		| ((x >= -w) << 2)
		| ((x <= w) << 3)
		| ((y <= w) << 4)
		| ((y >= -w) << 5);
}

static inline int triangle_is_fully_clipped(processed_triangle_t tri) {
	int c0 = compute_outcode(tri.v0);
	int c1 = compute_outcode(tri.v1);
	int c2 = compute_outcode(tri.v2);

	// A bit being 0 in all three means all verts are outside that plane
	// AND them together: any 0-bit in the result = fully outside one plane
	return (~(c0 | c1 | c2)) & 0x3F;
}

static inline float get_lerp_parameter(processed_vertex_t inside, processed_vertex_t outside) {
	float d_in = inside.homogenous_position_z + inside.homogenous_position_w;
	float d_out = outside.homogenous_position_z + outside.homogenous_position_w;
	float denom = d_out - d_in;
	if (fabsf(denom) < 1e-6f * fabsf(d_in))
		return 0.0f;
	return glm_clamp(-d_in / denom, 0.0f, 1.0f);
}

processed_vertex_t lerp_processed_vertex_pair(processed_vertex_t v0, processed_vertex_t v1, float t) {
	processed_vertex_t out;
	out.homogenous_position_x = v0.homogenous_position_x + t * (v1.homogenous_position_x - v0.homogenous_position_x);
	out.homogenous_position_y = v0.homogenous_position_y + t * (v1.homogenous_position_y - v0.homogenous_position_y);
	out.homogenous_position_z = v0.homogenous_position_z + t * (v1.homogenous_position_z - v0.homogenous_position_z);
	out.homogenous_position_w = v0.homogenous_position_w + t * (v1.homogenous_position_w - v0.homogenous_position_w);
	out.texcoord_u = v0.texcoord_u + t * (v1.texcoord_u - v0.texcoord_u);
	out.texcoord_v = v0.texcoord_v + t * (v1.texcoord_v - v0.texcoord_v);
	return out;
}

void clip_triangle(processed_triangle_t in, processed_triangle_t* out1, processed_triangle_t* out2, int* num_out) {
	*out1 = in;
	*num_out = 1;
	int vertex_count = 0;
	if (is_inside_near_plane(in.v0)) vertex_count++;
	if (is_inside_near_plane(in.v1)) vertex_count++;
	if (is_inside_near_plane(in.v2)) vertex_count++;
	if (vertex_count == 0) {
		*num_out = 0;
		return;
	}
	if (vertex_count == 3) {
		*out1 = in;
		*num_out = 1;
		return;
	}
	if (vertex_count == 1) {
		processed_vertex_t inside_vertex, outside_vertex1, outside_vertex2;
		if (is_inside_near_plane(in.v0)) {
			inside_vertex = in.v0;
			outside_vertex1 = in.v1;
			outside_vertex2 = in.v2;
		}
		else if (is_inside_near_plane(in.v1)) {
			inside_vertex = in.v1;
			outside_vertex1 = in.v2;
			outside_vertex2 = in.v0;
		}
		else {
			inside_vertex = in.v2;
			outside_vertex1 = in.v0;
			outside_vertex2 = in.v1;
		}
		float t1 = get_lerp_parameter(inside_vertex, outside_vertex1);
		float t2 = get_lerp_parameter(inside_vertex, outside_vertex2);
		processed_vertex_t new_vertex1 = lerp_processed_vertex_pair(inside_vertex, outside_vertex1, t1);
		processed_vertex_t new_vertex2 = lerp_processed_vertex_pair(inside_vertex, outside_vertex2, t2);
		out1->v0 = inside_vertex;
		out1->v1 = new_vertex1;
		out1->v2 = new_vertex2;
		*num_out = 1;
		return;
	}
	if (vertex_count == 2) {
		processed_vertex_t inside_vertex1, inside_vertex2, outside_vertex;
		if (!is_inside_near_plane(in.v0)) {
			outside_vertex = in.v0;
			inside_vertex1 = in.v1;
			inside_vertex2 = in.v2;
		}
		else if (!is_inside_near_plane(in.v1)) {
			outside_vertex = in.v1;
			inside_vertex1 = in.v2;
			inside_vertex2 = in.v0;
		}
		else {
			outside_vertex = in.v2;
			inside_vertex1 = in.v0;
			inside_vertex2 = in.v1;
		}
		float t1 = get_lerp_parameter(inside_vertex1, outside_vertex);
		float t2 = get_lerp_parameter(inside_vertex2, outside_vertex);
		processed_vertex_t new_vertex1 = lerp_processed_vertex_pair(inside_vertex1, outside_vertex, t1);
		processed_vertex_t new_vertex2 = lerp_processed_vertex_pair(inside_vertex2, outside_vertex, t2);
		out1->v0 = inside_vertex1;
		out1->v1 = new_vertex1;
		out1->v2 = inside_vertex2;
		out2->v0 = inside_vertex2;
		out2->v1 = new_vertex1;
		out2->v2 = new_vertex2;
		*num_out = 2;
		return;
	}
}

/* =========================================================== */
/*  Rasterization                                              */
/* =========================================================== */
void viewport_transform(processed_vertex_t* v, int width, int height, float* x, float* y) {
	*x = ((v->homogenous_position_x / v->homogenous_position_w + 1.0f) * 0.5f * (float)width);
	*y = ((1.0f - (v->homogenous_position_y / v->homogenous_position_w + 1.0f) * 0.5f) * (float)height);
}

float get_processed_triangle_area(processed_triangle_t triangle, int width, int height) {
	float x0, y0, x1, y1, x2, y2;
	viewport_transform(&triangle.v0, width, height, &x0, &y0);
	viewport_transform(&triangle.v1, width, height, &x1, &y1);
	viewport_transform(&triangle.v2, width, height, &x2, &y2);
	return 0.5f * fabsf(x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1));
}

static inline __m256i modulo_int_avx2(__m256i x, int modulus) {
	__m256i mod = _mm256_set1_epi32(modulus);
	__m256 fa = _mm256_cvtepi32_ps(x);
	__m256 fb = _mm256_cvtepi32_ps(mod);
	__m256 quotient = _mm256_div_ps(fa, fb);
	__m256 truncated = _mm256_round_ps(quotient, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
	__m256 prod = _mm256_mul_ps(truncated, fb);
	__m256 rem_f = _mm256_sub_ps(fa, prod);
	__m256i result = _mm256_cvttps_epi32(rem_f);

	__m256i mask = _mm256_cmpgt_epi32(_mm256_setzero_si256(), result);
	result = _mm256_add_epi32(result, _mm256_and_si256(mask, mod));
	return result;
}

static inline __m256 dot_product_3vec8f(__m256 a0, __m256 a1, __m256 a2, __m256 b0, __m256 b1, __m256 b2) {
	return _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(a0, b0), _mm256_mul_ps(a1, b1)), _mm256_mul_ps(a2, b2));
}

void rasterize_triangle_avx2_texture_index_only(processed_triangle_t triangle, rendering_ctx_t* ctx) {
	framebuffer_i32* index_buffer = &ctx->index_buffer;
	framebuffer_f* depth_buffer = &ctx->depth_buffer;
	int starty = ctx->starty, endy = ctx->endy;
	int width = ctx->width, height = ctx->height;

	int diffuse_atlas_offset = triangle.diffuse_atlas_offset;
	int tex_width = triangle.tex_width;
	int tex_height = triangle.tex_height;
	int is_textured = (tex_width != 0 && tex_height != 0);

	float v0_x, v0_y, v1_x, v1_y, v2_x, v2_y;
	viewport_transform(&triangle.v0, width, height, &v0_x, &v0_y);
	viewport_transform(&triangle.v1, width, height, &v1_x, &v1_y);
	viewport_transform(&triangle.v2, width, height, &v2_x, &v2_y);

	// 28.4 fixed-point coordinates
	const int Y1 = rintf((float)fixed_point_scale * v0_y);
	const int Y2 = rintf((float)fixed_point_scale * v1_y);
	const int Y3 = rintf((float)fixed_point_scale * v2_y);

	const int X1 = rintf((float)fixed_point_scale * v0_x);
	const int X2 = rintf((float)fixed_point_scale * v1_x);
	const int X3 = rintf((float)fixed_point_scale * v2_x);

	// Deltas
	const int DX12 = X1 - X2;
	const int DX23 = X2 - X3;
	const int DX31 = X3 - X1;

	const int DY12 = Y1 - Y2;
	const int DY23 = Y2 - Y3;
	const int DY31 = Y3 - Y1;

	// Fixed-point deltas
	const int FDX12 = DX12 * fixed_point_scale;
	const int FDX23 = DX23 * fixed_point_scale;
	const int FDX31 = DX31 * fixed_point_scale;

	const int FDY12 = DY12 * fixed_point_scale;
	const int FDY23 = DY23 * fixed_point_scale;
	const int FDY31 = DY31 * fixed_point_scale;

	// Bounding rectangle
	int minx = (min_int(min_int(X1, X2), X3) + (fixed_point_scale - 1)) / fixed_point_scale;
	int maxx = (max_int(max_int(X1, X2), X3) + (fixed_point_scale - 1)) / fixed_point_scale;
	int miny = (min_int(min_int(Y1, Y2), Y3) + (fixed_point_scale - 1)) / fixed_point_scale;
	int maxy = (max_int(max_int(Y1, Y2), Y3) + (fixed_point_scale - 1)) / fixed_point_scale;
	minx = minx - minx % 8;
	maxx = (maxx + 7) - (maxx + 7) % 8;
	minx = clamp_int(minx, 0, width - 1);
	maxx = clamp_int(maxx, 0, width - 1);
	miny = clamp_int(miny, starty, endy - 1);
	maxy = clamp_int(maxy, starty, endy - 1);

	if (minx >= maxx || miny >= maxy) return;

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	int area = C1 + C2 + C3;
	if (area < fixed_point_scale && area > -fixed_point_scale) return; // Degenerate triangle, skip

	float inv_area = 1.0f / (float)area;
	const __m256 inv_area_vec = _mm256_set1_ps(inv_area);

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	int CY1[8], CY2[8], CY3[8];
	for (int i = 0; i < 8; i++) {
		CY1[i] = C1 + FDX12 * (miny + 0) - FDY12 * (minx + i);
		CY2[i] = C2 + FDX23 * (miny + 0) - FDY23 * (minx + i);
		CY3[i] = C3 + FDX31 * (miny + 0) - FDY31 * (minx + i);
	}

	__m256i cy1_vec = _mm256_loadu_si256((__m256i*)CY1);
	__m256i cy2_vec = _mm256_loadu_si256((__m256i*)CY2);
	__m256i cy3_vec = _mm256_loadu_si256((__m256i*)CY3);

	int ystride = 1;

	const __m256i fdx12_vec = _mm256_set1_epi32(FDX12 * ystride);
	const __m256i fdx23_vec = _mm256_set1_epi32(FDX23 * ystride);
	const __m256i fdx31_vec = _mm256_set1_epi32(FDX31 * ystride);

	const __m256i eight_fdy12_vec = _mm256_set1_epi32(FDY12 * 8);
	const __m256i eight_fdy23_vec = _mm256_set1_epi32(FDY23 * 8);
	const __m256i eight_fdy31_vec = _mm256_set1_epi32(FDY31 * 8);

	const __m256i zero_vec = _mm256_setzero_si256();

	const __m256 v0w_inv_vec = _mm256_set1_ps(1.0f / triangle.v0.homogenous_position_w);
	const __m256 v1w_inv_vec = _mm256_set1_ps(1.0f / triangle.v1.homogenous_position_w);
	const __m256 v2w_inv_vec = _mm256_set1_ps(1.0f / triangle.v2.homogenous_position_w);

	const __m256 v0u_vec = _mm256_set1_ps(triangle.v0.texcoord_u);
	const __m256 v1u_vec = _mm256_set1_ps(triangle.v1.texcoord_u);
	const __m256 v2u_vec = _mm256_set1_ps(triangle.v2.texcoord_u);
	const __m256 v0v_vec = _mm256_set1_ps(triangle.v0.texcoord_v);
	const __m256 v1v_vec = _mm256_set1_ps(triangle.v1.texcoord_v);
	const __m256 v2v_vec = _mm256_set1_ps(triangle.v2.texcoord_v);

	const __m256i diffuse_atlas_offset_vec = _mm256_set1_epi32(diffuse_atlas_offset);

	int32_t* index_buffer_ptr = index_buffer->data;
	float* depth_buffer_ptr = depth_buffer->data;


	for (int y = miny; y <= maxy; y += 1)
	{
		index_buffer_ptr = index_buffer->data + ((y - starty) * width + minx);
		depth_buffer_ptr = depth_buffer->data + ((y - starty) * width + minx);

		__m256i cx1_vec = cy1_vec;
		__m256i cx2_vec = cy2_vec;
		__m256i cx3_vec = cy3_vec;
		for (int x = minx; x < maxx; x += 8) {
			__m256i cmp1 = _mm256_cmpgt_epi32(cx1_vec, zero_vec);
			__m256i cmp2 = _mm256_cmpgt_epi32(cx2_vec, zero_vec);
			__m256i cmp3 = _mm256_cmpgt_epi32(cx3_vec, zero_vec);
			__m256i positive_mask = _mm256_and_si256(_mm256_and_si256(cmp1, cmp2), cmp3);
			__m256i negative_mask = _mm256_xor_si256(_mm256_or_si256(_mm256_or_si256(cmp1, cmp2), cmp3), _mm256_set1_epi32(-1));
			__m256i inside_mask = _mm256_or_si256(positive_mask, negative_mask);

			int inside_mask_int = _mm256_movemask_epi8(inside_mask);
			if (inside_mask_int == 0) {
				cx1_vec = _mm256_sub_epi32(cx1_vec, eight_fdy12_vec);
				cx2_vec = _mm256_sub_epi32(cx2_vec, eight_fdy23_vec);
				cx3_vec = _mm256_sub_epi32(cx3_vec, eight_fdy31_vec);
				index_buffer_ptr += 8;
				depth_buffer_ptr += 8;
				continue;
			}

			__m256 alpha_vec = _mm256_mul_ps(_mm256_cvtepi32_ps(cx2_vec), inv_area_vec);
			__m256 beta_vec = _mm256_mul_ps(_mm256_cvtepi32_ps(cx3_vec), inv_area_vec);
			__m256 gamma_vec = _mm256_mul_ps(_mm256_cvtepi32_ps(cx1_vec), inv_area_vec);

			__m256 w_interp_vec = _mm256_rcp_ps(dot_product_3vec8f(alpha_vec, beta_vec, gamma_vec, v0w_inv_vec, v1w_inv_vec, v2w_inv_vec));
			__m256 current_depth_vec = _mm256_load_ps(depth_buffer_ptr);
			__m256i depth_mask = _mm256_castps_si256(_mm256_cmp_ps(w_interp_vec, current_depth_vec, _CMP_LT_OQ));

			__m256i mask = _mm256_and_si256(depth_mask, inside_mask);
			int mask_int = _mm256_movemask_epi8(mask);

			__m256 coeff0_vec = _mm256_mul_ps(alpha_vec, v0w_inv_vec);
			__m256 coeff1_vec = _mm256_mul_ps(beta_vec, v1w_inv_vec);
			__m256 coeff2_vec = _mm256_mul_ps(gamma_vec, v2w_inv_vec);
			__m256 interp_u = _mm256_mul_ps(
				dot_product_3vec8f(coeff0_vec, coeff1_vec, coeff2_vec, v0u_vec, v1u_vec, v2u_vec),
				w_interp_vec
			);
			__m256 interp_v = _mm256_mul_ps(
				dot_product_3vec8f(coeff0_vec, coeff1_vec, coeff2_vec, v0v_vec, v1v_vec, v2v_vec),
				w_interp_vec
			);

			if (mask_int != 0) {
				__m256i index_vec = diffuse_atlas_offset_vec;
				if (is_textured) {
					__m256 one_minus_interp_v = _mm256_sub_ps(_mm256_set1_ps(1.0f), interp_v);
					__m256i tex_x = _mm256_cvtps_epi32(_mm256_mul_ps(interp_u, _mm256_set1_ps((float)(tex_width - 1))));
					__m256i tex_y = _mm256_cvtps_epi32(_mm256_mul_ps(one_minus_interp_v, _mm256_set1_ps((float)(tex_height - 1))));
					__m256i tex_width_vec = _mm256_set1_epi32(tex_width);
					tex_x = modulo_int_avx2(tex_x, tex_width);
					tex_y = modulo_int_avx2(tex_y, tex_height);

					__m256i texture_index_vec = _mm256_add_epi32(_mm256_mullo_epi32(tex_y, tex_width_vec), tex_x);
					index_vec = _mm256_add_epi32(texture_index_vec, index_vec);
				}
				if (mask_int == (int)0xFFFFFFFF) {
					_mm256_store_si256((__m256i*)index_buffer_ptr, index_vec);
					_mm256_store_ps(depth_buffer_ptr, w_interp_vec);
				}
				else {
					_mm256_maskstore_epi32((int*)index_buffer_ptr, mask, index_vec);
					_mm256_maskstore_ps(depth_buffer_ptr, mask, w_interp_vec);
				}
			}
			index_buffer_ptr += 8;
			depth_buffer_ptr += 8;

			cx1_vec = _mm256_sub_epi32(cx1_vec, eight_fdy12_vec);
			cx2_vec = _mm256_sub_epi32(cx2_vec, eight_fdy23_vec);
			cx3_vec = _mm256_sub_epi32(cx3_vec, eight_fdy31_vec);
		}
		cy1_vec = _mm256_add_epi32(cy1_vec, fdx12_vec);
		cy2_vec = _mm256_add_epi32(cy2_vec, fdx23_vec);
		cy3_vec = _mm256_add_epi32(cy3_vec, fdx31_vec);
	}
}

void texture_sample_pass_5r6g5b(rendering_ctx_t* ctx, texture_atlas_t* atlas) {
	framebuffer_i32* index_buffer = &ctx->index_buffer;
	framebuffer_u16* output_buffer = &ctx->output_buffer;
	int width = index_buffer->width, height = index_buffer->height;
	int length = width * height;
	int32_t* ib_data = index_buffer->data;
	uint16_t* fb_data = output_buffer->data;
	uint16_t* atlas_data = (uint16_t*)atlas->data;
	for (int i = 0; i < length; i += 1) {
		int index = ib_data[i];
		if (index < 0 || index >= atlas->length) {
			fb_data[i] = convert_8r8g8b8a_to_5r6g5b(255, 0, 255);
			continue;
		}
		fb_data[i] = atlas_data[index];
	}
}


/* =========================================================== */
/*  Main Render Function                                       */
/* =========================================================== */

static inline processed_triangle_t make_triangle(mesh_t* mesh, int i, mat4 mvp) {
	raw_vertex_t vert_input0 = create_vertex(mesh->attrib, mesh->attrib.faces[i * 3 + 0]);
	raw_vertex_t vert_input1 = create_vertex(mesh->attrib, mesh->attrib.faces[i * 3 + 1]);
	raw_vertex_t vert_input2 = create_vertex(mesh->attrib, mesh->attrib.faces[i * 3 + 2]);

	processed_triangle_t triangle;
	triangle.v0 = process_vertex(vert_input0, mvp);
	triangle.v1 = process_vertex(vert_input1, mvp);
	triangle.v2 = process_vertex(vert_input2, mvp);

	int mat_idx = mesh->attrib.material_ids[i];
	triangle.tex_width = 0;
	triangle.tex_height = 0;
	triangle.diffuse_atlas_offset = 0;
	if (mat_idx != -1) {
		triangle.tex_width = mesh->materials[mat_idx].texture_width;
		triangle.tex_height = mesh->materials[mat_idx].texture_height;
		triangle.diffuse_atlas_offset = mesh->materials[mat_idx].diffuse_atlas_offset;
	}
	return triangle;
}

void render_mesh(mesh_t* mesh, rendering_ctx_t* ctx) {
	timer_start(&ctx->timer);
	clear_framebuffer_f(&ctx->depth_buffer, far_plane);
	clear_framebuffer_u16(&ctx->output_buffer, (uint16_t)0);
	clear_framebuffer_i32(&ctx->index_buffer, (int32_t)0);
	double clear_time = timer_elapsed_ms(&ctx->timer);
	ctx->total_clear_time += clear_time;

	timer_start(&ctx->timer);
	for (int i = (int)mesh->start_triangle_index; i < (int)mesh->end_triangle_index; i++) {
		processed_triangle_t triangle = make_triangle(mesh, i, ctx->params.model_view_projection);
		if (triangle_is_fully_clipped(triangle)) {
			continue;
		}
		
		processed_triangle_t clipped_triangle0, clipped_triangle1;
		int num_clipped_triangles = 0;
		clip_triangle(triangle, &clipped_triangle0, &clipped_triangle1, &num_clipped_triangles);
		clipped_triangle0.diffuse_atlas_offset = triangle.diffuse_atlas_offset;
		clipped_triangle0.tex_width = triangle.tex_width;
		clipped_triangle0.tex_height = triangle.tex_height;
		clipped_triangle1.diffuse_atlas_offset = triangle.diffuse_atlas_offset;
		clipped_triangle1.tex_width = triangle.tex_width;
		clipped_triangle1.tex_height = triangle.tex_height;
		
		if (num_clipped_triangles == 0) continue;
		rasterize_triangle_avx2_texture_index_only(clipped_triangle0, ctx);
		if (num_clipped_triangles == 1) continue;
		rasterize_triangle_avx2_texture_index_only(clipped_triangle1, ctx);

	}
	double rasterisation_time = timer_elapsed_ms(&ctx->timer);
	ctx->total_rasterisation_time += rasterisation_time;

	timer_start(&ctx->timer);
	texture_sample_pass_5r6g5b(ctx, &mesh->diffuse_atlas);
	double texture_sampling_time = timer_elapsed_ms(&ctx->timer);
	ctx->total_texture_sampling_time += texture_sampling_time;

}

#endif
