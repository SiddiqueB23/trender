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
static inline float min_float(float a, float b) {
	return (a < b) ? a : b;
}
static inline float max_float(float a, float b) {
	return (a > b) ? a : b;
}
static inline float clamp_float(float x, float xmin, float xmax) {
	return min_float(max_float(x, xmin), xmax);
}
static inline int modulo_int(int a, int b) {
	return ((a % b) + b) % b;
}
#endif // UTILS_H