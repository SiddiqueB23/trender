#include "framebuffer_4i8.h"
#include "framebuffer_f.h"
#include "mesh_loading.h"
#include "raycast.h"
//#include "rendering.h"
#include "rendering_avx2.h"
#include "sixel_display.h"
#include "timer.h"
#include "tio.h"
#include <math.h>
#include <stdlib.h>

//#define RESOURCES_PATH "../resources/"

void update_matrices(mat4 model_matrix, mat4 view_matrix, mat4 projection_matrix,
	mat4 model_view, mat4 model_view_projection, mat3 normal_matrix) {
	glm_mat4_mul(view_matrix, model_matrix, model_view);
	glm_mat4_mul(projection_matrix, model_view, model_view_projection);
	glm_mat4_pick3(model_view, normal_matrix);
	glm_mat3_inv(normal_matrix, normal_matrix);
	glm_mat3_transpose(normal_matrix);
}

float scale_x = 1.0f, scale_y = 1.0f, scale_z = 1.0f;
float translate_x = 0.0f, translate_y = -1.0f, translate_z = 0.0f;
float rotate_angle = 0.0;
void update_model_matrix(mat4 model_matrix) {
	glm_mat4_identity(model_matrix);
	glm_scale(model_matrix, (vec3) { scale_x, scale_y, scale_z });
	glm_rotate_y(model_matrix, glm_rad(rotate_angle), model_matrix);
	glm_translate(model_matrix, (vec3) { translate_x, translate_y, translate_z });
}

float camera_x = 0.0f, camera_y = 2.0f, camera_z = -1.49999988f;
float camera_pitch = 0.0f, camera_yaw = 0.0f;

void update_view_matrix(mat4 view_matrix) {
	glm_mat4_identity(view_matrix);
	glm_rotate_x(view_matrix, glm_rad(camera_pitch), view_matrix);
	glm_rotate_y(view_matrix, glm_rad(camera_yaw), view_matrix);
	glm_translate(view_matrix, (vec3) { -camera_x, -camera_y, camera_z });
}

void first_person_camera(int event_code,
	mat4 model_matrix, mat4 view_matrix, mat4 projection_matrix,
	mat4 model_view, mat4 model_view_projection, mat3 normal_matrix) {
	float camera_facing_x = sinf(glm_rad(camera_yaw));
	float camera_facing_y = sinf(glm_rad(camera_pitch));
	float camera_facing_z = cosf(glm_rad(camera_yaw));
	float camera_right_x = sinf(glm_rad(camera_yaw - 90.0f));
	float camera_right_y = 0.0f;
	float camera_right_z = cosf(glm_rad(camera_yaw - 90.0f));

	if (event_code == 'd') {
		camera_x -= camera_right_x * 0.1f;
		camera_y -= camera_right_y * 0.1f;
		camera_z -= camera_right_z * 0.1f;
	}
	else if (event_code == 'a') {
		camera_x += camera_right_x * 0.1f;
		camera_y += camera_right_y * 0.1f;
		camera_z += camera_right_z * 0.1f;
	}
	else if (event_code == 'w') {
		camera_x += camera_facing_x * 0.1f;
		camera_y += camera_facing_y * 0.1f;
		camera_z += camera_facing_z * 0.1f;
	}
	else if (event_code == 's') {
		camera_x -= camera_facing_x * 0.1f;
		camera_y -= camera_facing_y * 0.1f;
		camera_z -= camera_facing_z * 0.1f;
	}
	else if (event_code == 'q') {
		camera_y += 0.1f;
	}
	else if (event_code == 'e') {
		camera_y -= 0.1f;
	}
	else if (event_code == ARROW_LEFT) {
		camera_yaw -= 10.0f;
	}
	else if (event_code == ARROW_RIGHT) {
		camera_yaw += 10.0f;
	}
	else if (event_code == ARROW_UP) {
		camera_pitch -= 10.0f;
		glm_clamp(camera_pitch, -45.0f, 45.0f);
	}
	else if (event_code == ARROW_DOWN) {
		camera_pitch += 10.0f;
		glm_clamp(camera_pitch, -45.0f, 45.0f);
	}
	update_view_matrix(view_matrix);
	update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
}

