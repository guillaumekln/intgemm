#include "avx2_gemm.h"

#include <cassert>
#include <emmintrin.h>
#include <immintrin.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tmmintrin.h>
#include <xmmintrin.h>

namespace intgemm {

#ifdef __AVX2__
namespace AVX2 {

namespace {
// Same implementation as AVX512, just shorter
inline __m256i QuantizerGrab(const float *input, const __m256 quant_mult_reg) {
  return _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_load_ps(input), quant_mult_reg));
}

} // namespace

void Quantize16(const float *input, int16_t *output, float quant_mult, std::size_t size) {
  assert(size % 16 == 0);
  assert(reinterpret_cast<uintptr_t>(input) % 32 == 0);
  const __m256 quant_mult_reg = _mm256_set1_ps(quant_mult);
  const float *end = input + size;
  for (; input != end; input += 16, output += 16) {
    __m256i g0 = QuantizerGrab(input, quant_mult_reg);
    __m256i g1 = QuantizerGrab(input + 8, quant_mult_reg);
    __m256i packed = _mm256_packs_epi32(g0, g1);
    // Reorder the packed values because Intel does 0 1 2 3 8 9 10 11 4 5 6 7 12 13 14 15.
    // Technically this could be removed so long as the rows are bigger than 16
    // and the values are only used for GEMM.
    packed = _mm256_permute4x64_epi64(packed, 0xd8 /* 0, 2, 1, 3 */);
    *reinterpret_cast<__m256i*>(output) = packed;
  }
}

void Quantize8(const float *input, int8_t *output, float quant_mult, std::size_t size) {
  assert(size % 32 == 0);
  assert(reinterpret_cast<uintptr_t>(input) % 32 == 0);
  const __m256 quant_mult_reg = _mm256_set1_ps(quant_mult);
  const __m256i neg127 = _mm256_set1_epi8(-127);
  const __m256i shuffle_param = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);
  const float *end = input + size;
  for (; input != end; input += 32, output += 32) {
    // Grab 4 registers at a time in 32-bit format.
    __m256i g0 = QuantizerGrab(input, quant_mult_reg);
    __m256i g1 = QuantizerGrab(input + 8, quant_mult_reg);
    __m256i g2 = QuantizerGrab(input + 16, quant_mult_reg);
    __m256i g3 = QuantizerGrab(input + 24, quant_mult_reg);
    // Pack 32-bit to 16-bit.
    __m256i packed0 = _mm256_packs_epi32(g0, g1);
    __m256i packed1 = _mm256_packs_epi32(g2, g3);
    // Pack 16-bit to 8-bit.
    __m256i packed = _mm256_packs_epi16(packed0, packed1);
    // Ban -128.
    packed = _mm256_max_epi8(packed, neg127);
    // Currently in 0 1 2 3 8 9 10 11 16 17 18 19 24 25 26 27 4 5 6 7 12 13 14 15 20 21 22 23 28 29 30 31
    // Or as 32-bit integers 0 2 4 6 1 3 5 7
    // Technically this could be removed so long as the rows are bigger than 16
    // and the values are only used for GEMM.
    packed = _mm256_permutevar8x32_epi32(packed, shuffle_param);
    *reinterpret_cast<__m256i*>(output) = packed;
  }
}

