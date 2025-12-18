#ifndef UTILS_H
#define UTILS_H
static inline int min_int(int a, int b) {
    return (a < b) ? a : b;
}
static inline int max_int(int a, int b) {
    return (a > b) ? a : b;
}
static inline int clamp_int(int x, int xmin, int xmax) {
    return min_int(max_int(x, xmin), xmax);
}
#endif // UTILS_H