float near_plane = 1.0f;
float far_plane = 100.0f;

tio_ctx_t ctx;

void cleanup(void) {
	tio_destroy(&ctx);
}

int mousex = 0, mousey = 0;

void draw_square(framebuffer_4i8* fb, int x, int y, int width, int screen_width, int screen_height) {
	for (int i = y;i < y + width && i < screen_height;i++) {
		for (int j = x;j < x + width && j < screen_width;j++) {
			set_pixel_4i8(fb, j, i, 255, 0, 0, 255);
		}
	}
}

int compare_uint64_t(const void* a, const void* b) {
	uint64_t val_a = *(const uint64_t*)a;
	uint64_t val_b = *(const uint64_t*)b;
	if (val_a < val_b) return -1;
	else if (val_a > val_b) return 1;
	else return 0;
}

int main() {

	tio_init(&ctx);
	atexit(cleanup);
	printf("\x1b[2J");   // Clear screen
	printf("\x1b[H");    // Move cursor to home
	printf("\x1b[?25l"); // Hide cursor
	fflush(stdout);

	char obj_path[128] = RESOURCES_PATH "Grass_Block.obj";
	//char obj_path[128] = RESOURCES_PATH "DabrovikSponza/sponza.obj";
	mesh_t mesh;
	int ret = load_obj(obj_path, &mesh);
	if (ret != 0) {
		fprintf(stderr, "Failed to load mesh: %d\nFilepath: %s", ret, obj_path);
		return 1;
	}

	//print_material_info(mesh.materials, mesh.num_materials);

	int rows, cols;
	if (tio_get_window_size(&ctx, &rows, &cols) == -1) {
		fprintf(stderr, "Unable to get window size\n");
		return 1;
	}
	//cols = 80;
	//rows = 45;
	//printf("Window size: %d rows, %d cols\n", rows, cols);
	rows *= 12;
	cols *= 6;

	mat4 model_matrix, view_matrix, projection_matrix, model_view_projection, model_view;
	mat3 normal_matrix;
	glm_mat4_identity(projection_matrix);
	glm_translate(view_matrix, (vec3) { 0.0f, 0.0f, 0.0f });
	glm_perspective(glm_rad(90.0f), (float)cols / (float)rows, near_plane, far_plane, projection_matrix);

	update_model_matrix(model_matrix);
	update_view_matrix(view_matrix);
	update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);

	framebuffer_4i8 fb = create_framebuffer_4i8(cols, rows);
	framebuffer_i32 ib = create_framebuffer_i32(cols, rows);
	framebuffer_f depth_buffer = create_framebuffer_f(cols, rows);

	sixel_display_ctx sixel_ctx;
	init_sixel_display_ctx(&sixel_ctx, cols, rows);
	init_sixel_indexed_bitmap(&sixel_ctx.bitmap, cols, rows);
	init_sixel_palette_rgbuniform(&sixel_ctx.bitmap.palette, 5);

	monotonic_timer_t timer, timer_whole;
	timer_start(&timer_whole);

	double total_rasterization_time = 0.0;
	double total_conversion_time = 0.0;
	double total_generation_time = 0.0;
	double total_display_time = 0.0;

	int hit_triangle_idx = -1;

	int num_frames = 500;
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
					first_person_camera(event.code, model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
					break;
				case '1':
					use_avx2 = 1;break;
				case '2':
					use_avx2 = 0;break;
				case 'i':
				case 'I':
					index_only = !index_only;break;
				case 't':
				case 'T':
					texture_index_only = !texture_index_only;break;

				}
			}
			else if (event.type == TIO_INPUT_EVENT_TYPE_MOUSE) {
				mousex = 10 * event.position_x + 10;
				mousey = 20 * event.position_y;
				mousex = clamp_int(mousex, 0, cols - 1);
				mousey = clamp_int(mousey, 0, cols - 1);
				if (event.code == LMB_DOWN) {
					hit_triangle_idx = ray_cast(&mesh, model_view, mousex, mousey, cols, rows);
					if (hit_triangle_idx == -1) {
						mesh.start_triangle_index = 0;
						mesh.end_triangle_index = mesh.attrib.num_face_num_verts;
					}
					else {
						mesh.start_triangle_index = hit_triangle_idx;
						mesh.end_triangle_index = hit_triangle_idx + 1;
					}
				}
			}
		}

		timer_start(&timer);
		clear_framebuffer_4i8(&fb, 255, 0, 255, 255);
		clear_framebuffer_f(&depth_buffer, far_plane);
		clear_framebuffer_i32(&ib, -1);
		render_mesh(&mesh, model_view_projection, normal_matrix, model_view, &fb, &depth_buffer, &ib);
		double rasterization_elapsed_ms = timer_elapsed_ms(&timer);
		total_rasterization_time += rasterization_elapsed_ms;

		//draw_square(&fb, mousex, mousey, 10, cols, rows);

		timer_start(&timer);
		if (index_only == 0)
			convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors2(&sixel_ctx.bitmap, fb);
		else
			convert_i32_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_rainbow(&sixel_ctx.bitmap, ib);
		double conversion_elapsed_ms = timer_elapsed_ms(&timer);
		total_conversion_time += conversion_elapsed_ms;

		timer_start(&timer);
		generate_sixel_display_data(&sixel_ctx);
		double generation_elapsed_ms = timer_elapsed_ms(&timer);
		total_generation_time += generation_elapsed_ms;

		timer_start(&timer);
		if (tio_write(&ctx, sixel_ctx.data, sixel_ctx.data_size) == -1) {
			goto end;
		}
		double display_elapsed_ms = timer_elapsed_ms(&timer);
		total_display_time += display_elapsed_ms;


		printf("\x1b[H");    // Move cursor to home
		printf("\r\n");
		//printf("Mouse: (%d, %d)              \r\n", mousex, mousey);
		//printf("Screen size: %d rows, %d cols, %d pixels\n", rows, cols, rows * cols);
		//printf("Texture Accesses: %d\r\n", texture_access_count);
		//printf("Ray Intersected Triangle Index: %d                \r\n", hit_triangle_idx);
		printf("Rasterization: %0.2f\r\n", rasterization_elapsed_ms);
		printf("Conversion:    %0.2f\r\n", conversion_elapsed_ms);
		printf("Generation:    %0.2f\r\n", generation_elapsed_ms);
		printf("Display:       %0.2f\r\n", display_elapsed_ms);
		fflush(stdout);
		texture_access_count = 0;
	}

end:

	printf("\r\n");
	printf("Total times:\r\n");
	printf("Rasterization: %0.2f\r\n", total_rasterization_time);
	printf("Conversion:    %0.2f\r\n", total_conversion_time);
	printf("Generation:    %0.2f\r\n", total_generation_time);
	printf("Display:       %0.2f\r\n", total_display_time);
	fflush(stdout);
	free_framebuffer_4i8(&fb);
	free_framebuffer_f(&depth_buffer);
	tinyobj_attrib_free(&(mesh.attrib));
	tinyobj_shapes_free(mesh.shapes, mesh.num_shapes);


	double whole_elapsed_ms = timer_elapsed_ms(&timer_whole);
	printf("\r\nTotal time for %d frames: %0.2f ms\r\n", 100, whole_elapsed_ms);
	printf("\x1b[?25h"); // Show cursor
	fflush(stdout);

	return 0;
}