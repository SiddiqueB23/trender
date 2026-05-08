#ifndef RENDERING_H
#define RENDERING_H

#include "cglm/cglm.h"
#include "framebuffer_4i8.h"
#include "framebuffer_f.h"
#include "mesh_loading.h"
#include "utils.h"

typedef struct {
	float position_x, position_y, position_z;
	float texcoord_u, texcoord_v;
	//float color_r, color_g, color_b;
	float normal_x, normal_y, normal_z;
} raw_vertex_t;

typedef struct {
	float homogenous_position_x, homogenous_position_y, homogenous_position_z, homogenous_position_w;
	float fragment_position_x, fragment_position_y, fragment_position_z;
	float texcoord_u, texcoord_v;
	float normal_x, normal_y, normal_z;
	//float color_r, color_g, color_b;
	//float filler;
} processed_vertex_t;

typedef struct {
	processed_vertex_t v0, v1, v2;
} processed_triangle_t;

void process_fragment(processed_vertex_t interp, material_t material, unsigned char* r, unsigned char* g, unsigned char* b);
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
	if (idx.vn_idx > 0) {
		input.normal_x = attrib.normals[3 * idx.vn_idx + 0];
		input.normal_y = attrib.normals[3 * idx.vn_idx + 1];
		input.normal_z = attrib.normals[3 * idx.vn_idx + 2];
	}
	return input;
}

processed_vertex_t process_vertex(raw_vertex_t input, mat4 model_view_projection, mat3 normal_transform, mat4 model_view) {
	vec4 position = { input.position_x, input.position_y, input.position_z, 1.0f };
	vec3 normal = { input.normal_x, input.normal_y, input.normal_z };
	vec4 homogenous_position;
	vec4 fragment_position;
	glm_mat4_mulv(model_view, position, fragment_position);
	glm_vec4_scale(fragment_position, 1.0f / fragment_position[3], fragment_position);
	glm_mat4_mulv(model_view_projection, position, homogenous_position);
	glm_mat3_mulv(normal_transform, normal, normal);
	processed_vertex_t processed_vertex = {
		.fragment_position_x = fragment_position[0],
		.fragment_position_y = fragment_position[1],
		.fragment_position_z = fragment_position[2],
		.homogenous_position_x = homogenous_position[0],
		.homogenous_position_y = homogenous_position[1],
		.homogenous_position_z = homogenous_position[2],
		.homogenous_position_w = homogenous_position[3],
		.texcoord_u = input.texcoord_u,
		.texcoord_v = input.texcoord_v,
		.normal_x = normal[0],
		.normal_y = normal[1],
		.normal_z = normal[2],
		//.color_r = input.color_r,
		//.color_g = input.color_g,
		//.color_b = input.color_b,
	};
	return processed_vertex;
}

/* =========================================================== */
/*  Triangle Clipping                                          */
/* =========================================================== */
static inline int is_inside_near_plane(processed_vertex_t v) {
	return v.homogenous_position_z >= -v.homogenous_position_w;
}

static inline float get_lerp_parameter(processed_vertex_t inside, processed_vertex_t outside) {
	float d_in = inside.homogenous_position_z + inside.homogenous_position_w;
	float d_out = outside.homogenous_position_z + outside.homogenous_position_w;
	float denom = d_out - d_in;
	if (fabsf(denom) < 1e-6f * fabsf(d_in))
		return 0.0f; // degenerate: keep inside vertex as-is
	return glm_clamp(-d_in / denom, 0.0f, 1.0f);
}

