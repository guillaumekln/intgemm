#include "test.h"
#include "../aligned.h"
#include "../avx2_gemm.h"
#include "../avx512_gemm.h"
#include "../sse2_gemm.h"
#include "../ssse3_gemm.h"

#include <cstring>
#include <iostream>
#include <math.h>

namespace intgemm {
namespace {

template <typename Backend>
void PrepareBQuantizedTransposedRef(const typename Backend::Integer* input, typename Backend::Integer* output, Index rows, Index cols) {
  using vec_t = intgemm::vector_t<Backend::kUses, typename Backend::Integer>;
  constexpr Index vec_len = sizeof(vec_t) / sizeof(typename Backend::Integer);

  auto output_it = output;
  for (Index r = 0; r < rows; r += 8)
    for (Index c = 0; c < cols; c += vec_len)
      for (Index ri = 0; ri < 8; ++ri)
        for (Index ci = 0; ci < vec_len; ++ci)
          *output_it++ = input[(r + ri) * cols + c + ci];
}

template <typename Backend>
bool Test(const AlignedVector<typename Backend::Integer>& input, Index rows, Index cols) {
  bool success = true;

  AlignedVector<typename Backend::Integer> output(input.size());
  Backend::PrepareBQuantizedTransposed(input.begin(), output.begin(), cols, rows);

  AlignedVector<typename Backend::Integer> reference(input.size());
  PrepareBQuantizedTransposedRef<Backend>(input.begin(), reference.begin(), rows, cols);

  for (std::size_t i = 0; i < output.size(); ++i) {
    if (output[i] != reference[i]) {
      UNSCOPED_INFO("Error at " << i << ", output = " << int(output[i]) << ", reference = " << int(reference[i]));
      success = false;
      break;
    }
  }
  return success;
}

template <typename Backend>
bool TestMany() {
  const static Index rows = 128;
  const static Index cols = 128;
  AlignedVector<typename Backend::Integer> input(rows * cols);

  std::generate(input.begin(), input.end(), []() {
    static constexpr int divider = sizeof(intgemm::vector_t<Backend::kUses, typename Backend::Integer>) / sizeof(typename Backend::Integer);
    static int value = 0;
    return (value++) % divider;
  });

  return Test<Backend>(input, rows, cols);
}

TEST_CASE("PrepareBQuantizedTransposed SSE2", "") {
  if (kCPU < CPUType::SSE2)
    return;

  CHECK(TestMany<SSE2_16bit>());
}

TEST_CASE("PrepareBQuantizedTransposed SSSE3", "") {
  if (kCPU < CPUType::SSSE3)
    return;

  CHECK(TestMany<SSSE3_8bit>());
}

TEST_CASE("PrepareBQuantizedTransposed AVX2", "") {
  if (kCPU < CPUType::AVX2)
    return;

  CHECK(TestMany<AVX2_8bit>());
  CHECK(TestMany<AVX2_16bit>());
}

#ifdef INTGEMM_COMPILER_SUPPORTS_AVX512
  TEST_CASE("PrepareBQuantizedTransposed AVX512", "") {
    if (kCPU < CPUType::AVX512BW)
      return;

    CHECK(TestMany<AVX512_8bit>());
    CHECK(TestMany<AVX512_16bit>());
  }
#endif

}
}