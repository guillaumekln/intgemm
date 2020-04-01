#if defined(INTGEMM_THIS_IS_AVX512VNNI)
#define INTGEMM_ARCH AVX512VNNI
#define INTGEMM_TARGET INTGEMM_AVX512VNNI
#define INTGEMM_TEST_NAME "AVX512VNNI"
#elif defined(INTGEMM_THIS_IS_AVX512BW)
#define INTGEMM_ARCH AVX512BW
#define INTGEMM_TARGET INTGEMM_AVX512BW
#define INTGEMM_TEST_NAME "AVX512BW"
#elif defined(INTGEMM_THIS_IS_AVX2)
#define INTGEMM_ARCH AVX2
#define INTGEMM_TARGET INTGEMM_AVX2
#define INTGEMM_TEST_NAME "AVX2"
#elif defined(INTGEMM_THIS_IS_SSSE3)
#define INTGEMM_ARCH SSSE3
#define INTGEMM_TARGET INTGEMM_SSSE3
#define INTGEMM_TEST_NAME "SSSE3"
#else
#error "Included without expected architecture"
#endif


namespace intgemm {
namespace INTGEMM_ARCH {

INTGEMM_TARGET void OneIteration() {
  AlignedVector<int8_t> A(1 * sizeof(Register));
  AlignedVector<int8_t> B(sizeof(Register) * 1);
  AlignedVector<int32_t> C(sizeof(Register) / sizeof(int32_t)/* Raw sums */);

  memset(C.begin(), 0, sizeof(Register));

  std::iota(A.begin(), A.end(), 7 /* made up */);
  std::iota(B.begin(), B.end(), 1 /* made up */);

  typedef RowMajorAccess<int8_t> InputA;
  typedef ColMajorAccess<int8_t> InputB;
  typedef RegisterRowMajorAccess Output;
  Access<InputA, InputB, Output> access(
      InputA(A.begin(), sizeof(Register)),
      InputB(B.begin(), sizeof(Register)),
      Output(reinterpret_cast<Register*>(C.begin()), 1));
  MatrixTile<1, 1, Shifted8>::Run(access);

  const std::size_t kStride = sizeof(int32_t) / sizeof(int8_t);
  for (std::size_t i = 0; i < sizeof(Register) / sizeof(int32_t); ++i) {
    int32_t sum = 0;
    for (std::size_t j = i * kStride; j < (i+1) * kStride; ++j) {
      sum += static_cast<int32_t>(A[j]) * static_cast<int32_t>(B[j]);
    }
    CHECK(C[i] == sum);
  }
}

TEST_CASE("Basic Tile " INTGEMM_TEST_NAME, "[tile]") {
  if (kCPU >= CPUType::INTGEMM_ARCH) {
    OneIteration();
  }
}

INTGEMM_TARGET void DumpRegister(Register reg) {
  int32_t values[sizeof(Register) / sizeof(int32_t)];
  memcpy(values, &reg, sizeof(Register));
  for (std::size_t i = 0; i < sizeof(Register) / sizeof(int32_t); ++i) {
    std::cout.width(11);
    std::cout << values[i] << ' ';
  }
}

INTGEMM_TARGET void Pack32Test() {
  const std::size_t kPack = sizeof(Register) / sizeof(int32_t);
  Register regs[kPack];
  std::mt19937 gen;
  //std::uniform_int_distribution<int32_t> dist(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
  std::uniform_int_distribution<int32_t> dist(0, 100);
  std::vector<int32_t> reference(kPack, 0);
  for (std::size_t i = 0; i < kPack; ++i) {
    int32_t temp[kPack];
    for (std::size_t j = 0; j < kPack; ++j) {
      temp[j] = dist(gen);
      reference[j] += temp[j];
    }
    memcpy(&regs[i], temp, sizeof(Register));
  }
  Register *indirect = regs;
  for (std::size_t i = 0; i < 4; ++i) {
    DumpRegister(indirect[i]);
    std::cout << '\n';
  }
  Pack32<3, Sum32Op>(indirect);
  DumpRegister(indirect[0]);
  std::cout << '\n';
}

TEST_CASE("Reduce " INTGEMM_TEST_NAME, "[tile]") {
  if (kCPU >= CPUType::INTGEMM_ARCH)
    Pack32Test();
}

} // namespace INTGEMM_ARCH
} // namespace intgemm

#undef INTGEMM_ARCH
#undef INTGEMM_TARGET
#undef INTGEMM_TEST_NAME
