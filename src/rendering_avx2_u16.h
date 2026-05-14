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

const int scale = 16;

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
	int material_id;
	int tex_width;
	int tex_height;
} processed_triangle_t;

/* =========================================================== */
/*  Vertex Processing                                          */
/* =========================================================== */
raw_vertex_t create_vertex(tinyobj_attrib_t attrib, tinyobj_vertex_index_t idx) {
	raw_vertex_t input;
	input.position_x = attrib.vertices[3 * idx.v_idx + 0];
	input.position_y = attrib.vertices[3 * idx.v_idx + 1];
	input.position_z = attrib.vertices[3 * idx.v_idx + 2];
	if (idx.vt_idx > 0) {
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
static inline int is_inside_far_plane(processed_vertex_t v) {
	return v.homogenous_position_z <= v.homogenous_position_w;
}
static inline int is_inside_down_plane(processed_vertex_t v) {
	return v.homogenous_position_y >= -v.homogenous_position_w;
}
static inline int is_inside_up_plane(processed_vertex_t v) {
	return v.homogenous_position_y <= v.homogenous_position_w;
}
static inline int is_inside_left_plane(processed_vertex_t v) {
	return v.homogenous_position_x >= -v.homogenous_position_w;
}
static inline int is_inside_right_plane(processed_vertex_t v) {
	return v.homogenous_position_x <= v.homogenous_position_w;
}

static inline int triangle_is_fully_clipped(processed_triangle_t triangle) {
	int far_plane_check_v0 = is_inside_far_plane(triangle.v0);
	int far_plane_check_v1 = is_inside_far_plane(triangle.v1);
	int far_plane_check_v2 = is_inside_far_plane(triangle.v2);
	int near_plane_check_v0 = is_inside_near_plane(triangle.v0);
	int near_plane_check_v1 = is_inside_near_plane(triangle.v1);
	int near_plane_check_v2 = is_inside_near_plane(triangle.v2);
	int left_plane_check_v0 = is_inside_left_plane(triangle.v0);
	int left_plane_check_v1 = is_inside_left_plane(triangle.v1);
	int left_plane_check_v2 = is_inside_left_plane(triangle.v2);
	int right_plane_check_v0 = is_inside_right_plane(triangle.v0);
	int right_plane_check_v1 = is_inside_right_plane(triangle.v1);
	int right_plane_check_v2 = is_inside_right_plane(triangle.v2);
	int up_plane_check_v0 = is_inside_up_plane(triangle.v0);
	int up_plane_check_v1 = is_inside_up_plane(triangle.v1);
	int up_plane_check_v2 = is_inside_up_plane(triangle.v2);
	int down_plane_check_v0 = is_inside_down_plane(triangle.v0);
	int down_plane_check_v1 = is_inside_down_plane(triangle.v1);
	int down_plane_check_v2 = is_inside_down_plane(triangle.v2);
	if (!(far_plane_check_v0 == far_plane_check_v1 && far_plane_check_v1 == far_plane_check_v2))return 0;
	if (!(near_plane_check_v0 == near_plane_check_v1 && near_plane_check_v1 == near_plane_check_v2))return 0;
	if (!(left_plane_check_v0 == left_plane_check_v1 && left_plane_check_v1 == left_plane_check_v2))return 0;
	if (!(right_plane_check_v0 == right_plane_check_v1 && right_plane_check_v1 == right_plane_check_v2))return 0;
	if (!(up_plane_check_v0 == up_plane_check_v1 && up_plane_check_v1 == up_plane_check_v2))return 0;
	if (!(down_plane_check_v0 == down_plane_check_v1 && down_plane_check_v1 == down_plane_check_v2))return 0;
	int completely_outside_at_least_one_plane =
		far_plane_check_v0 == 0 ||
		near_plane_check_v0 == 0 ||
		left_plane_check_v0 == 0 ||
		right_plane_check_v0 == 0 ||
		up_plane_check_v0 == 0 ||
		down_plane_check_v0 == 0;
	return completely_outside_at_least_one_plane;
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
	if (triangle_is_fully_clipped(in)) {
		*num_out = 0;
		return;
	}
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
		int inside_index;
		if (is_inside_near_plane(in.v0)) {
			inside_vertex = in.v0;
			outside_vertex1 = in.v1;
			outside_vertex2 = in.v2;
			inside_index = 0;
		}
		else if (is_inside_near_plane(in.v1)) {
			inside_vertex = in.v1;
			outside_vertex1 = in.v2;
			outside_vertex2 = in.v0;
			inside_index = 1;
		}
		else {
			inside_vertex = in.v2;
			outside_vertex1 = in.v0;
			outside_vertex2 = in.v1;
			inside_index = 2;
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
		int outside_index;
		if (!is_inside_near_plane(in.v0)) {
			outside_vertex = in.v0;
			inside_vertex1 = in.v1;
			inside_vertex2 = in.v2;
			outside_index = 0;
		}
		else if (!is_inside_near_plane(in.v1)) {
			outside_vertex = in.v1;
			inside_vertex1 = in.v2;
			inside_vertex2 = in.v0;
			outside_index = 1;
		}
		else {
			outside_vertex = in.v2;
			inside_vertex1 = in.v0;
			inside_vertex2 = in.v1;
			outside_index = 2;
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

//static inline __m256i _mm256_rem_epi32(__m256i a, __m256i b) {
//	// Convert int32 → float (exact for values within float's 24-bit mantissa)
//	__m256 fa = _mm256_cvtepi32_ps(a);
//	__m256 fb = _mm256_cvtepi32_ps(b);
//
//	// Divide and truncate toward zero (matches C integer division semantics)
//	__m256 quotient = _mm256_div_ps(fa, fb);
//	__m256 truncated = _mm256_round_ps(quotient, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
//
//	// remainder = a - trunc(a/b) * b
//	__m256 prod = _mm256_mul_ps(truncated, fb);
//	__m256 rem_f = _mm256_sub_ps(fa, prod);
//
//	return _mm256_cvttps_epi32(rem_f);
//}

static inline __m256i modulo_int_avx2(__m256i x, int modulus) {
	__m256i mod = _mm256_set1_epi32(modulus);
	__m256i result = _mm256_rem_epi32(x, mod);
	__m256i mask = _mm256_cmpgt_epi32(_mm256_setzero_si256(), result);
	result = _mm256_add_epi32(result, _mm256_and_si256(mask, mod));
	return result;
}

static inline __m256 dot_product_3vec8f(__m256 a0, __m256 a1, __m256 a2, __m256 b0, __m256 b1, __m256 b2) {
	return _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(a0, b0), _mm256_mul_ps(a1, b1)), _mm256_mul_ps(a2, b2));
}

void rasterize_triangle_avx2_texture_index_only(framebuffer_i32* fb, framebuffer_f* depth_buffer, processed_triangle_t triangle) {

	int mat_idx = triangle.material_id;
	int tex_width = triangle.tex_width;
	int tex_height = triangle.tex_height;
	int is_textured = (tex_width != 0 && tex_height != 0);

	float v0_x, v0_y, v1_x, v1_y, v2_x, v2_y;
	viewport_transform(&triangle.v0, fb->width, fb->height, &v0_x, &v0_y);
	viewport_transform(&triangle.v1, fb->width, fb->height, &v1_x, &v1_y);
	viewport_transform(&triangle.v2, fb->width, fb->height, &v2_x, &v2_y);

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
	minx = clamp_int(minx, 0, fb->width - 1);
	maxx = clamp_int(maxx, 0, fb->width - 1);
	miny = clamp_int(miny, 0, fb->height - 1);
	maxy = clamp_int(maxy, 0, fb->height - 1);

	// Half-edge constants
	int C1 = DY12 * X1 - DX12 * Y1;
	int C2 = DY23 * X2 - DX23 * Y2;
	int C3 = DY31 * X3 - DX31 * Y3;

	// Correct for fill convention
	if (DY12 < 0 || (DY12 == 0 && DX12 > 0)) C1++;
	if (DY23 < 0 || (DY23 == 0 && DX23 > 0)) C2++;
	if (DY31 < 0 || (DY31 == 0 && DX31 > 0)) C3++;

	float inv_denom = 1.0f / ((float)C1 + (float)C2 + (float)C3);

	int CY1[8], CY2[8], CY3[8];
	for (int i = 0; i < 8; i++) {
		CY1[i] = C1 + FDX12 * (miny + 0) - FDY12 * (minx + i);
		CY2[i] = C2 + FDX23 * (miny + 0) - FDY23 * (minx + i);
		CY3[i] = C3 + FDX31 * (miny + 0) - FDY31 * (minx + i);
	}

	float* depth_buffer_ptr = depth_buffer->data;

	__m256i cy1_vec = _mm256_loadu_si256((__m256i*)CY1);
	__m256i cy2_vec = _mm256_loadu_si256((__m256i*)CY2);
	__m256i cy3_vec = _mm256_loadu_si256((__m256i*)CY3);

	const __m256i fdx12_vec = _mm256_set1_epi32(FDX12 * 1);
	const __m256i fdx23_vec = _mm256_set1_epi32(FDX23 * 1);
	const __m256i fdx31_vec = _mm256_set1_epi32(FDX31 * 1);

	const __m256i eight_fdy12_vec = _mm256_set1_epi32(FDY12 * 8);
	const __m256i eight_fdy23_vec = _mm256_set1_epi32(FDY23 * 8);
	const __m256i eight_fdy31_vec = _mm256_set1_epi32(FDY31 * 8);

	const __m256i zero_vec = _mm256_setzero_si256();

	const __m256 v0w_inv_vec = _mm256_set1_ps(1.0f / triangle.v0.homogenous_position_w);
	const __m256 v1w_inv_vec = _mm256_set1_ps(1.0f / triangle.v1.homogenous_position_w);
	const __m256 v2w_inv_vec = _mm256_set1_ps(1.0f / triangle.v2.homogenous_position_w);

	int area = C1 + C2 + C3;
	float inv_area = 1.0f / (float)area;
	const __m256 inv_area_vec = _mm256_set1_ps(inv_area);

	if (area < scale && area > -scale) return; // Degenerate triangle, skip

	int32_t* color_buffer = fb->data;

	const __m256 v0u_vec = _mm256_set1_ps(triangle.v0.texcoord_u);
	const __m256 v1u_vec = _mm256_set1_ps(triangle.v1.texcoord_u);
	const __m256 v2u_vec = _mm256_set1_ps(triangle.v2.texcoord_u);
	const __m256 v0v_vec = _mm256_set1_ps(triangle.v0.texcoord_v);
	const __m256 v1v_vec = _mm256_set1_ps(triangle.v1.texcoord_v);
	const __m256 v2v_vec = _mm256_set1_ps(triangle.v2.texcoord_v);

	const __m256i material_id_vec = _mm256_set1_epi32(mat_idx << 24);

	for (int y = miny; y < maxy; y += 1)
	{
		color_buffer = fb->data + (y * fb->width + minx);
		depth_buffer_ptr = depth_buffer->data + (y * depth_buffer->width + minx);

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
				color_buffer += 8;
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
				__m256i material_index_vec = material_id_vec;
				if (is_textured) {
					__m256 one_minus_interp_v = _mm256_sub_ps(_mm256_set1_ps(1.0f), interp_v);
					__m256i tex_x = _mm256_cvtps_epi32(_mm256_mul_ps(interp_u, _mm256_set1_ps((float)(tex_width - 1))));
					__m256i tex_y = _mm256_cvtps_epi32(_mm256_mul_ps(one_minus_interp_v, _mm256_set1_ps((float)(tex_height - 1))));
					__m256i tex_width_vec = _mm256_set1_epi32(tex_width);
					tex_x = modulo_int_avx2(tex_x, tex_width);
					tex_y = modulo_int_avx2(tex_y, tex_height);

					__m256i index_vec = _mm256_add_epi32(_mm256_mullo_epi32(tex_y, tex_width_vec), tex_x);
					//index_vec = _mm256_mullo_epi32(index_vec, _mm256_set1_epi32(4));
					material_index_vec = _mm256_add_epi32(material_index_vec, index_vec);
				}
				if (mask_int == (int)0xFFFFFFFF) {
					_mm256_store_si256((__m256i*)color_buffer, material_index_vec);
					_mm256_store_ps(depth_buffer_ptr, w_interp_vec);
				}
				else {
					_mm256_maskstore_epi32((int*)color_buffer, mask, material_index_vec);
					_mm256_maskstore_ps(depth_buffer_ptr, mask, w_interp_vec);
				}
			}

			color_buffer += 8;
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

//DECLSPEC_NOINLINE
//void texture_sample_pass_5r6g5b(framebuffer_i32* ib, framebuffer_u16* fb, material_t* materials) {
//	int width = ib->width;
//	int height = ib->height;
//	int length = width * height;
//	int32_t* ib_data = ib->data;
//	uint16_t* fb_data = fb->data;
//#define BLOCK_SIZE 64
//	for (int i = 0;i < length;i += BLOCK_SIZE) {
//		uint16_t pixels[BLOCK_SIZE];
//		for (int j = 0;j < BLOCK_SIZE;j++) {
//			int32_t material_index = *ib_data;
//			int material_id = material_index >> 24;
//			if (material_id != -1) {
//				int texture_index = material_index & 0xFFFFFF;
//				material_t* material = &materials[material_id];
//				uint16_t* tex_data = (uint16_t*)material->diffuse_texture.data;
//				if (material->diffuse_texture.data == NULL) {
//					unsigned char r = (unsigned char)clamp_int(lroundf(material->diffuse[0] * 255.0f), 0, 255);
//					unsigned char g = (unsigned char)clamp_int(lroundf(material->diffuse[1] * 255.0f), 0, 255);
//					unsigned char b = (unsigned char)clamp_int(lroundf(material->diffuse[2] * 255.0f), 0, 255);
//					pixels[j] = convert_8r8g8b8a_to_5r6g5b(r, g, b);
//				}
//				else {
//					//memcpy(&pixels[j], tex_data + texture_index, 2);
//					pixels[j] = tex_data[texture_index];
//				}
//			}
//			else {
//				pixels[j] = convert_8r8g8b8a_to_5r6g5b(255, 0, 255);
//			}
//			ib_data += 1;
//		}
//		memcpy(fb_data, pixels, BLOCK_SIZE * 2);
//		fb_data += BLOCK_SIZE;
//	}
//}
void texture_sample_pass_5r6g5b(framebuffer_i32* ib, framebuffer_u16* fb, material_t* materials, int num_materials) {
	int width = ib->width;
	int height = ib->height;
	int length = width * height;
	int32_t* ib_data = ib->data;
	uint16_t* fb_data = fb->data;
	for (int i = 0; i < length; i += 1) {
		int32_t material_index = *ib_data;
		int material_id = material_index >> 24;
		if (material_id != -1) {
			int texture_index = material_index & 0xFFFFFF;
			//int texture_index = 0;
			material_t* material = &materials[material_id];
			uint16_t* tex_data = (uint16_t*)material->diffuse_texture.data;
			if (material->diffuse_texture.data == NULL) {
				unsigned char r = (unsigned char)clamp_int(lroundf(material->diffuse[0] * 255.0f), 0, 255);
				unsigned char g = (unsigned char)clamp_int(lroundf(material->diffuse[1] * 255.0f), 0, 255);
				unsigned char b = (unsigned char)clamp_int(lroundf(material->diffuse[2] * 255.0f), 0, 255);
				*fb_data = convert_8r8g8b8a_to_5r6g5b(r, g, b);
			}
			else {
				*fb_data = tex_data[texture_index];
			}
		}
		else {
			*fb_data = convert_8r8g8b8a_to_5r6g5b(255, 0, 255);
		}
		ib_data += 1;
		fb_data += 1;
	}
	//for (int divider = 1; divider < 16; divider *= 2) {
	//	printf("Divider = %d\r\n", divider);
	//	for (int i = 0; i < 2; i++) {
	//		for (int j = 0; j < 2; j++) {
	//			int material_index = ib->data[i * width + j];
	//			int material_id = material_index >> 24;
	//			int texture_index = material_index & 0xFFFFFF;
	//			int texture_x = texture_index % materials[material_id].diffuse_texture.width;
	//			int texture_y = texture_index / materials[material_id].diffuse_texture.width;
	//			printf("(%d, %d, %d) ",
	//				material_id,
	//				texture_x / divider, texture_y / divider
	//			);
	//		}
	//		printf("\r\n");
	//	}
	//}
}

//void texture_sample_pass_5r6g5b(framebuffer_i32* ib, framebuffer_u16* fb, material_t* materials, int num_materials) {
//	int width = ib->width;
//	int height = ib->height;
//	int length = width * height;
//	for (int j = 0;j < num_materials;j++) {
//		material_t* material = &materials[j];
//		uint16_t* tex_data = (uint16_t*)material->diffuse_texture.data;
//		uint16_t* fb_data = fb->data;
//		int32_t* ib_data = ib->data;
//		for (int i = 0;i < length;i += 1) {
//			int32_t material_index = *ib_data;
//			int material_id = material_index >> 24;
//			if (material_id == j) {
//				if (tex_data == NULL) {
//					unsigned char r = (unsigned char)clamp_int(lroundf(material->diffuse[0] * 255.0f), 0, 255);
//					unsigned char g = (unsigned char)clamp_int(lroundf(material->diffuse[1] * 255.0f), 0, 255);
//					unsigned char b = (unsigned char)clamp_int(lroundf(material->diffuse[2] * 255.0f), 0, 255);
//					*fb_data = convert_8r8g8b8a_to_5r6g5b(r, g, b);
//				}
//				else {
//					int texture_index = material_index & 0xFFFFFF;
//					*fb_data = tex_data[texture_index];
//					printf("Mat %d, idx %d = ( %d, %d) at image idx %d\r\n",
//						material_id, texture_index,
//						texture_index / material->diffuse_texture.width,
//						texture_index % material->diffuse_texture.width,
//						i);
//				}
//			}
//			fb_data++;
//			ib_data++;
//		}
//	}
//}

/* =========================================================== */
/*  Main Render Function                                       */
/* =========================================================== */

double total_texture_sample_time = 0.0;
monotonic_timer_t texture_sample_timer;
vec4* ndc_vertices = NULL;
int ndc_vertices_len = 0;
processed_triangle_t* processed_triangles = NULL;
int processed_triangles_len = 0;
int processed_triangles_count = 0;

void render_mesh(mesh_t* mesh, mat4 model_view_projection, mat3 normal_transfrom, mat4 model_view,
	framebuffer_u16* fb, framebuffer_f* depth_buffer, framebuffer_i32* index_buffer) {

	int num_vertices = mesh->attrib.num_vertices;
	int num_required_vertices = num_vertices;
	if (ndc_vertices_len < num_vertices) {
		ndc_vertices = (vec4*)realloc(ndc_vertices, sizeof(vec4) * num_vertices);
		ndc_vertices_len = num_vertices;
	}
	int num_triangles = mesh->attrib.num_face_num_verts;
	if (processed_triangles_len < num_triangles) {
		processed_triangles = (processed_triangle_t*)realloc(processed_triangles, sizeof(processed_triangle_t) * num_triangles * 2);
		processed_triangles_len = num_triangles;
	}

	for (int i = 0; i < num_vertices; i++) {
		float vertex_x = mesh->attrib.vertices[3 * i + 0];
		float vertex_y = mesh->attrib.vertices[3 * i + 1];
		float vertex_z = mesh->attrib.vertices[3 * i + 2];
		vec4 position = { vertex_x, vertex_y, vertex_z, 1.0f };
		vec4 homogenous_position;
		glm_mat4_mulv(model_view_projection, position, homogenous_position);
		glm_vec4_copy(homogenous_position, &ndc_vertices[i][0]);
	}

	int processed_triangles_count = 0;
	for (int i = (int)mesh->start_triangle_index; i < (int)mesh->end_triangle_index; i++) {
		processed_vertex_t vert0 = process_vertex_2(ndc_vertices, mesh->attrib, mesh->attrib.faces[i * 3 + 0]);
		processed_vertex_t vert1 = process_vertex_2(ndc_vertices, mesh->attrib, mesh->attrib.faces[i * 3 + 1]);
		processed_vertex_t vert2 = process_vertex_2(ndc_vertices, mesh->attrib, mesh->attrib.faces[i * 3 + 2]);

		processed_triangle_t triangle = { vert0, vert1, vert2 };
		processed_triangle_t clipped_triangle0, clipped_triangle1;
		int num_clipped_triangles = 0;
		int mat_idx = mesh->attrib.material_ids[i];
		int is_textured = 0;
		int tex_width = 0;
		int tex_height = 0;
		if (mat_idx != -1) {
			if (mesh->materials[mat_idx].diffuse_texture.data != NULL) {
				is_textured = 1;
				tex_width = mesh->materials[mat_idx].diffuse_texture.width;
				tex_height = mesh->materials[mat_idx].diffuse_texture.height;
			}
		}
		clip_triangle(triangle, &clipped_triangle0, &clipped_triangle1, &num_clipped_triangles);
		clipped_triangle0.material_id = mat_idx;
		clipped_triangle0.tex_width = tex_width;
		clipped_triangle0.tex_height = tex_height;
		clipped_triangle1.material_id = mat_idx;
		clipped_triangle1.tex_width = tex_width;
		clipped_triangle1.tex_height = tex_height;

		if (num_clipped_triangles == 0) continue;
		if (fabsf(get_processed_triangle_area(clipped_triangle0, fb->width, fb->height)) > 1.0f) {
			processed_triangles[processed_triangles_count++] = clipped_triangle0;
		}
		if (num_clipped_triangles == 1) continue;
		if (fabsf(get_processed_triangle_area(clipped_triangle1, fb->width, fb->height)) > 1.0f) {
			processed_triangles[processed_triangles_count++] = clipped_triangle1;
		}
	}

	for (int i = 0; i < processed_triangles_count; i++) {
		processed_triangle_t triangle = processed_triangles[i];
		rasterize_triangle_avx2_texture_index_only(index_buffer, depth_buffer, triangle);
	}

	timer_start(&texture_sample_timer);
	texture_sample_pass_5r6g5b(index_buffer, fb, mesh->materials, mesh->num_materials);
	total_texture_sample_time += timer_elapsed_ms(&texture_sample_timer);

}

#endif