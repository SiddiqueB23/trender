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

float near_plane = 1.0f;
float far_plane = 100.0f;

const int fixed_point_scale = 16;

typedef struct {
	float position_x, position_y, position_z;
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
	float x, y, z, w;
} clip_vertex_t;

typedef struct {
	clip_vertex_t v0, v1, v2;
} clip_triangle_t;

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
	int startx, endx;
	int starty, endy;
	int width, height;
	int chunk_index;   /* which output chunk this ctx renders; tested against hint_mask bits */
	double total_clear_time;
	double total_rasterisation_time;
	double total_texture_sampling_time;
	monotonic_timer_t timer;
	framebuffer_i32 index_buffer;
	framebuffer_f depth_buffer;
	uint16_t* output_start; /* pointer into the shared combined-row buffer at this chunk's (startx, row); stride == width (full frame) */
	render_params_t params;
} rendering_ctx_t;

rendering_ctx_t create_rendering_ctx(int width, int height, int startx, int endx, int starty, int endy) {
	rendering_ctx_t ctx;
	int buffer_width = endx - startx;
	int buffer_height = endy - starty;
	ctx.index_buffer = create_framebuffer_i32(buffer_width, buffer_height);
	ctx.depth_buffer = create_framebuffer_f(buffer_width, buffer_height);
	ctx.output_start = NULL;
	ctx.height = height;
	ctx.width = width;
	ctx.startx = startx;
	ctx.endx = endx;
	ctx.starty = starty;
	ctx.endy = endy;
	ctx.chunk_index = 0;
	ctx.total_clear_time = 0.0;
	ctx.total_rasterisation_time = 0.0;
	ctx.total_texture_sampling_time = 0.0;
	return ctx;
}

void free_rendering_ctx(rendering_ctx_t* ctx) {
	free_framebuffer_i32(&ctx->index_buffer);
	free_framebuffer_f(&ctx->depth_buffer);
}

/* =========================================================== */
/*  Vertex Processing                                          */
/* =========================================================== */
raw_vertex_t create_vertex(float* raw_vertices, int* indices, int local_fv) {
	int v = indices[local_fv];
	raw_vertex_t input;
	input.position_x = raw_vertices[3 * v + 0];
	input.position_y = raw_vertices[3 * v + 1];
	input.position_z = raw_vertices[3 * v + 2];
	return input;
}

