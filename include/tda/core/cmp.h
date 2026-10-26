#pragma once

#include "tda/core/export.h"

/// @file

/// @defgroup core_cmp core/cmp
/// @ingroup core
/// @brief tda_Cmp and tda_Eq, and ready-made ones for the built-in types
///
/// For each type: tda_cmp_<T> ascending, tda_cmp_desc_<T> the same with its
/// operands swapped, tda_eq_<T> only whether the two are equal.
///
/// Operands arrive as const void *, and a third argument carries the caller's context to
/// the comparators that need one; these ready-made ones ignore it, and a call passes
/// nullptr. There is deliberately no by-value form: pass addresses, as in
/// tda_cmp_i32(&a, &b, nullptr), which is also how a struct's comparator delegates to a
/// field's. The context is why these do not fit qsort, whose comparator takes two.
///
/// Float order is total, unlike the naive (a > b) - (a < b): NaN after every non-NaN,
/// NaN equal to NaN, -0.0 equal to +0.0. Without that, a span holding a NaN has no
/// strict weak ordering, and sorting it leaves the order unspecified (algo/sort).
///
/// @par Example
/// @snippet core/example_cmp.c struct
/// @snippet core/example_cmp.c ready
/// @snippet core/example_cmp.c use
/// @{

/// A comparator: negative if lhs < rhs, zero if equal, positive if lhs > rhs; every
/// ordering algo takes one. 'ctx' is whatever the caller passed beside it, so an order
/// can depend on more than its two operands: a field picked at run time, a table of keys
/// to sort indices by. An op that takes a key passes the elem as 'lhs' and the key as
/// 'rhs', so the two may be of different types.
typedef int (*tda_Cmp)(const void *lhs, const void *rhs, void *ctx);

/// An equality: true when the two are equal, given the caller's 'ctx'. The hash
/// containers take one. As with tda_Cmp, an op that takes a key passes the elem as 'lhs'
/// and the key as 'rhs'.
typedef bool (*tda_Eq)(const void *lhs, const void *rhs, void *ctx);

/// @name comparators
/// @{

/* ========== int ========== */

/// two int8_t, ascending
/// @param lhs the left operand, by address
/// @param rhs the right operand, by address
/// @param ctx unused; there for the shape of tda_Cmp
/// @return negative, zero or positive as lhs orders before, with, or after rhs
/// @bigo{1}
[[nodiscard]] TDA_API
int tda_cmp_i8(const void *lhs, const void *rhs, void *ctx);

/// two int16_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_i16(const void *lhs, const void *rhs, void *ctx);

/// two int32_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_i32(const void *lhs, const void *rhs, void *ctx);

/// two int64_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_i64(const void *lhs, const void *rhs, void *ctx);

/* ========== uint ========== */

/// two uint8_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_u8(const void *lhs, const void *rhs, void *ctx);

/// two uint16_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_u16(const void *lhs, const void *rhs, void *ctx);

/// two uint32_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_u32(const void *lhs, const void *rhs, void *ctx);

/// two uint64_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_u64(const void *lhs, const void *rhs, void *ctx);

/* ========== size ========== */

/// two size_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_size(const void *lhs, const void *rhs, void *ctx);

/* ========== ptrdiff ========== */

/// two ptrdiff_t, ascending
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_ptrdiff(const void *lhs, const void *rhs, void *ctx);

/* ========== floating point ========== */

/// two float, ascending in the total order above
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_f32(const void *lhs, const void *rhs, void *ctx);

/// two double, as tda_cmp_f32
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_f64(const void *lhs, const void *rhs, void *ctx);

/* ========== char ========== */

/// two char, as unsigned char whatever the target's signedness: the order strcmp gives
/// the chars of a string
/// @copydetails tda_cmp_i8
[[nodiscard]] TDA_API
int tda_cmp_char(const void *lhs, const void *rhs, void *ctx);

/* ========== str ========== */

/// two strings, in strcmp order; a null orders before every string, two nulls are equal
/// @param lhs the left operand; a pointer to the const char *, not the string
/// @param rhs the right operand, the same
/// @param ctx unused; there for the shape of tda_Cmp
/// @return negative, zero or positive as lhs orders before, with, or after rhs
/// @bigo{n}; n is the length of the shorter string
[[nodiscard]] TDA_API
int tda_cmp_cstr(const void *lhs, const void *rhs, void *ctx);

/// @}