namespace {

/* Again just a shorter version of AVX512.  TODO: test shift and friends.  Or _mm256_hadds_epi16 */
inline void Convert32Sum(__m256i &sum) {
  sum = _mm256_madd_epi16(sum, _mm256_set1_epi16(1) /* Empirically gcc is smart enough to pull this out */);
}

// Assuming sum1, sum2, sum3, and sum4 are arrays 32-bit signed integers,
// reduce within each.
// Returns [sum(sum1), sum(sum2), sum(sum3), sum(sum4)]
// TODO: consider doing in 64-bit, allowing 4 more bits of quantization?
// TODO: 8-way version?
inline __m128i Reduce32(__m256i sum1, __m256i sum2, __m256i sum3, __m256i sum4) {
  // 1 2 1 2 1 2 1 2
  __m256i pack12 = _mm256_add_epi32(_mm256_unpackhi_epi32(sum1, sum2), _mm256_unpacklo_epi32(sum1, sum2));
  // 3 4 3 4 3 4 3 4
  __m256i pack34 = _mm256_add_epi32(_mm256_unpackhi_epi32(sum3, sum4), _mm256_unpacklo_epi32(sum3, sum4));
  // 1 2 3 4 1 2 3 4
  __m256i pack1234 = _mm256_add_epi32(_mm256_unpackhi_epi64(pack12, pack34), _mm256_unpacklo_epi64(pack12, pack34));
  // Cut the register into halves and sum those.  1 2 3 4
  return _mm_add_epi32(_mm256_castsi256_si128(pack1234), _mm256_extracti128_si256(pack1234, 1));
}

inline __m128i Reduce16to32(__m256i sum1, __m256i sum2, __m256i sum3, __m256i sum4) {
  Convert32Sum(sum1);
  Convert32Sum(sum2);
  Convert32Sum(sum3);
  Convert32Sum(sum4);
  return Reduce32(sum1, sum2, sum3, sum4);
}

} // namespace

// This is an AVX2 implementation of int16_t multiply based on Jacob
// Devlin's SSE code.  The original SSE code was:

// Copyright (c) 2017 Microsoft Corporation

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


// We are multiplying A * B^T, as opposed to A * B. This is important because it means we can do consecutive memory access on A * B^T which allows to to take the most
// advantage of L1 cache.
// 
// B is typically a weight matrix, so it can be pre-processed offline, and therefore this transpose does not cost anything.
// A is typically an activation minibatch matrix.
// A and B must be 32-byte aligned.
// C should be the usual 4-byte alignment.
void MatrixMult16(const __m256i * A, const __m256i * B, float * C, float unquant_mult, int num_A_rows, int num_B_rows, int width) {
  assert(width % 16 == 0);
  assert(num_B_rows % 8 == 0);
  assert(reinterpret_cast<uintptr_t>(A) % 32 == 0);
  assert(reinterpret_cast<uintptr_t>(B) % 32 == 0);
  assert(reinterpret_cast<uintptr_t>(C) % 32 == 0);
  const int simd_width = width / 16;
  const __m256 unquant_reg = _mm256_set1_ps(unquant_mult);
  const __m256i *B0_row = B;
  for (int B0_rowidx = 0; B0_rowidx < num_B_rows; B0_row += 8 * simd_width, B0_rowidx += 8) {
    const __m256i *B1_row = B0_row + simd_width * 1;
    const __m256i *B2_row = B0_row + simd_width * 2;
    const __m256i *B3_row = B0_row + simd_width * 3;
    const __m256i *B4_row = B0_row + simd_width * 4;
    const __m256i *B5_row = B0_row + simd_width * 5;
    const __m256i *B6_row = B0_row + simd_width * 6;
    const __m256i *B7_row = B0_row + simd_width * 7;
    // Process one row of A at a time.  Doesn't seem to be faster to do multiple rows of A at once.
    for (int A_rowidx = 0; A_rowidx < num_A_rows; ++A_rowidx) {
      const __m256i *A_row = A + A_rowidx * simd_width;
      // These will be packed 16-bit integers containing sums for each row of B multiplied by the row of A.
      __m256i sum0 = _mm256_setzero_si256();
      __m256i sum1 = _mm256_setzero_si256();
      __m256i sum2 = _mm256_setzero_si256();
      __m256i sum3 = _mm256_setzero_si256();
      __m256i sum4 = _mm256_setzero_si256();
      __m256i sum5 = _mm256_setzero_si256();
      __m256i sum6 = _mm256_setzero_si256();
      __m256i sum7 = _mm256_setzero_si256();
      // Iterate over shared (inner) dimension.
      for (int k = 0; k < simd_width; ++k) {
        // These do the loads from B which is important to do early to hide as
        // much memory latency as possible.
        // It's possible to rearrange B so that these will all be consecutive
        // and benchmarks show that is faster.  TODO.
        __m256i a = *(A_row + k);
        __m256i b0 = *(B0_row + k);
        __m256i b1 = *(B1_row + k);
        __m256i b2 = *(B2_row + k);
        __m256i b3 = *(B3_row + k);
        __m256i b4 = *(B4_row + k);
        __m256i b5 = *(B5_row + k);
        __m256i b6 = *(B6_row + k);
        __m256i b7 = *(B7_row + k);
        // Multiply 8-bit unsigned * signed, horizontally add to packed 16-bit integers.
        __m256i mult0 = _mm256_madd_epi16(a, b0);
        __m256i mult1 = _mm256_madd_epi16(a, b1);
        __m256i mult2 = _mm256_madd_epi16(a, b2);
        __m256i mult3 = _mm256_madd_epi16(a, b3);
        __m256i mult4 = _mm256_madd_epi16(a, b4);
        __m256i mult5 = _mm256_madd_epi16(a, b5);
        __m256i mult6 = _mm256_madd_epi16(a, b6);
        __m256i mult7 = _mm256_madd_epi16(a, b7);
        // Sum packed 32-bit integers with danger of overflow.  TODO: accumulate in 64-bit every so often.
        sum0 = _mm256_add_epi32(mult0, sum0);
        sum1 = _mm256_add_epi32(mult1, sum1);
        sum2 = _mm256_add_epi32(mult2, sum2);
        sum3 = _mm256_add_epi32(mult3, sum3);
        sum4 = _mm256_add_epi32(mult4, sum4);
        sum5 = _mm256_add_epi32(mult5, sum5);
        sum6 = _mm256_add_epi32(mult6, sum6);
        sum7 = _mm256_add_epi32(mult7, sum7);
      }
      // Write to C.  TODO: optimize shuffling by pushing into Reduce function.
      __m256i combined = _mm256_insertf128_si256(_mm256_castsi128_si256(Reduce32(sum0, sum1, sum2, sum3)), Reduce32(sum4, sum5, sum6, sum7), 1);
      *reinterpret_cast<__m256*>(C + A_rowidx * num_B_rows + B0_rowidx) = _mm256_mul_ps(_mm256_cvtepi32_ps(combined), unquant_reg);
    }
  }

}


