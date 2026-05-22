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

#if defined(__linux__)
#include <immintrin.h>
static inline __m256i _mm256_rem_epi32(__m256i a, __m256i b) {
	// Convert int32 → float (exact for values within float's 24-bit mantissa)
	__m256 fa = _mm256_cvtepi32_ps(a);
	__m256 fb = _mm256_cvtepi32_ps(b);

	// Divide and truncate toward zero (matches C integer division semantics)
	__m256 quotient = _mm256_div_ps(fa, fb);
	__m256 truncated = _mm256_round_ps(quotient, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);

	// remainder = a - trunc(a/b) * b
	__m256 prod = _mm256_mul_ps(truncated, fb);
	__m256 rem_f = _mm256_sub_ps(fa, prod);

	return _mm256_cvttps_epi32(rem_f);
}

static inline __m256i _mm256_div_epi16(__m256i a, __m256i b) {
	// Split 16 int16s into two groups of 8, convert to int32, divide, and pack back
	
	// Lower 8 int16s → int32s
	__m256i a_lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(a));
	__m256i b_lo = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(b));
	
	// Upper 8 int16s → int32s (extract upper 128 bits first)
	__m256i a_hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(a, 1));
	__m256i b_hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(b, 1));
	
	// Convert to float and divide
	__m256 fa_lo = _mm256_cvtepi32_ps(a_lo);
	__m256 fb_lo = _mm256_cvtepi32_ps(b_lo);
	__m256 fa_hi = _mm256_cvtepi32_ps(a_hi);
	__m256 fb_hi = _mm256_cvtepi32_ps(b_hi);
	
	// Divide and truncate toward zero
	__m256 q_lo = _mm256_round_ps(_mm256_div_ps(fa_lo, fb_lo), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
	__m256 q_hi = _mm256_round_ps(_mm256_div_ps(fa_hi, fb_hi), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
	
	// Convert back to int32
	__m256i res_lo = _mm256_cvttps_epi32(q_lo);
	__m256i res_hi = _mm256_cvttps_epi32(q_hi);
	
	// Pack back to int16 (saturate overflow to int16 range)
	return _mm256_packs_epi32(res_lo, res_hi);
}
#endif

#endif // UTILS_H