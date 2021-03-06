#include "../test.h"
#include "../../aligned.h"
#include "../../kernels.h"

#include <numeric>

namespace intgemm {

float sigmoid_ref(float x) {
  if (x < 0)
    return exp(x) / (1 + exp(x));
  else
    return 1 / (1 + exp(-x));
}

template <CPUType CPUType_>
void kernel_sigmoid_test() {
  if (kCPU < CPUType_)
    return;

  using vec_t = vector_t<CPUType_, float>;
  constexpr static int VECTOR_LENGTH = sizeof(vec_t) / sizeof(float);

  AlignedVector<float> input(VECTOR_LENGTH);
  AlignedVector<float> output(VECTOR_LENGTH);

  std::iota(input.begin(), input.end(), -int(VECTOR_LENGTH / 2));

  *output.template as<vec_t>() = kernels::sigmoid(*input.template as<vec_t>());
  for (std::size_t i = 0; i < output.size(); ++i)
    CHECK_EPS(output[i], sigmoid_ref(input[i]), 0.001f);
}

template INTGEMM_AVX2 void kernel_sigmoid_test<CPUType::AVX2>();
KERNEL_TEST_CASE("sigmoid AVX2") { return kernel_sigmoid_test<CPUType::AVX2>(); }

#ifdef INTGEMM_COMPILER_SUPPORTS_AVX512BW
template INTGEMM_AVX512BW void kernel_sigmoid_test<CPUType::AVX512BW>();
KERNEL_TEST_CASE("sigmoid AVX512BW") { return kernel_sigmoid_test<CPUType::AVX512BW>(); }
#endif

}
