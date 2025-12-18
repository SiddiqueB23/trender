#ifndef RENDERING_H
#define RENDERING_H

#include "cglm/cglm.h"
#include "framebuffer_4i8.h"
#include "framebuffer_f.h"
#include "mesh_loading.h"
#include "utils.h"

typedef struct {
    float mvpx, mvpy, mvpz, mvpw;
    float r, g, b;
    float u, v;
    float nx, ny, nz;
    float mvx, mvy, mvz;
} triangle_2d_vertex_t;

typedef struct {
    triangle_2d_vertex_t v0, v1, v2;
} triangle_2d_t;

float near_plane = 0.5f;
float far_plane = 10.0f;

void get_barycentric_coordinates(triangle_2d_t triangle, float px, float py, float *alpha, float *beta, float *gamma) {
    float denom = (triangle.v1.mvpy - triangle.v2.mvpy) * (triangle.v0.mvpx - triangle.v2.mvpx) +
                  (triangle.v2.mvpx - triangle.v1.mvpx) * (triangle.v0.mvpy - triangle.v2.mvpy);
    *alpha = ((triangle.v1.mvpy - triangle.v2.mvpy) * (px - triangle.v2.mvpx) +
              (triangle.v2.mvpx - triangle.v1.mvpx) * (py - triangle.v2.mvpy)) /
             denom;
    *beta = ((triangle.v2.mvpy - triangle.v0.mvpy) * (px - triangle.v2.mvpx) +
             (triangle.v0.mvpx - triangle.v2.mvpx) * (py - triangle.v2.mvpy)) /
            denom;
    *gamma = 1.0f - *alpha - *beta;
}