processed_vertex_t lerp_processed_vertex_pair(processed_vertex_t v0, processed_vertex_t v1, float t) {
	processed_vertex_t out;
	out.homogenous_position_x = v0.homogenous_position_x + t * (v1.homogenous_position_x - v0.homogenous_position_x);
	out.homogenous_position_y = v0.homogenous_position_y + t * (v1.homogenous_position_y - v0.homogenous_position_y);
	out.homogenous_position_z = v0.homogenous_position_z + t * (v1.homogenous_position_z - v0.homogenous_position_z);
	out.homogenous_position_w = v0.homogenous_position_w + t * (v1.homogenous_position_w - v0.homogenous_position_w);
	//out.color_r = v0.color_r + t * (v1.color_r - v0.color_r);
	//out.color_g = v0.color_g + t * (v1.color_g - v0.color_g);
	//out.color_b = v0.color_b + t * (v1.color_b - v0.color_b);
	out.texcoord_u = v0.texcoord_u + t * (v1.texcoord_u - v0.texcoord_u);
	out.texcoord_v = v0.texcoord_v + t * (v1.texcoord_v - v0.texcoord_v);
	out.fragment_position_x = v0.fragment_position_x + t * (v1.fragment_position_x - v0.fragment_position_x);
	out.fragment_position_y = v0.fragment_position_y + t * (v1.fragment_position_y - v0.fragment_position_y);
	out.fragment_position_z = v0.fragment_position_z + t * (v1.fragment_position_z - v0.fragment_position_z);
	float normal_x = v0.normal_x + t * (v1.normal_x - v0.normal_x);
	float normal_y = v0.normal_y + t * (v1.normal_y - v0.normal_y);
	float normal_z = v0.normal_z + t * (v1.normal_z - v0.normal_z);
	vec3 normal = { normal_x, normal_y, normal_z };
	glm_vec3_normalize(normal);
	out.normal_x = normal[0];
	out.normal_y = normal[1];
	out.normal_z = normal[2];
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
			outside_vertex1 = in.v0;
			outside_vertex2 = in.v2;
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
			inside_vertex1 = in.v0;
			inside_vertex2 = in.v2;
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
		out1->v1 = inside_vertex2;
		out1->v2 = new_vertex1;
		out2->v0 = inside_vertex2;
		out2->v1 = new_vertex2;
		out2->v2 = new_vertex1;
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

void lerp_projected_depth(processed_triangle_t triangle, float alpha, float beta, float gamma, processed_vertex_t* out) {
	out->homogenous_position_w = 1.0f / (alpha / triangle.v0.homogenous_position_w + beta / triangle.v1.homogenous_position_w + gamma / triangle.v2.homogenous_position_w);
}

void lerp_projected_attrs(processed_triangle_t triangle, float alpha, float beta, float gamma, processed_vertex_t* out) {
	float* out_buf = (float*)out;
	float* v0_buf = (float*)(&(triangle.v0));
	float* v1_buf = (float*)(&(triangle.v1));
	float* v2_buf = (float*)(&(triangle.v2));
	float coeff0 = alpha / triangle.v0.homogenous_position_w;
	float coeff1 = beta / triangle.v1.homogenous_position_w;
	float coeff2 = gamma / triangle.v2.homogenous_position_w;
	float coeff3 = out->homogenous_position_w;
	for (int i = 4;i < 12;i++) {
		out_buf[i] = coeff3 * (coeff0 * v0_buf[i] + coeff1 * v1_buf[i] + coeff2 * v2_buf[i]);
	}
	vec3 normal = { out->normal_x, out->normal_y, out->normal_z };
	glm_vec3_normalize(normal);
	out->normal_x = normal[0];
	out->normal_y = normal[1];
	out->normal_z = normal[2];
}

void rasterize_triangle(framebuffer_4i8* fb, framebuffer_f* depth_buffer, processed_triangle_t triangle, material_t material) {
	float v0_x, v0_y, v1_x, v1_y, v2_x, v2_y;
	viewport_transform(&triangle.v0, fb->width, fb->height, &v0_x, &v0_y);
	viewport_transform(&triangle.v1, fb->width, fb->height, &v1_x, &v1_y);
	viewport_transform(&triangle.v2, fb->width, fb->height, &v2_x, &v2_y);

	float area = (v1_x - v0_x) * (v2_y - v0_y) - (v2_x - v0_x) * (v1_y - v0_y);
	if (fabsf(area) < 0.1f) return;

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

			processed_vertex_t interp;
			lerp_projected_depth(triangle, alpha, beta, gamma, &interp);
			float current_depth = interp.homogenous_position_w;

			get_pixel_f(depth_buffer, x, y, &current_depth);
			if (interp.homogenous_position_w > current_depth) continue;
			set_pixel_f(depth_buffer, x, y, interp.homogenous_position_w);

			lerp_projected_attrs(triangle, alpha, beta, gamma, &interp);

			unsigned char r = 255, g = 0, b = 0;
			//if (material.diffuse_texture.data != NULL)
			process_fragment(interp, material, &r, &g, &b);
			set_pixel_4i8(fb, x, y, r, g, b, 255);


		}
	}
}

/* =========================================================== */
/*  Fragment Processing                                        */
/* =========================================================== */

vec3 light_pos = { 5.0f, 5.0f, 5.0f };
vec3 light_color = { 1.0, 1.0, 1.0 };
float light_power = 40.0;
vec3 ambient_color = { 0.0, 0.0, 0.0 };
float shininess = 160.0;
float screen_gamma = 2.2;

float gamma_lut[256];
void init_gamma_lut() {
	for (int i = 0; i < 256; ++i) {
		gamma_lut[i] = powf(i / 255.0f, screen_gamma);
	}
}

void sample_texture_nearest(unsigned char* texture, int tex_width, int tex_height, float u, float v, unsigned char* r, unsigned char* g, unsigned char* b) {
	int x = (int)lroundf(u * (float)(tex_width - 1));
	int y = (int)lroundf((1.0f - v) * (float)(tex_height - 1));
	//x = clamp_int(x, 0, tex_width - 1);
	//y = clamp_int(y, 0, tex_height - 1);
	x = modulo_int(x, tex_width);
	y = modulo_int(y, tex_height);
	int index = (y * tex_width + x) * 4;
	*r = texture[index + 0];
	*g = texture[index + 1];
	*b = texture[index + 2];
}