/* Computes C = AB^T where:
 * A is num_A_rows x width in row major storage.
 * B is width x num_B_rows (so B^T has num_B_rows)
 * Results are converted to float, multiplied by unquant_mult, and stored in C.
 */
void MatrixMult8(const __m256i *A, const __m256i *B, float *C, float unquant_mult, int num_A_rows, int num_B_rows, int width) {
  assert(width % 32 == 0);
  assert(reinterpret_cast<uintptr_t>(A) % 32 == 0);
  assert(reinterpret_cast<uintptr_t>(B) % 32 == 0);
  assert(reinterpret_cast<uintptr_t>(C) % 32 == 0);
  assert(num_B_rows % 8 == 0);
  __m256 unquant_reg = _mm256_set1_ps(unquant_mult);
  // This fills with bytes 10000000 which are used to detect negative numbers.
  const int simd_width = width / 32;
  int B0_rowidx = 0;
  // Go over 8 rows of B at a time.  TODO: rearrange B so that these accesses are adjacent (it's faster).
  for (const __m256i *B0_row = B; B0_rowidx != num_B_rows; B0_row += 8 * simd_width, B0_rowidx += 8) {
    const __m256i *B1_row = B0_row + simd_width * 1;
    const __m256i *B2_row = B0_row + simd_width * 2;
    const __m256i *B3_row = B0_row + simd_width * 3;
    const __m256i *B4_row = B0_row + simd_width * 4;
    const __m256i *B5_row = B0_row + simd_width * 5;
    const __m256i *B6_row = B0_row + simd_width * 6;
    const __m256i *B7_row = B0_row + simd_width * 7;
    // Process one row of A at a time.  Doesn't seem to be faster to do multiple rows of A at once.
    for (int A_rowidx = 0; A_rowidx < num_A_rows; ++A_rowidx) {
      const __m256i *A_row = A + A_rowidx * simd_width;
      // These will be packed 16-bit integers containing sums for each row of B multiplied by the row of A.
      __m256i sum0 = _mm256_setzero_si256();
      __m256i sum1 = _mm256_setzero_si256();
      __m256i sum2 = _mm256_setzero_si256();
      __m256i sum3 = _mm256_setzero_si256();
      __m256i sum4 = _mm256_setzero_si256();
      __m256i sum5 = _mm256_setzero_si256();
      __m256i sum6 = _mm256_setzero_si256();
      __m256i sum7 = _mm256_setzero_si256();
      // Iterate over shared (inner) dimension.
      for (int k = 0; k < simd_width; ++k) {
        // Read in 64 8-bit signed integers from A.
        __m256i a = *(A_row + k);
        /* These do the loads from B which is important to do early to hide as
         * much memory latency as possible.
         * It's possible to rearrange B so that these will all be consecutive
         * and benchmarks show that is faster.  TODO.
         * Annoyingly the only 8-bit multiply is signed * unsigned (maddubs).
         * So we take the sign bits off of a and apply them each b in a * b.
         * There is a 256-bit sign instruction so we'll try that.
         */
        __m256i a_positive = _mm256_abs_epi8(a);
        // Negate 8-bit values in b if the corresponding a was negative.
        // Negation is implemented by subtraction from zero.
        __m256i b0 = _mm256_sign_epi8(*(B0_row + k), a);
        __m256i b1 = _mm256_sign_epi8(*(B1_row + k), a);
        __m256i b2 = _mm256_sign_epi8(*(B2_row + k), a);
        __m256i b3 = _mm256_sign_epi8(*(B3_row + k), a);
        __m256i b4 = _mm256_sign_epi8(*(B4_row + k), a);
        __m256i b5 = _mm256_sign_epi8(*(B5_row + k), a);
        __m256i b6 = _mm256_sign_epi8(*(B6_row + k), a);
        __m256i b7 = _mm256_sign_epi8(*(B7_row + k), a);
        // Multiply 8-bit unsigned * signed, horizontally add to packed 16-bit integers.
        __m256i mult0 = _mm256_maddubs_epi16(a_positive, b0);
        __m256i mult1 = _mm256_maddubs_epi16(a_positive, b1);
        __m256i mult2 = _mm256_maddubs_epi16(a_positive, b2);
        __m256i mult3 = _mm256_maddubs_epi16(a_positive, b3);
        __m256i mult4 = _mm256_maddubs_epi16(a_positive, b4);
        __m256i mult5 = _mm256_maddubs_epi16(a_positive, b5);
        __m256i mult6 = _mm256_maddubs_epi16(a_positive, b6);
        __m256i mult7 = _mm256_maddubs_epi16(a_positive, b7);
        // Sum packed 16-bit integers with saturation.
        // With larger matrices there is a danger of saturating so TODO upcast to 32-bit every so often.
        sum0 = _mm256_adds_epi16(mult0, sum0);
        sum1 = _mm256_adds_epi16(mult1, sum1);
        sum2 = _mm256_adds_epi16(mult2, sum2);
        sum3 = _mm256_adds_epi16(mult3, sum3);
        sum4 = _mm256_adds_epi16(mult4, sum4);
        sum5 = _mm256_adds_epi16(mult5, sum5);
        sum6 = _mm256_adds_epi16(mult6, sum6);
        sum7 = _mm256_adds_epi16(mult7, sum7);
      }
      // Write to C.  TODO: optimize shuffling by pushing into Reduce function.
      __m256i combined = _mm256_insertf128_si256(_mm256_castsi128_si256(Reduce16to32(sum0, sum1, sum2, sum3)), Reduce16to32(sum4, sum5, sum6, sum7), 1);
      *reinterpret_cast<__m256*>(C + A_rowidx * num_B_rows + B0_rowidx) = _mm256_mul_ps(_mm256_cvtepi32_ps(combined), unquant_reg);
    }
  }
}

} // namespace AVX2
#endif // __AVX2__
} // namespace intgemm