clip_vertex_t process_vertex(raw_vertex_t input, mat4 model_view_projection) {
	vec4 position = { input.position_x, input.position_y, input.position_z, 1.0f };
	vec4 homogenous_position;
	glm_mat4_mulv(model_view_projection, position, homogenous_position);
	clip_vertex_t v = {
		.x = homogenous_position[0],
		.y = homogenous_position[1],
		.z = homogenous_position[2],
		.w = homogenous_position[3],
	};
	return v;
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

static inline int is_inside_near_plane_clip(clip_vertex_t v) {
	return v.z >= -v.w;
}

static inline int compute_outcode(clip_vertex_t v) {
	float x = v.x;
	float y = v.y;
	float z = v.z;
	float w = v.w;
	return ((z <= w))
		| ((z >= -w) << 1)
		| ((x >= -w) << 2)
		| ((x <= w) << 3)
		| ((y <= w) << 4)
		| ((y >= -w) << 5);
}

static inline int triangle_is_fully_clipped(clip_triangle_t tri) {
	int c0 = compute_outcode(tri.v0);
	int c1 = compute_outcode(tri.v1);
	int c2 = compute_outcode(tri.v2);

	// A bit being 0 in all three means all verts are outside that plane.
	// OR them together: any 0-bit in the result means all verts are outside that plane.
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

static inline void viewport_transform_clip(clip_vertex_t* v, int width, int height, float* x, float* y) {
	*x = ((v->x / v->w + 1.0f) * 0.5f * (float)width);
	*y = ((1.0f - (v->y / v->w + 1.0f) * 0.5f) * (float)height);
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

void rasterize_triangle_avx2_texture_index_only(processed_triangle_t* triangle, rendering_ctx_t* ctx) {
	framebuffer_i32* index_buffer = &ctx->index_buffer;
	framebuffer_f* depth_buffer = &ctx->depth_buffer;
	int startx = ctx->startx, endx = ctx->endx;
	int starty = ctx->starty, endy = ctx->endy;
	int width = ctx->width, height = ctx->height;

	int diffuse_atlas_offset = triangle->diffuse_atlas_offset;
	int tex_width = triangle->tex_width;
	int tex_height = triangle->tex_height;
	int is_textured = (tex_width != 0 && tex_height != 0);

	float v0_x, v0_y, v1_x, v1_y, v2_x, v2_y;
	viewport_transform(&triangle->v0, width, height, &v0_x, &v0_y);
	viewport_transform(&triangle->v1, width, height, &v1_x, &v1_y);
	viewport_transform(&triangle->v2, width, height, &v2_x, &v2_y);

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
	minx = max_int(minx, startx);
	maxx = min_int(maxx, endx - 1);
	// miny = clamp_int(miny, starty, endy - 1);
	// maxy = clamp_int(maxy, starty, endy - 1);
	miny = max_int(miny, starty);
	maxy = min_int(maxy, endy - 1);
	
	if (minx > maxx || miny > maxy) return;

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

	const __m256 v0w_inv_vec = _mm256_set1_ps(1.0f / triangle->v0.homogenous_position_w);
	const __m256 v1w_inv_vec = _mm256_set1_ps(1.0f / triangle->v1.homogenous_position_w);
	const __m256 v2w_inv_vec = _mm256_set1_ps(1.0f / triangle->v2.homogenous_position_w);

	const __m256 v0u_vec = _mm256_set1_ps(triangle->v0.texcoord_u);
	const __m256 v1u_vec = _mm256_set1_ps(triangle->v1.texcoord_u);
	const __m256 v2u_vec = _mm256_set1_ps(triangle->v2.texcoord_u);
	const __m256 v0v_vec = _mm256_set1_ps(triangle->v0.texcoord_v);
	const __m256 v1v_vec = _mm256_set1_ps(triangle->v1.texcoord_v);
	const __m256 v2v_vec = _mm256_set1_ps(triangle->v2.texcoord_v);

	const __m256i diffuse_atlas_offset_vec = _mm256_set1_epi32(diffuse_atlas_offset);

	int32_t* index_buffer_ptr = index_buffer->data;
	float* depth_buffer_ptr = depth_buffer->data;


	for (int y = miny; y <= maxy; y += 1)
	{
		index_buffer_ptr = index_buffer->data + ((y - starty) * index_buffer->width + (minx - startx));
		depth_buffer_ptr = depth_buffer->data + ((y - starty) * index_buffer->width + (minx - startx));

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
	int width = index_buffer->width, height = index_buffer->height;
	int32_t* ib_data = index_buffer->data;
	uint16_t* atlas_data = (uint16_t*)atlas->data;
	uint16_t* out_row = ctx->output_start;
	int ib_i = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int index = ib_data[ib_i];
			if (index < 0 || index >= atlas->length) {
				out_row[x] = convert_8r8g8b8a_to_5r6g5b(255, 0, 255);
			} else {
				out_row[x] = atlas_data[index];
			}
			ib_i++;
		}
		out_row += ctx->width;
	}
}

/* =========================================================== */
/*  Primitive Pass                                             */
/* =========================================================== */

static int build_chunk_vertex_arrays(mesh_t* mesh, int start_index, int end_index, float* raw_vertices, int* indices) {
	int num_verts = (int)mesh->attrib.num_vertices;
	int bitmask_words = (num_verts + 63) / 64;
	uint64_t* bitmask    = (uint64_t*)calloc((size_t)bitmask_words, sizeof(uint64_t));
	int*      vid_to_local = (int*)malloc((size_t)num_verts * sizeof(int));
	int num_unique = 0;
	for (int i = start_index; i < end_index; i++) {
		int local_tri = i - start_index;
		for (int v = 0; v < 3; v++) {
			int vid = mesh->attrib.faces[i * 3 + v].v_idx;
			int word = vid >> 6;
			uint64_t bit = (uint64_t)1 << (vid & 63);
			if (bitmask[word] & bit) {
				indices[local_tri * 3 + v] = vid_to_local[vid];
			} else {
				bitmask[word] |= bit;
				vid_to_local[vid] = num_unique;
				raw_vertices[num_unique * 3 + 0] = mesh->attrib.vertices[3 * vid + 0];
				raw_vertices[num_unique * 3 + 1] = mesh->attrib.vertices[3 * vid + 1];
				raw_vertices[num_unique * 3 + 2] = mesh->attrib.vertices[3 * vid + 2];
				indices[local_tri * 3 + v] = num_unique++;
			}
		}
	}
	free(bitmask);
	free(vid_to_local);
	return num_unique;
}

static inline clip_triangle_t process_triangle(float* raw_vertices, int* indices, int local_tri_idx, mat4 mvp) {
	int base = local_tri_idx * 3;
	raw_vertex_t r0 = create_vertex(raw_vertices, indices, base + 0);
	raw_vertex_t r1 = create_vertex(raw_vertices, indices, base + 1);
	raw_vertex_t r2 = create_vertex(raw_vertices, indices, base + 2);
	clip_triangle_t triangle;
	triangle.v0 = process_vertex(r0, mvp);
	triangle.v1 = process_vertex(r1, mvp);
	triangle.v2 = process_vertex(r2, mvp);
	return triangle;
}

typedef struct {
	clip_triangle_t*      triangles;   /* [num_triangles], MVP-transformed positions only */
	int                   num_triangles;
	int                   start_index; /* global mesh triangle index of triangles[0] */
	float*                raw_vertices; /* deduplicated xyz positions for the active chunk */
	int*                  indices;      /* [num_triangles * 3] local index into raw_vertices per face vertex */
	render_params_t       params;      /* process_triangle reads params.model_view_projection */
	monotonic_timer_t     timer;
	double                total_primitive_time;
	/* chunk layout, for computing hint_mask chunk-membership bits */
	int                   num_chunks_y, num_chunks_x;
	int                   width, height;
	int*                  chunk_starty;  /* [num_chunks_y] */
	int*                  chunk_endy;    /* [num_chunks_y] */
	int*                  chunk_startx;  /* [num_chunks_x] */
	int*                  chunk_endx;    /* [num_chunks_x] */
	/* Hints for raster pass */
	int			 		  hint_bitarray_size;
	uint64_t*             requires_clipping_hint;
	uint64_t*			  chunk_hints;	/* [num_chunks_y * num_chunks_x * hint_bitarray_size], flat-indexed (y*num_chunks_x+x)*hint_bitarray_size */
	uint64_t* 			  chunk_hints_temp;
	uint64_t			  requires_clipping_hint_temp;
} primitive_pass_ctx_t;

static inline primitive_pass_ctx_t create_primitive_pass_ctx(mesh_t* mesh, int start_index, int end_index, int num_chunks_y, int num_chunks_x) {
	int num_triangles = end_index - start_index;
	primitive_pass_ctx_t ctx;
	ctx.num_triangles = num_triangles;
	ctx.start_index   = start_index;
	ctx.triangles    = (clip_triangle_t*)malloc((size_t)num_triangles * sizeof(clip_triangle_t));
	ctx.raw_vertices = (float*)malloc((size_t)num_triangles * 3 * 3 * sizeof(float));
	ctx.indices      = (int*)malloc((size_t)num_triangles * 3 * sizeof(int));
	ctx.total_primitive_time = 0.0;

	ctx.num_chunks_y = num_chunks_y;
	ctx.num_chunks_x = num_chunks_x;
	ctx.width = 0;
	ctx.height = 0;
	ctx.chunk_starty = (int*)malloc((size_t)num_chunks_y * sizeof(int));
	ctx.chunk_endy   = (int*)malloc((size_t)num_chunks_y * sizeof(int));
	ctx.chunk_startx = (int*)malloc((size_t)num_chunks_x * sizeof(int));
	ctx.chunk_endx   = (int*)malloc((size_t)num_chunks_x * sizeof(int));

	ctx.hint_bitarray_size 		= (num_triangles / 64 + 1);
	ctx.requires_clipping_hint 	= (uint64_t*)calloc((size_t)ctx.hint_bitarray_size, sizeof(uint64_t));
	ctx.chunk_hints 			= (uint64_t*)calloc((size_t)(ctx.hint_bitarray_size * num_chunks_y * num_chunks_x), sizeof(uint64_t));
	ctx.chunk_hints_temp 		= (uint64_t*)calloc((size_t)(num_chunks_y * num_chunks_x), sizeof(uint64_t));

	int num_unique = build_chunk_vertex_arrays(mesh, start_index, end_index, ctx.raw_vertices, ctx.indices);
	ctx.raw_vertices = (float*)realloc(ctx.raw_vertices, (size_t)num_unique * 3 * sizeof(float));

	return ctx;
}

static inline void free_primitive_pass_ctx(primitive_pass_ctx_t* ctx) {
	free(ctx->triangles);
	free(ctx->raw_vertices);
	free(ctx->indices);
	free(ctx->chunk_starty);
	free(ctx->chunk_endy);
	free(ctx->chunk_startx);
	free(ctx->chunk_endx);
	free(ctx->requires_clipping_hint);
	free(ctx->chunk_hints);
}

/* A triangle needs near-plane clipping when at least one (but not all) vertex is
 * outside the near plane. (Fully-outside triangles are caught by triangle_is_fully_clipped.) */
static inline int triangle_requires_clipping(clip_triangle_t tri) {
	return !(is_inside_near_plane_clip(tri.v0) && is_inside_near_plane_clip(tri.v1) && is_inside_near_plane_clip(tri.v2));
}

static inline void set_hint_mask(clip_triangle_t* triangle, primitive_pass_ctx_t* ctx, int idx) {
	int num_cells = ctx->num_chunks_y * ctx->num_chunks_x;
	if (triangle_is_fully_clipped(*triangle)) {
		// for (int c = 0; c < num_cells; c++) {
		// 	ctx->chunk_hints_temp[c] &= ~((uint64_t)1ULL << (idx % 64));
		// }
		return;
	}
	if (triangle_requires_clipping(*triangle)) {
		ctx->requires_clipping_hint_temp |= ((uint64_t)1ULL << (idx % 64));
		return;
	}
	float x0, y0, x1, y1, x2, y2;
	viewport_transform_clip(&triangle->v0, ctx->width, ctx->height, &x0, &y0);
	viewport_transform_clip(&triangle->v1, ctx->width, ctx->height, &x1, &y1);
	viewport_transform_clip(&triangle->v2, ctx->width, ctx->height, &x2, &y2);
	float xmin_f = fminf(x0, fminf(x1, x2));
	float xmax_f = fmaxf(x0, fmaxf(x1, x2));
	float ymin_f = fminf(y0, fminf(y1, y2));
	float ymax_f = fmaxf(y0, fmaxf(y1, y2));
	int xmin = (int)floorf(xmin_f);
	int xmax = (int)ceilf(xmax_f);
	int ymin = (int)floorf(ymin_f);
	int ymax = (int)ceilf(ymax_f);
	for (int y = 0; y < ctx->num_chunks_y; y++) {
		if (ymin >= ctx->chunk_endy[y] || ymax <= ctx->chunk_starty[y]) continue;
		for (int x = 0; x < ctx->num_chunks_x; x++) {
			if (xmin >= ctx->chunk_endx[x] || xmax <= ctx->chunk_startx[x]) continue;
			int c = y * ctx->num_chunks_x + x;
			ctx->chunk_hints_temp[c] |= ((uint64_t)1ULL << (idx % 64));
		}
	}
}

void primitive_pass(mesh_t* mesh, primitive_pass_ctx_t* ctx, int start_index, int end_index) {
	timer_start(&ctx->timer);
	ctx->start_index = start_index;
	int num_cells = ctx->num_chunks_y * ctx->num_chunks_x;
	size_t chunk_hints_temp_size = num_cells * sizeof(uint64_t);
	for (int j = start_index; j < end_index; j+=64) {
		ctx->requires_clipping_hint_temp = (uint64_t)0ULL;
		memset((void *)ctx->chunk_hints_temp, (uint64_t)0ULL, chunk_hints_temp_size);
		for(int i = j; i < j + 64 && i < end_index ;i++) {
			clip_triangle_t tri = process_triangle(ctx->raw_vertices, ctx->indices, i - start_index, ctx->params.model_view_projection);
			set_hint_mask(&tri, ctx, i - start_index);
			ctx->triangles[i - start_index] = tri;
		}
		for (int c = 0; c < num_cells; c++) {
			ctx->chunk_hints[c * ctx->hint_bitarray_size + (j - start_index)/64] = ctx->chunk_hints_temp[c];
		}
		ctx->requires_clipping_hint[(j - start_index)/64] = ctx->requires_clipping_hint_temp;
	}
	ctx->num_triangles = end_index - start_index;
	ctx->total_primitive_time += timer_elapsed_ms(&ctx->timer);
}

processed_triangle_t get_triangle(mesh_t* mesh, primitive_pass_ctx_t* ctx, int idx) {
	int gi = ctx->start_index + idx;
	clip_triangle_t* ct = &ctx->triangles[idx];

	tinyobj_vertex_index_t f0 = mesh->attrib.faces[gi * 3 + 0];
	tinyobj_vertex_index_t f1 = mesh->attrib.faces[gi * 3 + 1];
	tinyobj_vertex_index_t f2 = mesh->attrib.faces[gi * 3 + 2];

	processed_triangle_t tri;
	tri.v0 = (processed_vertex_t){ ct->v0.x, ct->v0.y, ct->v0.z, ct->v0.w, 0.f, 0.f };
	tri.v1 = (processed_vertex_t){ ct->v1.x, ct->v1.y, ct->v1.z, ct->v1.w, 0.f, 0.f };
	tri.v2 = (processed_vertex_t){ ct->v2.x, ct->v2.y, ct->v2.z, ct->v2.w, 0.f, 0.f };

	if (f0.vt_idx >= 0) {
		tri.v0.texcoord_u = mesh->attrib.texcoords[2 * f0.vt_idx + 0];
		tri.v0.texcoord_v = mesh->attrib.texcoords[2 * f0.vt_idx + 1];
	}
	if (f1.vt_idx >= 0) {
		tri.v1.texcoord_u = mesh->attrib.texcoords[2 * f1.vt_idx + 0];
		tri.v1.texcoord_v = mesh->attrib.texcoords[2 * f1.vt_idx + 1];
	}
	if (f2.vt_idx >= 0) {
		tri.v2.texcoord_u = mesh->attrib.texcoords[2 * f2.vt_idx + 0];
		tri.v2.texcoord_v = mesh->attrib.texcoords[2 * f2.vt_idx + 1];
	}

	int mat_idx = mesh->attrib.material_ids[gi];
	tri.tex_width = 0;
	tri.tex_height = 0;
	tri.diffuse_atlas_offset = 0;
	if (mat_idx != -1) {
		tri.tex_width            = mesh->materials[mat_idx].texture_width;
		tri.tex_height           = mesh->materials[mat_idx].texture_height;
		tri.diffuse_atlas_offset = mesh->materials[mat_idx].diffuse_atlas_offset;
	}
	return tri;
}

/* =========================================================== */
/*  Main Render Function                                       */
/* =========================================================== */

void clear_pass(rendering_ctx_t* ctx) {
	timer_start(&ctx->timer);
	clear_framebuffer_f(&ctx->depth_buffer, far_plane);
	clear_framebuffer_i32(&ctx->index_buffer, (int32_t)0);
	double clear_time = timer_elapsed_ms(&ctx->timer);
	ctx->total_clear_time += clear_time;
}

void render_mesh(mesh_t* mesh, rendering_ctx_t* ctx, primitive_pass_ctx_t* prim) {
	timer_start(&ctx->timer);
	uint64_t* chunk_hint_bitarray = prim->chunk_hints + ctx->chunk_index * prim->hint_bitarray_size;
	for (int i = 0; i < prim->num_triangles; i+=64) {
		uint64_t chunk_hint_i = chunk_hint_bitarray[i/64];
		uint64_t requires_clipping_hint_i = prim->requires_clipping_hint[i/64];
		if(chunk_hint_i == 0 && requires_clipping_hint_i == 0) continue;
		for (int j = i; j < (i + 64) && j < prim->num_triangles; j++) {
			
			if (requires_clipping_hint_i & ((uint64_t)1ULL << (j-i))) {
				processed_triangle_t triangle = get_triangle(mesh, prim, j);
				/* Needs near-plane clipping — processed in every chunk (rasteriser clamps to strip). */
				processed_triangle_t clipped_triangle0, clipped_triangle1;
				int num_clipped_triangles = 0;
				clip_triangle(triangle, &clipped_triangle0, &clipped_triangle1, &num_clipped_triangles);
				clipped_triangle0.diffuse_atlas_offset = triangle.diffuse_atlas_offset;
				clipped_triangle0.tex_width  = triangle.tex_width;
				clipped_triangle0.tex_height = triangle.tex_height;
				clipped_triangle1.diffuse_atlas_offset = triangle.diffuse_atlas_offset;
				clipped_triangle1.tex_width  = triangle.tex_width;
				clipped_triangle1.tex_height = triangle.tex_height;

				if (num_clipped_triangles == 0) continue;
				rasterize_triangle_avx2_texture_index_only(&clipped_triangle0, ctx);
				if (num_clipped_triangles == 1) continue;
				rasterize_triangle_avx2_texture_index_only(&clipped_triangle1, ctx);
			} else if (chunk_hint_i & ((uint64_t)1ULL << (j-i))) {
				processed_triangle_t triangle = get_triangle(mesh, prim, j);
				rasterize_triangle_avx2_texture_index_only(&triangle, ctx);
			}
		}
	}
	double rasterisation_time = timer_elapsed_ms(&ctx->timer);
	ctx->total_rasterisation_time += rasterisation_time;

	timer_start(&ctx->timer);
	texture_sample_pass_5r6g5b(ctx, &mesh->diffuse_atlas);
	double texture_sampling_time = timer_elapsed_ms(&ctx->timer);
	ctx->total_texture_sampling_time += texture_sampling_time;

}

#endif
