#ifndef MESH_LOADING_H
#define MESH_LOADING_H

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "tinyobj_loader_c.h"


#ifdef _WIN64
#define atoll(S) _atoi64(S)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "utils.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "textures.h"

typedef struct {
	int32_t diffuse_atlas_offset;
	int texture_width;
	int texture_height;
}material_t;

typedef struct {
	tinyobj_attrib_t attrib;
	tinyobj_shape_t* shapes;
	size_t num_shapes;
	material_t* materials;
	size_t num_materials;
	size_t start_triangle_index;
	size_t end_triangle_index;
	texture_atlas_t diffuse_atlas;
}mesh_t;

static char* mmap_file(size_t* len, const char* filename) {
#ifdef _WIN64
	HANDLE file =
		CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);

	if (file == INVALID_HANDLE_VALUE) { /* E.g. Model may not have materials. */
		return NULL;
	}

	HANDLE fileMapping = CreateFileMapping(file, NULL, PAGE_READONLY, 0, 0, NULL);
	assert(fileMapping != INVALID_HANDLE_VALUE);

	LPVOID fileMapView = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);
	char* fileMapViewChar = (char*)fileMapView;
	assert(fileMapView != NULL);

	DWORD file_size = GetFileSize(file, NULL);
	(*len) = (size_t)file_size;

	return fileMapViewChar;
#else

	struct stat sb;
	char* p;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return NULL;
	}

	if (fstat(fd, &sb) == -1) {
		perror("fstat");
		return NULL;
	}

	if (!S_ISREG(sb.st_mode)) {
		fprintf(stderr, "%s is not a file\n", filename);
		return NULL;
	}

	p = (char*)mmap(0, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);

	if (p == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}

	if (close(fd) == -1) {
		perror("close");
		return NULL;
	}

	(*len) = sb.st_size;

	return p;

#endif
}

static void get_file_data(void* ctx, const char* filename, const int is_mtl,
	const char* obj_filename, char** data, size_t* len) {
	// NOTE: If you allocate the buffer with malloc(),
	// You can define your own memory management struct and pass it through `ctx`
	// to store the pointer and free memories at clean up stage(when you quit an
	// app)
	// This example uses mmap(), so no free() required.
	(void)ctx;

	if (!filename) {
		fprintf(stderr, "null filename\n");
		(*data) = NULL;
		(*len) = 0;
		return;
	}

	size_t data_len = 0;

	*data = mmap_file(&data_len, filename);
	(*len) = data_len;
}

static int load_mesh(const char* filename, tinyobj_attrib_t* attrib,
	tinyobj_shape_t** shapes, size_t* num_shapes,
	tinyobj_material_t** materials, size_t* num_materials) {

	unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
	int ret = tinyobj_parse_obj(attrib, shapes, num_shapes, materials,
		num_materials, filename, get_file_data, NULL, flags);
	return ret;
}

static char* get_dirname(char* path, char* dir_path) {
	char* last_delim = NULL;

	if (path == NULL) {
		return path;
	}

	strcpy(dir_path, path);
	last_delim = strrchr(dir_path, '/');
	if (last_delim == NULL) {
		return NULL;
	}
	last_delim[1] = '\0';

	return dir_path;
}

void print_material_info(material_t* materials, size_t num_materials) {
	for (size_t i = 0; i < num_materials; i++) {
		printf("Material %zu:\n", i);
		printf("  Diffuse Atlas Offset: %d\n", materials[i].diffuse_atlas_offset);
		printf("  Texture Width: %d\n", materials[i].texture_width);
		printf("  Texture Height: %d\n", materials[i].texture_height);
		//printf("\n");
	}
}

