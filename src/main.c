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

void update_matrices(mat4 model_matrix, mat4 view_matrix, mat4 projection_matrix,
                     mat4 model_view, mat4 model_view_projection, mat3 normal_matrix) {
    glm_mat4_mul(view_matrix, model_matrix, model_view);
    glm_mat4_mul(projection_matrix, model_view, model_view_projection);
    glm_mat4_pick3(model_view, normal_matrix);
    glm_mat3_inv(normal_matrix, normal_matrix);
    glm_mat3_transpose(normal_matrix);
}

int main() {

    tinyobj_attrib_t attrib;
    tinyobj_shape_t *shapes = NULL;
    size_t num_shapes;
    tinyobj_material_t *materials = NULL;
    size_t num_materials;
    int ret = load_mesh("resources/Grass_Block.obj", &attrib, &shapes, &num_shapes, &materials, &num_materials);
    if (ret != 0) {
        fprintf(stderr, "Failed to load mesh: %d\n", ret);
        return 1;
    }

    int tex_width, tex_height, tex_channels = 4;
    unsigned char *texture = NULL;
    texture = stbi_load("resources/Grass_Block_TEX.png", &tex_width, &tex_height, &tex_channels, tex_channels);
    if (texture == NULL) {
        fprintf(stderr, "Failed to load texture image\n");
        return 1;
    }

    int rows, cols;
    if (get_window_size(STDIN_FILENO, STDOUT_FILENO, &rows, &cols) == -1) {
        fprintf(stderr, "Unable to get window size\n");
        return 1;
    }
    cols = 80;
    rows = 50;
    // rows -= 2;
    rows *= 10;
    cols *= 10;
    printf("Window size: %d rows, %d cols\n", rows, cols);

    mat4 model_matrix, view_matrix, projection_matrix, model_view_projection, model_view;
    mat3 normal_matrix;
    glm_mat4_identity(model_matrix);
    glm_mat4_identity(view_matrix);
    glm_mat4_identity(projection_matrix);
    glm_scale(model_matrix, (vec3){4.0f, 4.0f, 1.0f});
    glm_translate(model_matrix, (vec3){0.0f, -2.1f, -5.0f});
    glm_rotate_y(model_matrix, glm_rad(45.0f), model_matrix);
    glm_translate(view_matrix, (vec3){0.0f, 0.0f, -3.0f});
    glm_perspective(glm_rad(45.0f), (float)cols / (float)rows, near_plane, far_plane, projection_matrix);

    update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);

    tio_input_event_queue iq;
    tio_input_queue_init(&iq, 128);

    enable_raw_mode();
    atexit(disable_raw_mode);
    printf("\x1b[2J");   // Clear screen
    printf("\x1b[H");    // Move cursor to home
    printf("\x1b[?25l"); // Hide cursor
    fflush(stdout);

    framebuffer_4i8 fb = create_framebuffer_4i8(cols, rows);
    framebuffer_f depth_buffer = create_framebuffer_f(cols, rows);

    sixel_display_ctx sixel_ctx;
    init_sixel_display_ctx(&sixel_ctx, cols, rows);
    init_sixel_indexed_bitmap(&sixel_ctx.bitmap, cols, rows);
    init_sixel_palette_rgbuniform(&sixel_ctx.bitmap.palette, 5);

    monotonic_timer_t timer;
    
    int num_frames = 100;
    while (num_frames--) {
        // debug_msg_len = 0;

        // tio_input_event event = term_read_key();
        update_event_queue(&iq, iq.max_events / 2, iq.max_events / 2);
        tio_input_event event = TIO_INPUT_EVENT_INITIALIZER;
        while (tio_input_queue_pop(&iq, &event) == 0) {
            if (event.type == TIO_INPUT_EVENT_TYPE_KEY) {
                if (event.code == 'q' || event.code == 'Q' || event.code == CTRL_Q)
                    goto end;
                if (event.code == 'd') {
                    glm_rotate_y(model_matrix, glm_rad(10.0f), model_matrix);
                    update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
                } else if (event.code == 'a') {
                    glm_rotate_y(model_matrix, glm_rad(-10.0f), model_matrix);
                    update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
                } else if (event.code == 'w') {
                    glm_translate_z(view_matrix, 0.1f);
                    update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
                } else if (event.code == 's') {
                    glm_translate_z(view_matrix, -0.1f);
                    update_matrices(model_matrix, view_matrix, projection_matrix, model_view, model_view_projection, normal_matrix);
                } else if (event.code == '1') {
                    mode = 1;
                } else if (event.code == '2') {
                    mode = 2;
                }
            }
        }

        timer_start(&timer);
        clear_framebuffer_4i8(&fb, 0, 0, 0, 255);
        // clear_framebuffer_f(&depth_buffer, far_plane);
        clear_framebuffer_f(&depth_buffer, 1.0f);
        rasterize_mesh(attrib, model_view_projection, normal_matrix, model_view, &fb, &depth_buffer, texture, tex_width, tex_height);
        double rasterization_elapsed_ms = timer_elapsed_ms(&timer);

        timer_start(&timer);
        // convert_4i8_to_sixel_indexed_bitmap_rgbuniform(&sixel_ctx.bitmap, fb, 5);
        convert_4i8_to_sixel_indexed_bitmap_rgbuniform_ordered_dithering(&sixel_ctx.bitmap, fb, 5);
        double conversion_elapsed_ms = timer_elapsed_ms(&timer);

        timer_start(&timer);
        generate_sixel_display_data(&sixel_ctx);
        double generation_elapsed_ms = timer_elapsed_ms(&timer);
        
        timer_start(&timer);
        if (tio_write(sixel_ctx.data, sixel_ctx.data_size) == -1) {
            goto end;
        }
        double display_elapsed_ms = timer_elapsed_ms(&timer);

        printf("\r\n");
        printf("Rasterization: %0.2f\r\n", rasterization_elapsed_ms);
        printf("Conversion:    %0.2f\r\n", conversion_elapsed_ms);
        printf("Generation:    %0.2f\r\n", generation_elapsed_ms);
        printf("Display:       %0.2f\r\n", display_elapsed_ms);
        fflush(stdout);

        // debug_msgs[debug_msg_len] = '\0';
        // printf("\x1b[0J\r\n%s\r\n", debug_msgs);
    } 

end:
    free_framebuffer_4i8(&fb);
    free_framebuffer_f(&depth_buffer);
    tinyobj_attrib_free(&attrib);
    tinyobj_shapes_free(shapes, num_shapes);
    tinyobj_materials_free(materials, num_materials);

    // disable_raw_mode(STDIN_FILENO);
    printf("\x1b[?25h"); // Show cursor
    fflush(stdout);

    return 0;
}