/// @name descending comparators
/// @{

/* ========== int ========== */

/// tda_cmp_i8 with its operands swapped
/// @param lhs the left operand, by address
/// @param rhs the right operand, by address
/// @param ctx unused; there for the shape of tda_Cmp
/// @return negative, zero or positive as lhs orders before, with, or after rhs
/// @bigo{1}
[[nodiscard]] TDA_API
int tda_cmp_desc_i8(const void *lhs, const void *rhs, void *ctx);

/// tda_cmp_i16 with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_i16(const void *lhs, const void *rhs, void *ctx);

/// tda_cmp_i32 with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_i32(const void *lhs, const void *rhs, void *ctx);

/// tda_cmp_i64 with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_i64(const void *lhs, const void *rhs, void *ctx);

/* ========== uint ========== */

/// tda_cmp_u8 with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_u8(const void *lhs, const void *rhs, void *ctx);

/// tda_cmp_u16 with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_u16(const void *lhs, const void *rhs, void *ctx);

/// tda_cmp_u32 with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_u32(const void *lhs, const void *rhs, void *ctx);

/// tda_cmp_u64 with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_u64(const void *lhs, const void *rhs, void *ctx);

/* ========== size ========== */

/// tda_cmp_size with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_size(const void *lhs, const void *rhs, void *ctx);

/* ========== ptrdiff ========== */

/// tda_cmp_ptrdiff with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_ptrdiff(const void *lhs, const void *rhs, void *ctx);

/* ========== floating point ========== */

/// tda_cmp_f32 with its operands swapped, so NaN sorts first
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_f32(const void *lhs, const void *rhs, void *ctx);

/// tda_cmp_f64 with its operands swapped, so NaN sorts first
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_f64(const void *lhs, const void *rhs, void *ctx);

/* ========== char ========== */

/// tda_cmp_char with its operands swapped
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_char(const void *lhs, const void *rhs, void *ctx);

/* ========== str ========== */

/// tda_cmp_cstr with its operands swapped, so a null orders last
/// @copydetails tda_cmp_desc_i8
[[nodiscard]] TDA_API
int tda_cmp_desc_cstr(const void *lhs, const void *rhs, void *ctx);

/// @}

/// @name equality
/// @{

/* ========== int ========== */

/// two int8_t
/// @param lhs the left operand, by address
/// @param rhs the right operand, by address
/// @param ctx unused; there for the shape of tda_Eq
/// @return whether the two are equal
/// @bigo{1}
[[nodiscard]] TDA_API
bool tda_eq_i8(const void *lhs, const void *rhs, void *ctx);

/// two int16_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_i16(const void *lhs, const void *rhs, void *ctx);

/// two int32_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_i32(const void *lhs, const void *rhs, void *ctx);

/// two int64_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_i64(const void *lhs, const void *rhs, void *ctx);

/* ========== uint ========== */

/// two uint8_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_u8(const void *lhs, const void *rhs, void *ctx);

/// two uint16_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_u16(const void *lhs, const void *rhs, void *ctx);

/// two uint32_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_u32(const void *lhs, const void *rhs, void *ctx);

/// two uint64_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_u64(const void *lhs, const void *rhs, void *ctx);

/* ========== size ========== */

/// two size_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_size(const void *lhs, const void *rhs, void *ctx);

/* ========== ptrdiff ========== */

/// two ptrdiff_t
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_ptrdiff(const void *lhs, const void *rhs, void *ctx);

/* ========== floating point ========== */

/// two float, equal when tda_cmp_f32 calls them equal
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_f32(const void *lhs, const void *rhs, void *ctx);

/// two double, as tda_eq_f32
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_f64(const void *lhs, const void *rhs, void *ctx);

/* ========== char ========== */

/// two char
/// @copydetails tda_eq_i8
[[nodiscard]] TDA_API
bool tda_eq_char(const void *lhs, const void *rhs, void *ctx);

/* ========== str ========== */

/// two strings, equal by contents; two nulls are equal, a null equals nothing else
/// @param lhs the left operand; a pointer to the const char *, not the string
/// @param rhs the right operand, the same
/// @param ctx unused; there for the shape of tda_Eq
/// @return whether the two are equal
/// @bigo{n}; n is the length of the shorter string
[[nodiscard]] TDA_API
bool tda_eq_cstr(const void *lhs, const void *rhs, void *ctx);

/// @}

/// @}
