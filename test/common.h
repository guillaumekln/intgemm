#pragma once
#include "3rd_party/catch.hpp"
#define CHECK_MESSAGE(cond, msg) do { INFO(msg); CHECK(cond); } while((void)0, 0)
#define CHECK_FALSE_MESSAGE(cond, msg) do { INFO(msg); CHECK_FALSE(cond); } while((void)0, 0)
#define REQUIRE_MESSAGE(cond, msg) do { INFO(msg); REQUIRE(cond); } while((void)0, 0)
#define REQUIRE_FALSE_MESSAGE(cond, msg) do { INFO(msg); REQUIRE_FALSE(cond); } while((void)0, 0)
#include <sstream>
#include "intgemm.h"
#include "aligned.h"

namespace intgemm {
void SlowRefFloat(const float *A, const float *B, float *C, Index A_rows, Index width, Index B_cols, const float *bias=nullptr);


// Compute A*B slowly from integers.
template <class Integer> void SlowRefInt(const Integer *A, const Integer *B, float *C, float unquant_mult, Index A_rows, Index width, Index B_cols, const float *bias=nullptr);

void Compare(const float *float_ref, const float *int_ref, const float *int_test, std::size_t size, std::string test_info,
 float int_tolerance, float float_tolerance, float MSE_float_tolerance, float MSE_int_tolerance);

} //namespace intgemm
