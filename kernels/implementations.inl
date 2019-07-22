#include "intrinsics.h"
#include "types.h"
#include "utils.h"
#include "vec_traits.h"

#include <cassert>

#if defined(THIS_IS_SSE2)
  #define CPU_NAME SSE2
  #define CPU_ATTR INTGEMM_SSE2
#elif defined(THIS_IS_AVX2)
  #define CPU_NAME AVX2
  #define CPU_ATTR INTGEMM_AVX2
#elif defined(THIS_IS_AVX512BW)
  #define CPU_NAME AVX512BW
  #define CPU_ATTR INTGEMM_AVX512BW
#else
  #error "Only SSE2, AVX2 and AVX512BW are supported"
#endif

#define vi vector_t<CPUType::CPU_NAME, int>
#define vf vector_t<CPUType::CPU_NAME, float>
#define vd vector_t<CPUType::CPU_NAME, double>

/*
 * Kernels implementations....
 */
namespace intgemm {
namespace kernels {

/*
 * Write
 */
CPU_ATTR static inline void write(vi input, int8_t* output, Index offset) {
  *reinterpret_cast<vi*>(output + offset) = input;
}

CPU_ATTR static inline void write(vi input, int16_t* output, Index offset) {
  *reinterpret_cast<vi*>(output + offset) = input;
}

CPU_ATTR static inline void write(vi input, int* output, Index offset) {
  *reinterpret_cast<vi*>(output + offset) = input;
}

CPU_ATTR static inline void write(vf input, float* output, Index offset) {
  *reinterpret_cast<vf*>(output + offset) = input;
}

CPU_ATTR static inline void write(vd input, double* output, Index offset) {
  *reinterpret_cast<vd*>(output + offset) = input;
}

/*
 * Quantize
 */
CPU_ATTR static inline vi quantize(vf input, vf quant_mult) {
  return cvtps_epi32(mul_ps(input, quant_mult));
}

/*
 * Unquantize
 */
CPU_ATTR static inline vf unquantize(vi input, vf unquant_mult) {
  return mul_ps(cvtepi32_ps(input), unquant_mult);
}

/*
 * Add a bias term
 */
CPU_ATTR static inline vi add_bias(vi input, const int8_t* bias_addr, Index bias_offset) {
  auto bias_term = *reinterpret_cast<const vi*>(bias_addr + bias_offset);
  return add_epi8(input, bias_term);
}

CPU_ATTR static inline vi add_bias(vi input, const int16_t* bias_addr, Index bias_offset) {
  auto bias_term = *reinterpret_cast<const vi*>(bias_addr + bias_offset);
  return add_epi16(input, bias_term);
}

CPU_ATTR static inline vi add_bias(vi input, const int* bias_addr, Index bias_offset) {
  auto bias_term = *reinterpret_cast<const vi*>(bias_addr + bias_offset);
  return add_epi32(input, bias_term);
}

CPU_ATTR static inline vf add_bias(vf input, const float* bias_addr, Index bias_offset) {
  auto bias_term = *reinterpret_cast<const vf*>(bias_addr + bias_offset);
  return add_ps(input, bias_term);
}

CPU_ATTR static inline vd add_bias(vd input, const double* bias_addr, Index bias_offset) {
  auto bias_term = *reinterpret_cast<const vd*>(bias_addr + bias_offset);
  return add_pd(input, bias_term);
}

/*
 * ReLU
 */
template <typename Type>
CPU_ATTR static inline vector_t<CPUType::CPU_NAME, Type> relu(vector_t<CPUType::CPU_NAME, Type> input);

template <>
CPU_ATTR inline vi relu<int8_t>(vi input) {
  static const auto vconst_zero = set1_epi8<vi>(0);
#if defined(THIS_IS_SSE2)
  return and_si(input, _mm_cmplt_epi8(vconst_zero, input));
#elif defined(THIS_IS_AVX2)
  return _mm256_max_epi8(input, vconst_zero);
#else
  return _mm512_max_epi8(input, vconst_zero);
#endif
}

template <>
CPU_ATTR inline vi relu<int16_t>(vi input) {
  static const auto vconst_zero = set1_epi16<vi>(0);
  return max_epi16(input, vconst_zero);
}

template <>
CPU_ATTR inline vi relu<int>(vi input) {
  static const auto vconst_zero = set1_epi32<vi>(0);
#if defined(THIS_IS_SSE2)
  return and_si(input, _mm_cmplt_epi32(vconst_zero, input));
#elif defined(THIS_IS_AVX2)
  return _mm256_max_epi32(input, vconst_zero);
#else
  return _mm512_max_epi32(input, vconst_zero);
#endif
}

template <>
CPU_ATTR inline vf relu<float>(vf input) {
  static const auto vconst_zero = setzero_ps<vf>();
  return max_ps(input, vconst_zero);
}

template <>
CPU_ATTR inline vd relu<double>(vd input) {
  static const auto vconst_zero = setzero_pd<vd>();
  return max_pd(input, vconst_zero);
}

/*
 * Multiply (elemwise)
 */
template <typename Type>
CPU_ATTR static inline vector_t<CPUType::CPU_NAME, Type> multiply(vector_t<CPUType::CPU_NAME, Type> a, vector_t<CPUType::CPU_NAME, Type> b);

template <>
CPU_ATTR inline vi multiply<int8_t>(vi a, vi b) {
  auto even = mullo_epi16(a, b);
  auto odd = mullo_epi16(srli_epi16(a, 8), srli_epi16(b, 8));
  return or_si(slli_epi16(odd, 8), srli_epi16(slli_epi16(even, 8), 8));
}

template <>
CPU_ATTR inline vi multiply<int16_t>(vi a, vi b) {
  return mullo_epi16(a, b);
}

template <>
CPU_ATTR inline vi multiply<int>(vi a, vi b) {
#if defined(THIS_IS_SSE2)
  auto even = mul_epu32(a, b);
  auto odd = mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
  return unpacklo_epi32(shuffle_epi32(even, 0x8 /* = 0 0 2 0 */), shuffle_epi32(odd, 0x8 /* = 0 0 2 0 */));
#elif defined(THIS_IS_AVX2)
  return _mm256_mullo_epi32(a, b);
#else
  return _mm512_mullo_epi32(a, b);
#endif
}

template <>
CPU_ATTR inline vf multiply<float>(vf a, vf b) {
  return mul_ps(a, b);
}

template <>
CPU_ATTR inline vd multiply<double>(vd a, vd b) {
  return mul_pd(a, b);
}

/*
 * Floor
 */
CPU_ATTR static inline vf floor(vf input) {
#if defined(THIS_IS_SSE2)
  static const auto vconst_zero = setzero_ps<vf>();
  static const auto vconst_one = set1_ps<vf>(1.f);

  auto result = cvtepi32_ps(cvttps_epi32(input));
  auto negatives = _mm_cmplt_ps(input, vconst_zero);
  auto nonintegers = _mm_cmpneq_ps(input, result);

  return sub_ps(result, and_ps(vconst_one, and_ps(negatives, nonintegers)));
#elif defined(THIS_IS_AVX2)
  return _mm256_floor_ps(input);
#else
  // TODO: It should work but compiler throw the error "incorrect rounding operand"
  // return _mm512_roundscale_round_ps(input, 0, _MM_FROUND_FLOOR);

  static const auto vconst_zero = setzero_ps<vf>();
  static const auto vconst_one = set1_ps<vf>(1.f);

  auto result = cvtepi32_ps(cvttps_epi32(input));
  auto negatives = _mm512_cmp_ps_mask(input, vconst_zero, _CMP_LT_OQ);
  auto nonintegers = _mm512_cmp_ps_mask(input, result, _CMP_NEQ_OQ);

  return _mm512_mask_blend_ps(_mm512_kand(negatives, nonintegers), result, sub_ps(result, vconst_one));
#endif
}

/*
 * Calculate approximation of e^x using Taylor series and lookup table
 */
CPU_ATTR static inline vf exp_approx_taylor(vf x) {
#if defined(THIS_IS_SSE2)
  assert(false && "SSE2 is not supported");
#else
  static constexpr int EXP_MIN = -20;
  static constexpr int EXP_MAX = 20;
  static constexpr float EXP_LOOKUP[EXP_MAX - EXP_MIN + 1] = {
    expi(-20), expi(-19), expi(-18), expi(-17), expi(-16), expi(-15),
    expi(-14), expi(-13), expi(-12), expi(-11), expi(-10), expi(-9),
    expi(-8), expi(-7), expi(-6), expi(-5), expi(-4), expi(-3), expi(-2),
    expi(-1), expi(0), expi(1), expi(2), expi(3), expi(4), expi(5),
    expi(6), expi(7), expi(8), expi(9), expi(10), expi(11), expi(12),
    expi(13), expi(14), expi(15), expi(16), expi(17), expi(18), expi(19),
    expi(20),
  };

  static const vf dividers[] = {
    set1_ps<vf>(1.f / factorial(7)),
    set1_ps<vf>(1.f / factorial(6)),
    set1_ps<vf>(1.f / factorial(5)),
    set1_ps<vf>(1.f / factorial(4)),
    set1_ps<vf>(1.f / factorial(3)),
    set1_ps<vf>(1.f / factorial(2)),
    set1_ps<vf>(1.f / factorial(1)),
  };
  static const auto const_one = set1_ps<vf>(1.f);
  static const auto const_min_x = set1_ps<vf>(EXP_MIN);
  static const auto const_max_x = set1_ps<vf>(EXP_MAX);

  x = max_ps(x, const_min_x);
  x = min_ps(x, const_max_x);

  auto a = floor(x);
  auto xa = sub_ps(x, a);

  auto result = mul_ps(dividers[0], xa);

  result = add_ps(result, dividers[1]);
  result = mul_ps(result, xa);
  result = add_ps(result, dividers[2]);
  result = mul_ps(result, xa);
  result = add_ps(result, dividers[3]);
  result = mul_ps(result, xa);
  result = add_ps(result, dividers[4]);
  result = mul_ps(result, xa);
  result = add_ps(result, dividers[5]);
  result = mul_ps(result, xa);
  result = add_ps(result, dividers[6]);
  result = mul_ps(result, xa);

  result = add_ps(result, const_one);

  auto ea = i32gather_ps<4>(EXP_LOOKUP + EXP_MAX, cvtps_epi32(a));
  return mul_ps(ea, result);
#endif
}

/*
 * Sigmoid
 */
CPU_ATTR static inline vf sigmoid(vf input) {
#if defined(THIS_IS_SSE2)
  assert(false && "SSE2 is not supported"); // TODO: missing exp_approx_taylor for SSE2
#elif defined(THIS_IS_AVX2)
  static const auto vconst_zero = setzero_ps<vf>();
  static const auto vconst_one = set1_ps<vf>(1.f);

  auto x = input;
  auto minus_x = sub_ps(vconst_zero, x);
  auto e_x = exp_approx_taylor(x);
  auto e_minus_x = exp_approx_taylor(minus_x);

  auto sigmoid_case1 = _mm256_rcp_ps(add_ps(vconst_one, e_minus_x));
  auto sigmoid_case2 = mul_ps(e_x, _mm256_rcp_ps(add_ps(vconst_one, e_x)));

  auto nonnegative_x_mask = _mm256_cmp_ps(vconst_zero, x, _CMP_LT_OS);
  return _mm256_blendv_ps(sigmoid_case1, sigmoid_case2, nonnegative_x_mask);
#else
  static const auto vconst_zero = setzero_ps<vf>();
  static const auto vconst_one = set1_ps<vf>(1.f);

  auto x = input;
  auto minus_x = sub_ps(vconst_zero, x);
  auto e_x = exp_approx_taylor(x);
  auto e_minus_x = exp_approx_taylor(minus_x);

  auto sigmoid_case1 = _mm512_rcp14_ps(add_ps(vconst_one, e_minus_x));
  auto sigmoid_case2 = mul_ps(e_x, _mm512_rcp14_ps(add_ps(vconst_one, e_x)));

  auto nonnegative_x_mask = _mm512_cmp_ps_mask(vconst_zero, x, _CMP_LT_OS);
  return _mm512_mask_blend_ps(nonnegative_x_mask, sigmoid_case1, sigmoid_case2);
#endif
}

/*
 * Tanh
 */
CPU_ATTR static inline vf tanh(vf input) {
#if defined(THIS_IS_SSE2)
  assert(false && "SSE2 is not supported"); // TODO: missing exp_approx_taylor for SSE2
#else
  const static auto vconst_zero = setzero_ps<vf>();

  auto e_x = exp_approx_taylor(input);
  auto e_minus_x = exp_approx_taylor(sub_ps(vconst_zero, input));

  return div_ps(sub_ps(e_x, e_minus_x), add_ps(e_x, e_minus_x));
#endif
}

}
}

#undef CPU_NAME
#undef CPU_ATTR
#undef vi
#undef vf
#undef vd