void get_barycentric_coordinates_2(triangle_2d_t triangle, float px, float py, float *u, float *v, float *w) {
    vec2 a = {triangle.v0.mvpx, triangle.v0.mvpy};
    vec2 b = {triangle.v1.mvpx, triangle.v1.mvpy};
    vec2 c = {triangle.v2.mvpx, triangle.v2.mvpy};
    vec2 p = {px, py};
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

void sample_texture_nearest(unsigned char *texture, int tex_width, int tex_height, float u, float v, float *r, float *g, float *b) {
    int x = (int)lroundf(u * (float)(tex_width - 1));
    int y = (int)lroundf((1.0f - v) * (float)(tex_height - 1)); // Flip V coordinate
    x = clamp_int(x, 0, tex_width - 1);
    y = clamp_int(y, 0, tex_height - 1);
    int index = (y * tex_width + x) * 4; // Assuming 4 channels (RGBA)
    *r = glm_clamp((float)texture[index + 0] / 255.0f, 0.0f, 1.0f);
    *g = glm_clamp((float)texture[index + 1] / 255.0f, 0.0f, 1.0f);
    *b = glm_clamp((float)texture[index + 2] / 255.0f, 0.0f, 1.0f);
}

void lerp_projected(triangle_2d_t triangle, float alpha, float beta, float gamma, triangle_2d_vertex_t *out) {
    out->mvpw = 1.0f / (alpha / triangle.v0.mvpw + beta / triangle.v1.mvpw + gamma / triangle.v2.mvpw);
    out->u = out->mvpw * (alpha * triangle.v0.u / triangle.v0.mvpw + beta * triangle.v1.u / triangle.v1.mvpw + gamma * triangle.v2.u / triangle.v2.mvpw);
    out->v = out->mvpw * (alpha * triangle.v0.v / triangle.v0.mvpw + beta * triangle.v1.v / triangle.v1.mvpw + gamma * triangle.v2.v / triangle.v2.mvpw);
    out->r = out->mvpw * (alpha * triangle.v0.r / triangle.v0.mvpw + beta * triangle.v1.r / triangle.v1.mvpw + gamma * triangle.v2.r / triangle.v2.mvpw);
    out->g = out->mvpw * (alpha * triangle.v0.g / triangle.v0.mvpw + beta * triangle.v1.g / triangle.v1.mvpw + gamma * triangle.v2.g / triangle.v2.mvpw);
    out->b = out->mvpw * (alpha * triangle.v0.b / triangle.v0.mvpw + beta * triangle.v1.b / triangle.v1.mvpw + gamma * triangle.v2.b / triangle.v2.mvpw);
    float nx = out->mvpw * (alpha * triangle.v0.nx / triangle.v0.mvpw + beta * triangle.v1.nx / triangle.v1.mvpw + gamma * triangle.v2.nx / triangle.v2.mvpw);
    float ny = out->mvpw * (alpha * triangle.v0.ny / triangle.v0.mvpw + beta * triangle.v1.ny / triangle.v1.mvpw + gamma * triangle.v2.ny / triangle.v2.mvpw);
    float nz = out->mvpw * (alpha * triangle.v0.nz / triangle.v0.mvpw + beta * triangle.v1.nz / triangle.v1.mvpw + gamma * triangle.v2.nz / triangle.v2.mvpw);
    vec3 normal = {nx, ny, nz};
    glm_vec3_normalize(normal);
    out->nx = normal[0];
    out->ny = normal[1];
    out->nz = normal[2];
    out->mvx = out->mvpw * (alpha * triangle.v0.mvx / triangle.v0.mvpw + beta * triangle.v1.mvx / triangle.v1.mvpw + gamma * triangle.v2.mvx / triangle.v2.mvpw);
    out->mvy = out->mvpw * (alpha * triangle.v0.mvy / triangle.v0.mvpw + beta * triangle.v1.mvy / triangle.v1.mvpw + gamma * triangle.v2.mvy / triangle.v2.mvpw);
    out->mvz = out->mvpw * (alpha * triangle.v0.mvz / triangle.v0.mvpw + beta * triangle.v1.mvz / triangle.v1.mvpw + gamma * triangle.v2.mvz / triangle.v2.mvpw);
    out->mvpz = out->mvpw * (alpha * triangle.v0.mvpz / triangle.v0.mvpw + beta * triangle.v1.mvpz / triangle.v1.mvpw + gamma * triangle.v2.mvpz / triangle.v2.mvpw);
}

void lerp_vertex_pair(triangle_2d_vertex_t v0, triangle_2d_vertex_t v1, triangle_2d_vertex_t *out, float t) {
    out->mvpx = v0.mvpx + t * (v1.mvpx - v0.mvpx);
    out->mvpy = v0.mvpy + t * (v1.mvpy - v0.mvpy);
    out->mvpz = v0.mvpz + t * (v1.mvpz - v0.mvpz);
    out->mvpw = v0.mvpw + t * (v1.mvpw - v0.mvpw);
    out->r = v0.r + t * (v1.r - v0.r);
    out->g = v0.g + t * (v1.g - v0.g);
    out->b = v0.b + t * (v1.b - v0.b);
    out->u = v0.u + t * (v1.u - v0.u);
    out->v = v0.v + t * (v1.v - v0.v);
    float nx = v0.nx + t * (v1.nx - v0.nx);
    float ny = v0.ny + t * (v1.ny - v0.ny);
    float nz = v0.nz + t * (v1.nz - v0.nz);
    vec3 normal = {nx, ny, nz};
    glm_vec3_normalize(normal);
    out->nx = normal[0];
    out->ny = normal[1];
    out->nz = normal[2];
    out->mvx = v0.mvx + t * (v1.mvx - v0.mvx);
    out->mvy = v0.mvy + t * (v1.mvy - v0.mvy);
    out->mvz = v0.mvz + t * (v1.mvz - v0.mvz);
}

static inline int is_inside_near_plane(triangle_2d_vertex_t v) {
    return v.mvpz >= -v.mvpw;
}

static inline float get_intersection_t(triangle_2d_vertex_t inside, triangle_2d_vertex_t outside) {
    float d_in = inside.mvpz + inside.mvpw;
    float d_out = outside.mvpz + outside.mvpw;
    return -d_in / (d_out - d_in);
}

void clip_triangle(triangle_2d_t in, triangle_2d_t *out1, triangle_2d_t *out2, int *num_out) {
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
    // debug_msg_len += sprintf(debug_msgs + debug_msg_len, "Clipping triangle with %d inside vertex(es)\r\n", vertex_count);
    if (vertex_count == 3) {
        *out1 = in;
        *num_out = 1;
        return;
    }
    if (vertex_count == 1) {
        triangle_2d_vertex_t inside_vertex, outside_vertex1, outside_vertex2;
        if (is_inside_near_plane(in.v0)) {
            inside_vertex = in.v0;
            outside_vertex1 = in.v1;
            outside_vertex2 = in.v2;
        } else if (is_inside_near_plane(in.v1)) {
            inside_vertex = in.v1;
            outside_vertex1 = in.v0;
            outside_vertex2 = in.v2;
        } else {
            inside_vertex = in.v2;
            outside_vertex1 = in.v0;
            outside_vertex2 = in.v1;
        }
        float t1 = get_intersection_t(inside_vertex, outside_vertex1);
        float t2 = get_intersection_t(inside_vertex, outside_vertex2);
        triangle_2d_vertex_t new_vertex1, new_vertex2;
        lerp_vertex_pair(inside_vertex, outside_vertex1, &new_vertex1, t1);
        lerp_vertex_pair(inside_vertex, outside_vertex2, &new_vertex2, t2);
        out1->v0 = inside_vertex;
        out1->v1 = new_vertex1;
        out1->v2 = new_vertex2;
        *num_out = 1;
        return;
    }
    if (vertex_count == 2) {
        triangle_2d_vertex_t inside_vertex1, inside_vertex2, outside_vertex;
        if (!is_inside_near_plane(in.v0)) {
            outside_vertex = in.v0;
            inside_vertex1 = in.v1;
            inside_vertex2 = in.v2;
        } else if (!is_inside_near_plane(in.v1)) {
            outside_vertex = in.v1;
            inside_vertex1 = in.v0;
            inside_vertex2 = in.v2;
        } else {
            outside_vertex = in.v2;
            inside_vertex1 = in.v0;
            inside_vertex2 = in.v1;
        }
        float t1 = get_intersection_t(inside_vertex1, outside_vertex);
        float t2 = get_intersection_t(inside_vertex2, outside_vertex);
        triangle_2d_vertex_t new_vertex1, new_vertex2;
        lerp_vertex_pair(inside_vertex1, outside_vertex, &new_vertex1, t1);
        lerp_vertex_pair(inside_vertex2, outside_vertex, &new_vertex2, t2);
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

vec3 light_pos = {5.0f, 5.0f, 5.0f};
vec3 light_color = {1.0, 1.0, 1.0};
float light_power = 40.0;
vec3 ambient_color = {0.0, 0.0, 0.0};
float shininess = 160.0;
float screen_gamma = 2.2;
int mode = 1;

void viewport_transform(triangle_2d_vertex_t *v, int width, int height) {
    v->mvpx = ((v->mvpx / v->mvpw + 1.0f) * 0.5f * (float)width);
    v->mvpy = ((1.0f - (v->mvpy / v->mvpw + 1.0f) * 0.5f) * (float)height);
    v->mvpz /= v->mvpw;
}

void draw_triangle(framebuffer_4i8 *fb, framebuffer_f *depth_buffer, triangle_2d_t triangle, unsigned char *texture, int tex_width, int tex_height) {
    // debug_msg_len += sprintf(debug_msgs + debug_msg_len,
    //                          "v0(%.2f %.2f %.2f %.2f) - %d\r\n"
    //                          "v1(%.2f %.2f %.2f %.2f) - %d\r\n"
    //                          "v2(%.2f %.2f %.2f %.2f) - %d\r\n",
    //                          triangle.v0.mvpx, triangle.v0.mvpy, triangle.v0.mvpz, triangle.v0.mvpw, is_inside_near_plane(triangle.v0),
    //                          triangle.v1.mvpx, triangle.v1.mvpy, triangle.v1.mvpz, triangle.v1.mvpw, is_inside_near_plane(triangle.v1),
    //                          triangle.v2.mvpx, triangle.v2.mvpy, triangle.v2.mvpz, triangle.v2.mvpw, is_inside_near_plane(triangle.v2));

    viewport_transform(&triangle.v0, fb->width, fb->height);
    viewport_transform(&triangle.v1, fb->width, fb->height);
    viewport_transform(&triangle.v2, fb->width, fb->height);

    int min_x = (int)(glm_min(glm_min(triangle.v0.mvpx, triangle.v1.mvpx), triangle.v2.mvpx) - 1.0f);
    int max_x = (int)(glm_max(glm_max(triangle.v0.mvpx, triangle.v1.mvpx), triangle.v2.mvpx) + 1.0f);
    int min_y = (int)(glm_min(glm_min(triangle.v0.mvpy, triangle.v1.mvpy), triangle.v2.mvpy) - 1.0f);
    int max_y = (int)(glm_max(glm_max(triangle.v0.mvpy, triangle.v1.mvpy), triangle.v2.mvpy) + 1.0f);
    min_x = clamp_int(min_x, 0, fb->width - 1);
    max_x = clamp_int(max_x, 0, fb->width - 1);
    min_y = clamp_int(min_y, 0, fb->height - 1);
    max_y = clamp_int(max_y, 0, fb->height - 1);

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float alpha, beta, gamma;
            // get_barycentric_coordinates(triangle, (float)x + 0.5f, (float)y + 0.5f, &alpha, &beta, &gamma);
            get_barycentric_coordinates_2(triangle, (float)x + 0.5f, (float)y + 0.5f, &alpha, &beta, &gamma);
            if (alpha >= 0 && beta >= 0 && gamma >= 0) {

                triangle_2d_vertex_t interp;
                lerp_projected(triangle, alpha, beta, gamma, &interp);
                unsigned char r = 0, g = 0, b = 0;
                vec3 diffuse_color = {interp.r, interp.g, interp.b};
                vec3 spec_color = {1.0, 1.0, 1.0};

                if (texture != NULL) {
                    sample_texture_nearest(texture, tex_width, tex_height, interp.u, interp.v, &diffuse_color[0], &diffuse_color[1], &diffuse_color[2]);
                    diffuse_color[0] = powf(diffuse_color[0], screen_gamma);
                    diffuse_color[1] = powf(diffuse_color[1], screen_gamma);
                    diffuse_color[2] = powf(diffuse_color[2], screen_gamma);
                }

                // r = (unsigned char)clamp_int(lroundf(interp.mvx * 255.0f), 0, 255);
                // g = (unsigned char)clamp_int(lroundf(interp.mvy * 255.0f), 0, 255);
                // b = (unsigned char)clamp_int(lroundf(interp.mvz * 255.0f), 0, 255);

                vec3 normal = {interp.nx, interp.ny, interp.nz};
                vec3 vert_pos = {interp.mvx, interp.mvy, interp.mvz};
                vec3 frag_color;
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

                    // phong
                    if (mode == 2) {
                        vec3 reflect_dir;
                        glm_vec3_scale(light_dir, -1.0f, light_dir);
                        glm_vec3_reflect(light_dir, normal, reflect_dir);
                        spec_angle = glm_max(glm_vec3_dot(reflect_dir, view_dir), 0.0);
                        specular = powf(spec_angle, shininess / 4.0);
                    }
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

                // glm_vec3_scale(diffuse_color, specular, frag_color);
                // glm_vec3_copy((vec3){specular, specular, specular}, frag_color);
                // float depth = (interp.mvpw);
                // float depth = (interp.mvpz)*0.5 + 0.5;
                // glm_vec3_copy(diffuse_color, frag_color);
                // glm_vec3_copy((vec3){depth, depth, depth}, frag_color);
                r = (unsigned char)clamp_int(lroundf(frag_color[0] * 255.0f), 0, 255);
                g = (unsigned char)clamp_int(lroundf(frag_color[1] * 255.0f), 0, 255);
                b = (unsigned char)clamp_int(lroundf(frag_color[2] * 255.0f), 0, 255);
                float current_depth = interp.mvpz;
                get_pixel_f(depth_buffer, x, y, &current_depth);
                if (interp.mvpz < current_depth) {
                    set_pixel_4i8(fb, x, y, r, g, b, 255);
                    set_pixel_f(depth_buffer, x, y, interp.mvpz);
                }
            }
        }
    }
}

void print_triangle(triangle_2d_t triangle) {
    printf("(v0: (x=%.2f, y=%.2f, z=%.2f), v1: (x=%.2f, y=%.2f, z=%.2f), v2: (x=%.2f, y=%.2f, z=%.2f))\r\n",
           triangle.v0.mvpx, triangle.v0.mvpy, triangle.v0.mvpw,
           triangle.v1.mvpx, triangle.v1.mvpy, triangle.v1.mvpw,
           triangle.v2.mvpx, triangle.v2.mvpy, triangle.v2.mvpw);
}

void rasterize_mesh(tinyobj_attrib_t attrib, mat4 model_view_projection, mat3 normal_transfrom, mat4 model_view,
                    framebuffer_4i8 *fb, framebuffer_f *depth_buffer,
                    unsigned char *texture, int tex_width, int tex_height) {
    for (int i = 0; i < (int)attrib.num_face_num_verts; i++) {
        // for (int i = 0; i < 1; i++) {
        tinyobj_vertex_index_t v0_idx = attrib.faces[i * 3];
        tinyobj_vertex_index_t v1_idx = attrib.faces[i * 3 + 1];
        tinyobj_vertex_index_t v2_idx = attrib.faces[i * 3 + 2];

        vec4 v0 = {attrib.vertices[3 * v0_idx.v_idx + 0], attrib.vertices[3 * v0_idx.v_idx + 1], attrib.vertices[3 * v0_idx.v_idx + 2], 1.0f};
        vec4 v1 = {attrib.vertices[3 * v1_idx.v_idx + 0], attrib.vertices[3 * v1_idx.v_idx + 1], attrib.vertices[3 * v1_idx.v_idx + 2], 1.0f};
        vec4 v2 = {attrib.vertices[3 * v2_idx.v_idx + 0], attrib.vertices[3 * v2_idx.v_idx + 1], attrib.vertices[3 * v2_idx.v_idx + 2], 1.0f};

        vec2 t0 = {attrib.texcoords[2 * v0_idx.vt_idx + 0], attrib.texcoords[2 * v0_idx.vt_idx + 1]};
        vec2 t1 = {attrib.texcoords[2 * v1_idx.vt_idx + 0], attrib.texcoords[2 * v1_idx.vt_idx + 1]};
        vec2 t2 = {attrib.texcoords[2 * v2_idx.vt_idx + 0], attrib.texcoords[2 * v2_idx.vt_idx + 1]};

        vec4 n0 = {attrib.normals[3 * v0_idx.vn_idx + 0], attrib.normals[3 * v0_idx.vn_idx + 1], attrib.normals[3 * v0_idx.vn_idx + 2], 0.0f};
        vec4 n1 = {attrib.normals[3 * v1_idx.vn_idx + 0], attrib.normals[3 * v1_idx.vn_idx + 1], attrib.normals[3 * v1_idx.vn_idx + 2], 0.0f};
        vec4 n2 = {attrib.normals[3 * v2_idx.vn_idx + 0], attrib.normals[3 * v2_idx.vn_idx + 1], attrib.normals[3 * v2_idx.vn_idx + 2], 0.0f};

        glm_mat3_mulv(normal_transfrom, n0, n0);
        glm_mat3_mulv(normal_transfrom, n1, n1);
        glm_mat3_mulv(normal_transfrom, n2, n2);
        glm_vec3_normalize(n0);
        glm_vec3_normalize(n1);
        glm_vec3_normalize(n2);

        vec4 mv0, mv1, mv2;
        glm_mat4_mulv(model_view, v0, mv0);
        glm_mat4_mulv(model_view, v1, mv1);
        glm_mat4_mulv(model_view, v2, mv2);
        glm_vec4_scale(mv0, 1.0f / mv0[3], mv0);
        glm_vec4_scale(mv1, 1.0f / mv1[3], mv1);
        glm_vec4_scale(mv2, 1.0f / mv2[3], mv2);

        vec4 mvp0, mvp1, mvp2;
        glm_mat4_mulv(model_view_projection, v0, mvp0);
        glm_mat4_mulv(model_view_projection, v1, mvp1);
        glm_mat4_mulv(model_view_projection, v2, mvp2);

        const vec3 diffuse_color = {0.5, 0.0, 0.0};

        triangle_2d_vertex_t vert0 = {
            .mvpx = mvp0[0],
            .mvpy = mvp0[1],
            .mvpz = mvp0[2],
            .mvpw = mvp0[3],
            .r = diffuse_color[0],
            .g = diffuse_color[1],
            .b = diffuse_color[2],
            .u = t0[0],
            .v = t0[1],
            .nx = n0[0],
            .ny = n0[1],
            .nz = n0[2],
            .mvx = mv0[0],
            .mvy = mv0[1],
            .mvz = mv0[2]};
        triangle_2d_vertex_t vert1 = {
            .mvpx = mvp1[0],
            .mvpy = mvp1[1],
            .mvpz = mvp1[2],
            .mvpw = mvp1[3],
            .r = diffuse_color[0],
            .g = diffuse_color[1],
            .b = diffuse_color[2],
            .u = t1[0],
            .v = t1[1],
            .nx = n1[0],
            .ny = n1[1],
            .nz = n1[2],
            .mvx = mv1[0],
            .mvy = mv1[1],
            .mvz = mv1[2]};
        triangle_2d_vertex_t vert2 = {
            .mvpx = mvp2[0],
            .mvpy = mvp2[1],
            .mvpz = mvp2[2],
            .mvpw = mvp2[3],
            .r = diffuse_color[0],
            .g = diffuse_color[1],
            .b = diffuse_color[2],
            .u = t2[0],
            .v = t2[1],
            .nx = n2[0],
            .ny = n2[1],
            .nz = n2[2],
            .mvx = mv2[0],
            .mvy = mv2[1],
            .mvz = mv2[2]};
        triangle_2d_t triangle = {vert0, vert1, vert2};

        triangle_2d_t clipped_triangle0, clipped_triangle1;
        int num_clipped_triangles = 0;
        clip_triangle(triangle, &clipped_triangle0, &clipped_triangle1, &num_clipped_triangles);
        if (num_clipped_triangles > 0) draw_triangle(fb, depth_buffer, clipped_triangle0, texture, tex_width, tex_height);
        if (num_clipped_triangles > 1) draw_triangle(fb, depth_buffer, clipped_triangle1, texture, tex_width, tex_height);
    }
}
#endif // RENDERING_H