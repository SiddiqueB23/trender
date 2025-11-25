#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "tinyobj_loader_c.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char *mmap_file(size_t *len, const char *filename) {

    struct stat sb;
    char *p;
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

    p = (char *)mmap(0, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);

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
}

static void get_file_data(void *ctx, const char *filename, const int is_mtl,
                          const char *obj_filename, char **data, size_t *len) {
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

static int load_mesh(const char *filename, tinyobj_attrib_t *attrib,
                     tinyobj_shape_t **shapes, size_t *num_shapes,
                     tinyobj_material_t **materials, size_t *num_materials) {

    unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
    int ret = tinyobj_parse_obj(attrib, shapes, num_shapes, materials,
                                num_materials, filename, get_file_data, NULL, flags);
    return ret;
}