int load_materials(tinyobj_material_t* materials, size_t num_materials, material_t* loaded_materials, char* texture_dir_path, texture_atlas_t* atlas) {
	int atlas_length = 1 + (int)num_materials;
	for (int i = 0;i < (int)num_materials;i++) {
		if (materials[i].diffuse_texname != NULL) {
			int already_loaded_idx = -1;
			for (int j = 0;j < i;j++) {
				if (materials[j].diffuse_texname == NULL)continue;
				if (strcmp(materials[i].diffuse_texname, materials[j].diffuse_texname) == 0) {
					already_loaded_idx = j;
					break;
				}
			}
			if (already_loaded_idx != -1) continue;
			int texture_width, texture_height, texture_color_size;
			char texture_path[128];
			strcpy(texture_path, texture_dir_path);
			strcat(texture_path, materials[i].diffuse_texname);
			stbi_info(texture_path, &texture_width, &texture_height, &texture_color_size);
			if (texture_width <= 0 || texture_height <= 0) {
				fprintf(stderr, "Failed to get texture image info\nFilepath: %s\r\n", texture_path);
				return -1;
			}
			atlas_length += texture_width * texture_height;
			printf("Texture image info: %s, Material ID: %d, Width: %d, Height: %d, Color Size: %d\r\n", texture_path, i, texture_width, texture_height, texture_color_size);
			printf("Calculated atlas length so far: %d\r\n", atlas_length);
		}
	}
	atlas->color_size = 2;
	atlas->length = 0;
	atlas->data = NULL;
	atlas->data = malloc(atlas_length * sizeof(uint16_t));
	if (atlas->data == NULL) {
		fprintf(stderr, "Failed to allocate memory for texture atlas\n");
		return -1;
	}
	
	append_3f32rgb_to_5r6g5b_texture_atlas((float[]) { 1.0f, 0.0f, 1.0f }, atlas); // Reserve the first entry for default color
	for (int i = 0;i < (int)num_materials;i++) {
		loaded_materials[i].diffuse_atlas_offset = atlas->length;
		append_3f32rgb_to_5r6g5b_texture_atlas(materials[i].diffuse, atlas);
		loaded_materials[i].texture_height = 0;
		loaded_materials[i].texture_width =  0;
	}
	for (int i = 0;i < (int)num_materials;i++) {
		if (materials[i].diffuse_texname != NULL) {
			int already_loaded_idx = -1;
			for (int j = 0;j < i;j++) {
				if (materials[j].diffuse_texname == NULL)continue;
				if (strcmp(materials[i].diffuse_texname, materials[j].diffuse_texname) == 0) {
					already_loaded_idx = j;
					break;
				}
			}
			if (already_loaded_idx != -1) {
				loaded_materials[i].diffuse_atlas_offset = loaded_materials[already_loaded_idx].diffuse_atlas_offset;
				loaded_materials[i].texture_width = loaded_materials[already_loaded_idx].texture_width;
				loaded_materials[i].texture_height = loaded_materials[already_loaded_idx].texture_height;
				continue;
			}
			texture_t temp_texture;
			char texture_path[128];
			strcpy(texture_path, texture_dir_path);
			strcat(texture_path, materials[i].diffuse_texname);
			temp_texture.data = (unsigned char*)stbi_load(texture_path, &temp_texture.width, &temp_texture.height, &temp_texture.color_size, 4);
			if (temp_texture.data == NULL) {
				fprintf(stderr, "Failed to load texture image\nFilepath: %s\r\n", texture_path);
				return -1;
			}
			loaded_materials[i].diffuse_atlas_offset = atlas->length;
			loaded_materials[i].texture_width = temp_texture.width;
			loaded_materials[i].texture_height = temp_texture.height;
			printf("Loaded texture image: %s, Material ID: %d\r\n", texture_path, i);
			append_8r8g8b8a_texture_to_5r6g5b_texture_atlas(&temp_texture, atlas);
			printf("Converted texture to 5r6g5b format and appended to atlas: %s, Material ID: %d\r\n", texture_path, i);
			stbi_image_free(temp_texture.data);
		}
	}
	return 0;
}

int load_obj(char* obj_path, mesh_t* mesh) {
	tinyobj_material_t* materials = NULL;
	int ret = load_mesh(obj_path, &(mesh->attrib), &(mesh->shapes), &(mesh->num_shapes), &materials, &(mesh->num_materials));
	if (ret != 0) {
		fprintf(stderr, "Failed to load mesh: %d\nFilepath: %s\r\n", ret, obj_path);
		return 1;
	}
	mesh->start_triangle_index = 0;
	mesh->end_triangle_index = mesh->attrib.num_face_num_verts;
	mesh->materials = (material_t*)calloc(mesh->num_materials, sizeof(material_t));
	char texture_dir_path[128];
	get_dirname(obj_path, texture_dir_path);
	ret = load_materials(materials, mesh->num_materials, mesh->materials, texture_dir_path, &mesh->diffuse_atlas);
	if (ret != 0) {
		fprintf(stderr, "Failed to load materials: %d\nFilepath: %s\r\n", ret, obj_path);
		return 1;
	}
	tinyobj_materials_free(materials, mesh->num_materials);
	return 0;
}

#endif // MESH_LOADING_H