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
	float diffuse[3];
	//float ambient[3];
	//float specular[3];
	//float transmittance[3];
	//float emission[3];
	//float shininess;
	//float ior;      /* index of refraction */
	//float dissolve; /* 1 == opaque; 0 == fully transparent */
	texture_t diffuse_texture;            /* map_Kd */
	//char* ambient_texname;            /* map_Ka */
	//char* specular_texname;           /* map_Ks */
	//char* specular_highlight_texname; /* map_Ns */
	//char* bump_texname;               /* map_bump, bump */
	//char* displacement_texname;       /* disp */
	//char* alpha_texname;              /* map_d */
}material_t;

typedef struct {
	tinyobj_attrib_t attrib;
	tinyobj_shape_t* shapes;
	size_t num_shapes;
	material_t* materials;
	size_t num_materials;
	size_t start_triangle_index;
	size_t end_triangle_index;
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
		printf("  Diffuse: [%f, %f, %f]\n", materials[i].diffuse[0], materials[i].diffuse[1], materials[i].diffuse[2]);
		//printf("  Ambient: [%f, %f, %f]\n", materials[i].ambient[0], materials[i].ambient[1], materials[i].ambient[2]);
		//printf("  Specular: [%f, %f, %f]\n", materials[i].specular[0], materials[i].specular[1], materials[i].specular[2]);
		//printf("  Transmittance: [%f, %f, %f]\n", materials[i].transmittance[0], materials[i].transmittance[1], materials[i].transmittance[2]);
		//printf("  Emission: [%f, %f, %f]\n", materials[i].emission[0], materials[i].emission[1], materials[i].emission[2]);
		//printf("  Shininess: %f\n", materials[i].shininess);
		//printf("  IOR (Index of Refraction): %f\n", materials[i].ior);
		//printf("  Dissolve (Opacity): %f\n", materials[i].dissolve);

		if (materials[i].diffuse_texture.data) {
			printf("  Diffuse Texture: Width: %d, Height:%d\n", materials[i].diffuse_texture.width, materials[i].diffuse_texture.height);
		}
		else {
			printf("  Diffuse Texture: NULL\n");
		}
		//printf("\n");
	}
}

int load_materials(tinyobj_material_t* materials, size_t num_materials, material_t* loaded_materials, char* texture_dir_path) {
	for (int i = 0;i < (int)num_materials;i++) {
		loaded_materials[i].diffuse[0] = materials[i].diffuse[0];
		loaded_materials[i].diffuse[1] = materials[i].diffuse[1];
		loaded_materials[i].diffuse[2] = materials[i].diffuse[2];
		//loaded_materials[i].ambient[0] = materials[i].ambient[0];
		//loaded_materials[i].ambient[1] = materials[i].ambient[1];
		//loaded_materials[i].ambient[2] = materials[i].ambient[2];
		//loaded_materials[i].specular[0] = materials[i].specular[0];
		//loaded_materials[i].specular[1] = materials[i].specular[1];
		//loaded_materials[i].specular[2] = materials[i].specular[2];
		//loaded_materials[i].transmittance[0] = materials[i].transmittance[0];
		//loaded_materials[i].transmittance[1] = materials[i].transmittance[1];
		//loaded_materials[i].transmittance[2] = materials[i].transmittance[2];

		loaded_materials[i].diffuse_texture.data = NULL;
		if (materials[i].diffuse_texname != NULL) {
			char texture_path[128];
			strcpy(texture_path, texture_dir_path);
			strcat(texture_path, materials[i].diffuse_texname);
			loaded_materials[i].diffuse_texture.data = stbi_load(texture_path, 
				&(loaded_materials[i].diffuse_texture.width), 
				&(loaded_materials[i].diffuse_texture.height), 
				&(loaded_materials[i].diffuse_texture.color_size), 4);
			if (loaded_materials[i].diffuse_texture.data == NULL) {
				fprintf(stderr, "Failed to load texture image\nFilepath: %s", texture_path);
				return -1;
			}
			//printf("Loaded texture image: %s, Material ID: %d\n", texture_path, i);
		}
		else {
			loaded_materials[i].diffuse_texture.width = 0;
			loaded_materials[i].diffuse_texture.height = 0;
			loaded_materials[i].diffuse_texture.color_size = 0;
		}
	}
	return 0;
}

int load_obj(char* obj_path, mesh_t* mesh) {
	tinyobj_material_t* materials = NULL;
	int ret = load_mesh(obj_path, &(mesh->attrib), &(mesh->shapes), &(mesh->num_shapes), &materials, &(mesh->num_materials));
	if (ret != 0) {
		fprintf(stderr, "Failed to load mesh: %d\nFilepath: %s", ret, obj_path);
		return 1;
	}
	mesh->start_triangle_index = 0;
	mesh->end_triangle_index = mesh->attrib.num_face_num_verts;
	mesh->materials = (material_t*)calloc(mesh->num_materials, sizeof(material_t));
	char texture_dir_path[128];
	get_dirname(obj_path, texture_dir_path);
	ret = load_materials(materials, mesh->num_materials, mesh->materials, texture_dir_path);
	if (ret != 0) {
		fprintf(stderr, "Failed to load materials: %d\nFilepath: %s", ret, obj_path);
		return 1;
	}
	tinyobj_materials_free(materials, mesh->num_materials);
	return 0;
}

#endif // MESH_LOADING_H