void blinn_phong(processed_vertex_t interp, vec3 diffuse_color, vec3 frag_color) {
	vec3 spec_color = { 1.0, 1.0, 1.0 };
	vec3 normal = { interp.normal_x, interp.normal_y, interp.normal_z };
	vec3 vert_pos = { interp.fragment_position_x, interp.fragment_position_y, interp.fragment_position_z };
	vec3 light_dir;
	glm_vec3_sub(light_pos, vert_pos, light_dir);
	float distance = glm_vec3_dot(light_dir, light_dir);
	glm_normalize(light_dir);
	float lambertian = glm_max(glm_vec3_dot(light_dir, normal), 0.0);
	float specular = 0.0;

	if (lambertian > 0.0) {

		vec3 view_dir;
		glm_vec3_scale(vert_pos, -1.0f, view_dir);
		glm_normalize(view_dir);

		// blinn phong
		vec3 half_dir;
		glm_vec3_add(light_dir, view_dir, half_dir);
		glm_normalize(half_dir);
		float spec_angle = glm_max(glm_vec3_dot(half_dir, normal), 0.0);
		specular = powf(spec_angle, shininess);
	}
	glm_vec3_scale(diffuse_color, lambertian * light_power / distance, diffuse_color);
	glm_vec3_mul(diffuse_color, light_color, diffuse_color);
	glm_vec3_scale(spec_color, specular * light_power / distance, spec_color);
	glm_vec3_mul(spec_color, light_color, spec_color);
	vec3 color_linear;
	glm_vec3_copy(ambient_color, color_linear);
	glm_vec3_add(diffuse_color, color_linear, color_linear);
	glm_vec3_add(spec_color, color_linear, color_linear);

	frag_color[0] = powf(color_linear[0], 1.0 / screen_gamma);
	frag_color[1] = powf(color_linear[1], 1.0 / screen_gamma);
	frag_color[2] = powf(color_linear[2], 1.0 / screen_gamma);
}

void process_fragment(processed_vertex_t interp, material_t material, unsigned char* r, unsigned char* g, unsigned char* b) {
	vec3 diffuse_color = { material.diffuse[0], material.diffuse[1], material.diffuse[2]};

	//vec3 frag_color;
	//if (texture != NULL) {
	//	int texture_r, texture_g, texture_b;
	//	sample_texture_nearest(texture, tex_width, tex_height, interp.texcoord_u, interp.texcoord_v, &texture_r, &texture_g, &texture_b);
	//	diffuse_color[0] = gamma_lut[texture_r];
	//	diffuse_color[1] = gamma_lut[texture_g];
	//	diffuse_color[2] = gamma_lut[texture_b];
	//}
	//blinn_phong(interp, diffuse_color, frag_color);
	//*r = (unsigned char)clamp_int(lroundf(frag_color[0] * 255.0f), 0, 255);
	//*g = (unsigned char)clamp_int(lroundf(frag_color[1] * 255.0f), 0, 255);
	//*b = (unsigned char)clamp_int(lroundf(frag_color[2] * 255.0f), 0, 255);

	if (material.diffuse_texture.data != NULL) {
		sample_texture_nearest(material.diffuse_texture.data, material.diffuse_texture.width, material.diffuse_texture.height, interp.texcoord_u, interp.texcoord_v, r, g, b);
	}
	else {
		*r = (unsigned char)clamp_int(lroundf(diffuse_color[0] * 255.0f), 0, 255);
		*g = (unsigned char)clamp_int(lroundf(diffuse_color[1] * 255.0f), 0, 255);
		*b = (unsigned char)clamp_int(lroundf(diffuse_color[2] * 255.0f), 0, 255);
	}
}

void render_mesh(mesh_t* mesh, mat4 model_view_projection, mat3 normal_transfrom, mat4 model_view,
	framebuffer_4i8* fb, framebuffer_f* depth_buffer) {

	for (int i = 0; i < (int)mesh->attrib.num_face_num_verts; i++) {
		//for (int i = 13852; i < 13853; i++) {
		raw_vertex_t vert_input0 = create_vertex(mesh->attrib, mesh->attrib.faces[i * 3 + 0]);
		raw_vertex_t vert_input1 = create_vertex(mesh->attrib, mesh->attrib.faces[i * 3 + 1]);
		raw_vertex_t vert_input2 = create_vertex(mesh->attrib, mesh->attrib.faces[i * 3 + 2]);

		processed_vertex_t vert0 = process_vertex(vert_input0, model_view_projection, normal_transfrom, model_view);
		processed_vertex_t vert1 = process_vertex(vert_input1, model_view_projection, normal_transfrom, model_view);
		processed_vertex_t vert2 = process_vertex(vert_input2, model_view_projection, normal_transfrom, model_view);

		processed_triangle_t triangle = { vert0, vert1, vert2 };
		processed_triangle_t clipped_triangle0, clipped_triangle1;
		int num_clipped_triangles = 0;
		clip_triangle(triangle, &clipped_triangle0, &clipped_triangle1, &num_clipped_triangles);

		int mat_idx = mesh->attrib.material_ids[i];
		
		if (num_clipped_triangles > 0) rasterize_triangle(fb, depth_buffer, clipped_triangle0, mesh->materials[mat_idx]);
		if (num_clipped_triangles > 1) rasterize_triangle(fb, depth_buffer, clipped_triangle1, mesh->materials[mat_idx]);
	}
}
#endif