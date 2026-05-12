#ifndef RAYCAST_H
#define RAYCAST_H

#include "cglm/cglm.h"
#include "mesh_loading.h"

void ray_triangle_intersect(vec3 v0, vec3 v1, vec3 v2, vec3 ray_origin, vec3 ray_direction, float* out_t) {
	vec3 v0v1;
	vec3 v0v2;
	glm_vec3_sub(v1, v0, v0v1);
	glm_vec3_sub(v2, v0, v0v2);
	vec3 pvec;
	glm_vec3_cross(ray_direction, v0v2, pvec);
	float det = glm_vec3_dot(v0v1, pvec);
	if (fabsf(det) < 1e-6f) return;
	float inv_det = 1.0f / det;
	vec3 tvec;
	glm_vec3_sub(ray_origin, v0, tvec);
	float u = glm_vec3_dot(tvec, pvec) * inv_det;
	if (u < 0 || u > 1) return;
	vec3 qvec;
	glm_vec3_cross(tvec, v0v1, qvec);
	float v = glm_vec3_dot(ray_direction, qvec) * inv_det;
	if (v < 0 || u + v > 1) return;
	float t = glm_vec3_dot(v0v2, qvec) * inv_det;
	*out_t = t;
}

int ray_cast(mesh_t* mesh, mat4 model_view, int screenx, int screeny, int screen_width, int screen_height) {

	float ndc_x = (2.0f * ((float)screenx + 0.5f)) / (float)screen_width - 1.0f;
	float ndc_y = 1.0f - (2.0f * ((float)screeny + 0.5f)) / (float)screen_height;
	float fov = glm_rad(90.0f);
	float aspect = (float)screen_width / (float)screen_height;
	float dir_x = ndc_x * tanf(fov / 2.0f) * aspect;
	float dir_y = ndc_y * tanf(fov / 2.0f);
	float dir_z = -1.0f;

	int idx_hit = -1;
	float closest_t = INFINITY;
	for (int i = (int)mesh->start_triangle_index; i < (int)mesh->end_triangle_index; i++) {
		//for (int i = 13852; i < 13853; i++) {
		//for (int i = 65094; i < 65095; i++) {
		tinyobj_attrib_t attrib = mesh->attrib;
		tinyobj_vertex_index_t v0_idx = attrib.faces[i * 3];
		tinyobj_vertex_index_t v1_idx = attrib.faces[i * 3 + 1];
		tinyobj_vertex_index_t v2_idx = attrib.faces[i * 3 + 2];

		vec4 v0 = { attrib.vertices[3 * v0_idx.v_idx + 0], attrib.vertices[3 * v0_idx.v_idx + 1], attrib.vertices[3 * v0_idx.v_idx + 2], 1.0f };
		vec4 v1 = { attrib.vertices[3 * v1_idx.v_idx + 0], attrib.vertices[3 * v1_idx.v_idx + 1], attrib.vertices[3 * v1_idx.v_idx + 2], 1.0f };
		vec4 v2 = { attrib.vertices[3 * v2_idx.v_idx + 0], attrib.vertices[3 * v2_idx.v_idx + 1], attrib.vertices[3 * v2_idx.v_idx + 2], 1.0f };

		vec4 mv0, mv1, mv2;
		glm_mat4_mulv(model_view, v0, mv0);
		glm_mat4_mulv(model_view, v1, mv1);
		glm_mat4_mulv(model_view, v2, mv2);
		glm_vec4_scale(mv0, 1.0f / mv0[3], mv0);
		glm_vec4_scale(mv1, 1.0f / mv1[3], mv1);
		glm_vec4_scale(mv2, 1.0f / mv2[3], mv2);
		float t = -1.0f;
		ray_triangle_intersect(mv0, mv1, mv2, (vec3) { 0.0f, 0.0f, 0.0f }, (vec3) { dir_x, dir_y, dir_z }, & t);
		if (t > 0 && t < closest_t) {
			closest_t = t;
			idx_hit = i;
		}
	}
	return idx_hit;
}

#endif // RAYCAST_H