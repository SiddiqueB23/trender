#pragma once

#include <math.h>
#include <cglm/cglm.h>
#include "tio.h"
#include "rendering_avx2_u16_mt.h"

typedef struct {
	float scale_x, scale_y, scale_z;
	float translate_x, translate_y, translate_z;
	float rotate_angle;
	float camera_x, camera_y, camera_z;
	float camera_pitch, camera_yaw;
	render_params_t render_params;
	mat4 model_matrix, view_matrix, projection_matrix;
	tio_ctx_t* tio_ctx;
	int interactive;
	int rotate;
	int quit_requested;
} input_state_t;

#define INPUT_STATE_INITIALIZER { \
	.scale_x = 1.0f, .scale_y = 1.0f, .scale_z = 1.0f, \
	.translate_x = 0.0f, .translate_y = 0.0f, .translate_z = 0.0f, \
	.rotate_angle = 0.0f, \
	.camera_x = 0.0f, .camera_y = 0.0f, .camera_z = 0.0f, \
	.camera_pitch = 0.0f, .camera_yaw = 0.0f, \
	.tio_ctx = NULL, \
	.interactive = 0, \
	.rotate = 0, \
	.quit_requested = 0 \
}

static inline void update_matrices(input_state_t* s) {
	glm_mat4_mul(s->view_matrix, s->model_matrix, s->render_params.model_view);
	glm_mat4_mul(s->projection_matrix, s->render_params.model_view, s->render_params.model_view_projection);
	glm_mat4_pick3(s->render_params.model_view, s->render_params.normal_transform);
	glm_mat3_inv(s->render_params.normal_transform, s->render_params.normal_transform);
	glm_mat3_transpose(s->render_params.normal_transform);
}

static inline void update_model_matrix(input_state_t* s) {
	glm_mat4_identity(s->model_matrix);
	glm_scale(s->model_matrix, (vec3) { s->scale_x, s->scale_y, s->scale_z });
	glm_rotate_y(s->model_matrix, glm_rad(s->rotate_angle), s->model_matrix);
	glm_translate(s->model_matrix, (vec3) { s->translate_x, s->translate_y, s->translate_z });
}

static inline void update_view_matrix(input_state_t* s) {
	glm_mat4_identity(s->view_matrix);
	glm_rotate_x(s->view_matrix, glm_rad(s->camera_pitch), s->view_matrix);
	glm_rotate_y(s->view_matrix, glm_rad(s->camera_yaw), s->view_matrix);
	glm_translate(s->view_matrix, (vec3) { -s->camera_x, -s->camera_y, s->camera_z });
}

static inline void first_person_camera(int event_code, input_state_t* s) {
	float camera_facing_x = sinf(glm_rad(s->camera_yaw));
	float camera_facing_y = sinf(glm_rad(s->camera_pitch));
	float camera_facing_z = cosf(glm_rad(s->camera_yaw));
	float camera_right_x = sinf(glm_rad(s->camera_yaw - 90.0f));
	float camera_right_y = 0.0f;
	float camera_right_z = cosf(glm_rad(s->camera_yaw - 90.0f));

	float camera_speed = 1.0f;
	if (event_code == 'd') {
		s->camera_x -= camera_right_x * camera_speed;
		s->camera_y -= camera_right_y * camera_speed;
		s->camera_z -= camera_right_z * camera_speed;
	}
	else if (event_code == 'a') {
		s->camera_x += camera_right_x * camera_speed;
		s->camera_y += camera_right_y * camera_speed;
		s->camera_z += camera_right_z * camera_speed;
	}
	else if (event_code == 'w') {
		s->camera_x += camera_facing_x * camera_speed;
		s->camera_y += camera_facing_y * camera_speed;
		s->camera_z += camera_facing_z * camera_speed;
	}
	else if (event_code == 's') {
		s->camera_x -= camera_facing_x * camera_speed;
		s->camera_y -= camera_facing_y * camera_speed;
		s->camera_z -= camera_facing_z * camera_speed;
	}
	else if (event_code == 'q') {
		s->camera_y += camera_speed;
	}
	else if (event_code == 'e') {
		s->camera_y -= camera_speed;
	}
	else if (event_code == ARROW_LEFT) {
		s->camera_yaw -= 10.0f;
	}
	else if (event_code == ARROW_RIGHT) {
		s->camera_yaw += 10.0f;
	}
	else if (event_code == ARROW_UP) {
		s->camera_pitch -= 10.0f;
		glm_clamp(s->camera_pitch, -45.0f, 45.0f);
	}
	else if (event_code == ARROW_DOWN) {
		s->camera_pitch += 10.0f;
		glm_clamp(s->camera_pitch, -45.0f, 45.0f);
	}
	update_view_matrix(s);
}

static inline void init_input(input_state_t* s, tio_ctx_t* tio_ctx, int interactive, int rotate, int cols, int rows) {
	s->tio_ctx = tio_ctx;
	s->interactive = interactive;
	s->rotate = rotate;
	s->quit_requested = 0;

	glm_mat4_identity(s->projection_matrix);
	glm_perspective(glm_rad(90.0f), (float)cols / (float)rows, near_plane, far_plane, s->projection_matrix);

	update_model_matrix(s);
	update_view_matrix(s);
	update_matrices(s);
}

static inline void handle_input(input_state_t* s) {
	if (s->interactive) {
		int current_event_queue_bytes_size = tio_get_event_queue_byte_size(s->tio_ctx);
		int event_bytes_processed = 0;
		while (event_bytes_processed < current_event_queue_bytes_size) {
			tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
			int bytes_processed = tio_pop_event_queue(s->tio_ctx, &event);
			event_bytes_processed += bytes_processed;
			if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
				if (event.code == UPPERCASE_Q || event.code == CTRL_Q) {
					s->quit_requested = 1;
					break;
				}
				switch (event.code) {
				case 'w':
				case 'a':
				case 's':
				case 'd':
				case 'q':
				case 'e':
				case ARROW_UP:
				case ARROW_DOWN:
				case ARROW_RIGHT:
				case ARROW_LEFT:
					first_person_camera(event.code, s);
					break;
				}
			}
		}
	}
	if (s->rotate) {
		s->rotate_angle += 1.0f;
	}
	update_model_matrix(s);
	update_matrices(s);
}
