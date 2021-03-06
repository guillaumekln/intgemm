#include "../test.h"
#include "../../aligned.h"
#include "../../kernels.h"

#include <numeric>

namespace intgemm {

template <CPUType CPUType_>
void kernel_floor_test() {
  if (kCPU < CPUType_)
    return;

  using vec_t = vector_t<CPUType_, float>;
  constexpr static auto VECTOR_LENGTH = sizeof(vec_t) / sizeof(float);

  AlignedVector<float> input(VECTOR_LENGTH);
  AlignedVector<float> output(VECTOR_LENGTH);

  std::iota(input.begin(), input.end(), -int(VECTOR_LENGTH / 2));

  *output.template as<vec_t>() = kernels::floor(*input.template as<vec_t>());
  for (std::size_t i = 0; i < output.size(); ++i)
    CHECK(output[i] == std::floor(input[i]));
}

template INTGEMM_SSE2 void kernel_floor_test<CPUType::SSE2>();
KERNEL_TEST_CASE("floor SSE2") { return kernel_floor_test<CPUType::SSE2>(); }

template INTGEMM_AVX2 void kernel_floor_test<CPUType::AVX2>();
KERNEL_TEST_CASE("floor AVX2") { return kernel_floor_test<CPUType::AVX2>(); }

#ifdef INTGEMM_COMPILER_SUPPORTS_AVX512BW
template INTGEMM_AVX512BW void kernel_floor_test<CPUType::AVX512BW>();
KERNEL_TEST_CASE("floor AVX512BW") { return kernel_floor_test<CPUType::AVX512BW>(); }
#endif

}
