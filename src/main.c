#define NUM_THREADS 4
#define BUFFER_COUNT 3

#include "framebuffer_4i8.h"
#include "framebuffer_f.h"
#include "mesh_loading.h"
#include "raycast.h"
#include "rendering_avx2_u16_mt.h"
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

tio_ctx_t tio_ctx;

void cleanup(void) {
	tio_destroy(&tio_ctx);
}

int mousex = 0, mousey = 0;

void draw_square(framebuffer_4i8* fb, int x, int y, int width, int screen_width, int screen_height) {
	for (int i = y; i < y + width && i < screen_height; i++) {
		for (int j = x; j < x + width && j < screen_width; j++) {
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

	tio_init(&tio_ctx);
	atexit(cleanup);
	printf("\x1b[2J");   // Clear screen
	printf("\x1b[H");    // Move cursor to home
	printf("\x1b[?25l"); // Hide cursor
	fflush(stdout);

	//char obj_path[128] = RESOURCES_PATH "Grass_Block.obj";
	char obj_path[128] = RESOURCES_PATH "DabrovikSponza/sponza.obj";
	mesh_t mesh;
	int ret = load_obj(obj_path, &mesh);
	if (ret != 0) {
		fprintf(stderr, "Failed to load mesh: %d\nFilepath: %s", ret, obj_path);
		return 1;
	}

	//print_material_info(mesh.materials, mesh.num_materials);
	for (int i = 0; i < mesh.num_materials; i++) {
		if (mesh.materials[i].diffuse_texture.data != NULL)
			convert_8r8g8b8a_to_5r6g5b_texture(&mesh.materials[i].diffuse_texture);
	}

	int rows, cols;
	if (tio_get_window_size(&tio_ctx, &rows, &cols) == -1) {
		fprintf(stderr, "Unable to get window size\n");
		return 1;
	}
	//printf("Window size: %d rows, %d cols\n", rows, cols);
	rows *= 2;
	rows *= 6;
	cols *= 6;
	rows -= rows % 6;
	cols -= cols % 8;

	mat4 model_matrix, view_matrix, projection_matrix, model_view_projection, model_view;
	mat3 normal_matrix;
	glm_mat4_identity(projection_matrix);
	glm_translate(view_matrix, (vec3) { 0.0f, 0.0f, 0.0f });
	glm_perspective(glm_rad(90.0f), (float)cols / (float)rows, near_plane, far_plane, projection_matrix);

	update_model_matrix(model_matrix);
	update_view_matrix(view_matrix);
	update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);

	rendering_ctx_t render_ctx[BUFFER_COUNT][NUM_THREADS];
	sixel_display_ctx sixel_ctx[BUFFER_COUNT][NUM_THREADS];

	int front[NUM_THREADS];
	int back[NUM_THREADS];
	for (int i = 0;i < NUM_THREADS;i++) {
		front[i] = 0;
		back[i] = 0;
	}

	if (NUM_THREADS == 1) {
		render_ctx[0][0] = create_rendering_ctx(cols, rows, 0, rows);
		init_sixel_display_ctx(&sixel_ctx[0][0], cols, rows);
		init_sixel_indexed_bitmap(&sixel_ctx[0][0].bitmap, cols, rows);
		init_sixel_palette_rgbuniform(&sixel_ctx[0][0].bitmap.palette, 5);
	}
	else {
		for (int b = 0;b < BUFFER_COUNT;b++) {
			for (int i = 1; i < NUM_THREADS; i++) {
				int starty, endy;
				starty = (rows * (i - 1)) / (NUM_THREADS - 1);
				endy = (rows * ((i - 1) + 1)) / (NUM_THREADS - 1);
				starty -= starty % 6;
				endy -= endy % 6;
				int rows_per_thread = endy - starty;
				render_ctx[b][i - 1] = create_rendering_ctx(cols, rows, starty, endy);
				init_sixel_display_ctx(&sixel_ctx[b][i - 1], cols, rows_per_thread);
				init_sixel_indexed_bitmap(&sixel_ctx[b][i - 1].bitmap, cols, rows_per_thread);
				init_sixel_palette_rgbuniform(&sixel_ctx[b][i - 1].bitmap.palette, 5);
			}
		}
	}

	omp_lock_t buffer_locks[BUFFER_COUNT][NUM_THREADS];
	for (int b = 0;b < BUFFER_COUNT;b++) {
		for (int i = 0; i < NUM_THREADS - 1; i++) {
			omp_init_lock(&buffer_locks[b][i]);
		}
	}

	monotonic_timer_t timer, timer_whole;
	timer_start(&timer_whole);

	double total_processing_time = 0.0;
	double total_display_time = 0.0;

	double processing_time = 0.0;
	double display_time = 0.0;

	double previous_end_time = timer_elapsed_ms(&timer_whole);
	double frame_time = 0.0;
	double total_frame_time = 0.0;
	double current_end_time = 0.0;

	int hit_triangle_idx = -1;

#pragma omp parallel num_threads(NUM_THREADS) default(shared)
	{
		int thread_id = omp_get_thread_num();
		int num_frames = 500;
		int num_frame_counter = num_frames;
		if (thread_id >= 1) {
			render_mesh(&mesh, model_view_projection, normal_matrix, model_view, &render_ctx[back[thread_id - 1]][thread_id - 1]);
			convert_5r6g5b_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_avx2_v2(&sixel_ctx[back[thread_id - 1]][thread_id - 1].bitmap, render_ctx[back[thread_id - 1]][thread_id - 1].output_buffer);
			generate_sixel_display_data(&sixel_ctx[back[thread_id - 1]][thread_id - 1]);
			int old_back = back[thread_id - 1];
			back[thread_id - 1] = (old_back + 1) % BUFFER_COUNT;
			omp_set_lock(&buffer_locks[back[thread_id - 1]][thread_id - 1]);
		}
#pragma omp barrier
		if (NUM_THREADS >= 2 && thread_id == 0) {
			for (int i = 0;i < NUM_THREADS - 1;i++) {
				omp_set_lock(&buffer_locks[front[i]][i]);
			}
		}
#pragma omp barrier
		while (num_frame_counter--) {
			if (thread_id == 0)
			{
				int current_event_queue_bytes_size = tio_get_event_queue_byte_size(&tio_ctx);
				int event_bytes_processed = 0;
				while (event_bytes_processed < current_event_queue_bytes_size) {
					tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
					int bytes_processed = tio_pop_event_queue(&tio_ctx, &event);
					event_bytes_processed += bytes_processed;
					if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
						if (event.code == 'Q' || event.code == CTRL_Q) {
							exit(-1);
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
						}
					}
					else if (event.type == TIO_INPUT_EVENT_TYPE_MOUSE) {
						mousex = 10 * event.position_x;
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
			}
			if (NUM_THREADS == 1) {
				render_mesh(&mesh, model_view_projection, normal_matrix, model_view, &render_ctx[back[0]][0]);
			}
			else if (thread_id >= 1) {
				render_mesh(&mesh, model_view_projection, normal_matrix, model_view, &render_ctx[back[thread_id - 1]][thread_id - 1]);
				convert_5r6g5b_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_avx2_v2(&sixel_ctx[back[thread_id - 1]][thread_id - 1], render_ctx[back[thread_id - 1]][thread_id - 1].output_buffer);
				generate_sixel_display_data(&sixel_ctx[back[thread_id - 1]][thread_id - 1]);
				int old_back = back[thread_id - 1];
				back[thread_id - 1] = (old_back + 1) % BUFFER_COUNT;
				omp_set_lock(&buffer_locks[back[thread_id - 1]][thread_id - 1]);
				omp_unset_lock(&buffer_locks[old_back][thread_id - 1]);
			}
			if (NUM_THREADS == 1) {
				convert_5r6g5b_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering_216colors_avx2_v2(&sixel_ctx[back[0]][0], render_ctx[back[0]][0].output_buffer);
				generate_sixel_display_data(&sixel_ctx[back[0]][0]);
			}
			if (thread_id == 0) {
				display_time = 0.0;
				if (NUM_THREADS == 1) {
					timer_start(&timer);
					tio_write(&tio_ctx, sixel_ctx[front[0]][0].data, sixel_ctx[front[0]][0].data_size);
					display_time += timer_elapsed_ms(&timer);
				}
				if (NUM_THREADS == 2) {
					timer_start(&timer);
					tio_write(&tio_ctx, sixel_ctx[front[0]][0].data, sixel_ctx[front[0]][0].data_size);
					display_time += timer_elapsed_ms(&timer);
					int old_front = front[0];
					front[0] = (old_front + 1) % BUFFER_COUNT;
					omp_set_lock(&buffer_locks[front[0]][0]);
					omp_unset_lock(&buffer_locks[old_front][0]);
				}
				else if (NUM_THREADS >= 3) {
					int footer_len = 2;
					timer_start(&timer);
					tio_write(&tio_ctx, sixel_ctx[front[0]][0].data, sixel_ctx[front[0]][0].data_size - footer_len);
					display_time += timer_elapsed_ms(&timer);
					int old_front = front[0];
					front[0] = (old_front + 1) % BUFFER_COUNT;
					omp_set_lock(&buffer_locks[front[0]][0]);
					omp_unset_lock(&buffer_locks[old_front][0]);
					for (int i = 1; i < NUM_THREADS - 2; i++) {
						timer_start(&timer);
						tio_write(&tio_ctx, sixel_ctx[front[i]][i].sixel_data, sixel_ctx[front[i]][i].sixel_data_size - footer_len);
						display_time += timer_elapsed_ms(&timer);
						old_front = front[i];
						front[i] = (old_front + 1) % BUFFER_COUNT;
						omp_set_lock(&buffer_locks[front[i]][i]);
						omp_unset_lock(&buffer_locks[old_front][i]);
					}
					timer_start(&timer);
					tio_write(&tio_ctx, sixel_ctx[front[NUM_THREADS - 2]][NUM_THREADS - 2].sixel_data, sixel_ctx[front[NUM_THREADS - 2]][NUM_THREADS - 2].sixel_data_size);
					display_time += timer_elapsed_ms(&timer);
					old_front = front[NUM_THREADS - 2];
					front[NUM_THREADS - 2] = (old_front + 1) % BUFFER_COUNT;
					omp_set_lock(&buffer_locks[front[NUM_THREADS - 2]][NUM_THREADS - 2]);
					omp_unset_lock(&buffer_locks[old_front][NUM_THREADS - 2]);
				}
				total_display_time += display_time;

				current_end_time = timer_elapsed_ms(&timer_whole);
				frame_time = current_end_time - previous_end_time;
				previous_end_time = current_end_time;
				total_frame_time += frame_time;

				processing_time = fmaxf(0.0f, frame_time - display_time);
				total_processing_time += processing_time;

				printf("\x1b[H");    // Move cursor to home
				printf("\r\n");
				//printf("Mouse: (%d, %d)              \r\n", mousex, mousey);
				//printf("Screen size: %d rows, %d cols, %d pixels\n", rows, cols, rows * cols);
				//printf("Ray Intersected Triangle Index: %d                \r\n", hit_triangle_idx);
				//printf("Processing:    %0.2f\r\n", processing_time);
				//printf("Display:       %0.2f\r\n", display_time);
				printf("Frame time:    %0.2f\r\n", frame_time);
				fflush(stdout);
			}
		}

		//end:
		if (thread_id == 0) {
			printf("\r\n");
			printf("Total times:\r\n");
			printf("Processing:    %0.2f\r\n", total_processing_time);
			printf("Display:       %0.2f\r\n", total_display_time);
			printf("Frame time:    %0.2f\r\n", total_frame_time);
			printf("Average times:\r\n");
			printf("Processing:    %0.2f\r\n", total_processing_time / (float)num_frames);
			printf("Display:       %0.2f\r\n", total_display_time / (float)num_frames);
			printf("Frame time:    %0.2f\r\n", total_frame_time / (float)num_frames);
		}
	}

#pragma omp barrier
	double whole_time = timer_elapsed_ms(&timer_whole);

	double total_clear_time = render_ctx[0][0].total_clear_time;
	double total_rasterisation_time = render_ctx[0][0].total_rasterisation_time;
	double total_texture_sampling_time = render_ctx[0][0].total_texture_sampling_time;
	double total_generation_time = sixel_ctx[0][0].total_generation_time;
	double total_conversion_time = sixel_ctx[0][0].total_conversion_time;
	for (int b = 0;b < BUFFER_COUNT;b++) {
		for (int i = 0; i < NUM_THREADS - 1; i++) {
			if (b == 0 && i == 0)continue;
			total_clear_time += render_ctx[b][i].total_clear_time;
			total_rasterisation_time += render_ctx[b][i].total_rasterisation_time;
			total_texture_sampling_time += render_ctx[b][i].total_texture_sampling_time;
			total_generation_time += sixel_ctx[b][i].total_generation_time;
			total_conversion_time += sixel_ctx[b][i].total_conversion_time;
		}
	}

	double total_frame_gen_time =
		total_clear_time +
		total_rasterisation_time +
		total_texture_sampling_time +
		total_generation_time +
		total_conversion_time;
	double unaccounted_time = whole_time - total_frame_gen_time - total_display_time;

	printf("total_clear_time:            %0.2f\r\n", total_clear_time);
	printf("total_rasterisation_time:    %0.2f\r\n", total_rasterisation_time);
	printf("total_texture_sampling_time: %0.2f\r\n", total_texture_sampling_time);
	printf("total_conversion_time:       %0.2f\r\n", total_conversion_time);
	printf("total_generation_time:       %0.2f\r\n", total_generation_time);
	printf("total_frame_gen_time:        %0.2f\r\n", total_frame_gen_time);
	printf("total_display_time:          %0.2f\r\n", total_display_time);
	printf("Total time for %d frames:    %0.2f ms\r\n", 500, whole_time);
	printf("unaccounted_time:            %0.2f\r\n", unaccounted_time);

	fflush(stdout);
	//tinyobj_attrib_free(&(mesh.attrib));
	//tinyobj_shapes_free(mesh.shapes, mesh.num_shapes);

	printf("\x1b[?25h"); // Show cursor
	fflush(stdout);

	return 0;
}