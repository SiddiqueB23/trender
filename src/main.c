#include "framebuffer_4i8.h"
#include "framebuffer_f.h"
#include "mesh_loading.h"
#include "rendering.h"
#include "sixel_display.h"
#include "timer.h"
#include "tio.h"
#include <math.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

float camera_x = 0.0f, camera_y = 2.0f, camera_z = -2.0f;
float camera_pitch = 0.0f, camera_yaw = 0.0f;
void update_view_matrix(mat4 view_matrix) {
	glm_mat4_identity(view_matrix);
	glm_rotate_x(view_matrix, glm_rad(camera_pitch), view_matrix);
	glm_rotate_y(view_matrix, glm_rad(camera_yaw), view_matrix);
	glm_translate(view_matrix, (vec3) { -camera_x, -camera_y, camera_z });
}

float near_plane = 0.5f;
float far_plane = 10.0f;

tio_ctx_t ctx;

void cleanup(void) {
	tio_destroy(&ctx);
}

int main() {

	tio_init(&ctx);
	atexit(cleanup);
	printf("\x1b[2J");   // Clear screen
	printf("\x1b[H");    // Move cursor to home
	printf("\x1b[?25l"); // Hide cursor
	fflush(stdout);

	const char mesh_path[] = RESOURCES_PATH "Grass_Block.obj";
	tinyobj_attrib_t attrib;
	tinyobj_shape_t* shapes = NULL;
	size_t num_shapes;
	tinyobj_material_t* materials = NULL;
	size_t num_materials;
	int ret = load_mesh(mesh_path, &attrib, &shapes, &num_shapes, &materials, &num_materials);
	if (ret != 0) {
		fprintf(stderr, "Failed to load mesh: %d\nFilepath: %s", ret, mesh_path);
		return 1;
	}

	const char* texture_path = RESOURCES_PATH "Grass_Block_TEX.png";
	int tex_width, tex_height, tex_channels = 4;
	unsigned char* texture = NULL;
	texture = stbi_load(texture_path, &tex_width, &tex_height, &tex_channels, tex_channels);
	if (texture == NULL) {
		fprintf(stderr, "Failed to load texture image\nFilepath: %s", texture_path);
		return 1;
	}

	int rows, cols;
	if (tio_get_window_size(&ctx, &rows, &cols) == -1) {
		fprintf(stderr, "Unable to get window size\n");
		return 1;
	}
	cols = 80;
	rows = 45;
	//rows *= 2;
	//rows -= 2;
	rows *= 8;
	cols *= 8;
	printf("Window size: %d rows, %d cols\n", rows, cols);

	init_gamma_lut();

	mat4 model_matrix, view_matrix, projection_matrix, model_view_projection, model_view;
	mat3 normal_matrix;
	glm_mat4_identity(projection_matrix);
	glm_translate(view_matrix, (vec3) { 0.0f, 0.0f, 0.0f });
	glm_perspective(glm_rad(90.0f), (float)cols / (float)rows, near_plane, far_plane, projection_matrix);

	update_model_matrix(model_matrix);
	update_view_matrix(view_matrix);
	update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);

	framebuffer_4i8 fb = create_framebuffer_4i8(cols, rows);
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
				else if (event.code == 'd') {
					camera_x += 0.1f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == 'a') {
					camera_x -= 0.1f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == 'w') {
					camera_z += 0.1f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == 's') {
					camera_z -= 0.1f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == 'q') {
					camera_y += 0.1f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == 'e') {
					camera_y -= 0.1f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == ARROW_LEFT) {
					camera_yaw -= 10.0f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == ARROW_RIGHT) {
					camera_yaw += 10.0f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == ARROW_UP) {
					camera_pitch -= 10.0f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				else if (event.code == ARROW_DOWN) {
					camera_pitch += 10.0f;
					update_view_matrix(view_matrix);
					update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
				}
				//else if (event.code == '1') {
				//	mode = 1;
				//}
				//else if (event.code == '2') {
				//	mode = 2;
				//}
			}
		}

		timer_start(&timer);
		clear_framebuffer_4i8(&fb, 0, 0, 0, 255);
		clear_framebuffer_f(&depth_buffer, far_plane);
		render_mesh(attrib, model_view_projection, normal_matrix, model_view, &fb, &depth_buffer, texture, tex_width, tex_height);
		double rasterization_elapsed_ms = timer_elapsed_ms(&timer);
		total_rasterization_time += rasterization_elapsed_ms;

		timer_start(&timer);
		//convert_4i8_to_sixel_indexed_bitmap_rgbuniform(&sixel_ctx.bitmap, fb, 5);
		//convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering(&sixel_ctx.bitmap, fb, 5);
		convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors2(&sixel_ctx.bitmap, fb);
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
		printf("Rasterization: %0.2f\r\n", rasterization_elapsed_ms);
		printf("Conversion:    %0.2f\r\n", conversion_elapsed_ms);
		printf("Generation:    %0.2f\r\n", generation_elapsed_ms);
		printf("Display:       %0.2f\r\n", display_elapsed_ms);
		fflush(stdout);
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
	tinyobj_attrib_free(&attrib);
	tinyobj_shapes_free(shapes, num_shapes);
	tinyobj_materials_free(materials, num_materials);


	double whole_elapsed_ms = timer_elapsed_ms(&timer_whole);
	printf("\r\nTotal time for %d frames: %0.2f ms\r\n", 100, whole_elapsed_ms);
	printf("\x1b[?25h"); // Show cursor
	fflush(stdout);

	return 0;
}