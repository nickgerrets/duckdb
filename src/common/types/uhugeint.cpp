#include "duckdb/common/types/uhugeint.hpp"
#include "duckdb/common/types/hugeint.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/algorithm.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/windows_undefs.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/operator/cast_operators.hpp"

#include <cmath>
#include <limits>

namespace duckdb {

//===--------------------------------------------------------------------===//
// String Conversion
//===--------------------------------------------------------------------===//
const uhugeint_t Uhugeint::POWERS_OF_TEN[] {
    uhugeint_t(1),
    uhugeint_t(10),
    uhugeint_t(100),
    uhugeint_t(1000),
    uhugeint_t(10000),
    uhugeint_t(100000),
    uhugeint_t(1000000),
    uhugeint_t(10000000),
    uhugeint_t(100000000),
    uhugeint_t(1000000000),
    uhugeint_t(10000000000),
    uhugeint_t(100000000000),
    uhugeint_t(1000000000000),
    uhugeint_t(10000000000000),
    uhugeint_t(100000000000000),
    uhugeint_t(1000000000000000),
    uhugeint_t(10000000000000000),
    uhugeint_t(100000000000000000),
    uhugeint_t(1000000000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(10),
    uhugeint_t(1000000000000000000) * uhugeint_t(100),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000),
    uhugeint_t(1000000000000000000) * uhugeint_t(10000),
    uhugeint_t(1000000000000000000) * uhugeint_t(100000),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(10000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(100000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(10000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(100000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(10000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(100000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(10000000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(100000000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000000000000000000),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000000000000000000) * uhugeint_t(10),
    uhugeint_t(1000000000000000000) * uhugeint_t(1000000000000000000) * uhugeint_t(100)};

string Uhugeint::ToString(uhugeint_t input) {
	uhugeint_t remainder;
	string result;
	while (true) {
		if (!input.lower && !input.upper) {
			break;
		}
		input = Uhugeint::DivMod(input, 10, remainder);
		result = string(1, '0' + remainder.lower) + result; // NOLINT
	}
	if (result.empty()) {
		// value is zero
		return "0";
	}
	return result;
}

//===--------------------------------------------------------------------===//
// Multiply
//===--------------------------------------------------------------------===//
bool Uhugeint::TryMultiply(uhugeint_t lhs, uhugeint_t rhs, uhugeint_t &result) {
#if ((__GNUC__ >= 5) || defined(__clang__)) && defined(__SIZEOF_INT128__)
	__uint128_t left = __uint128_t(lhs.lower) + (__uint128_t(lhs.upper) << 64);
	__uint128_t right = __uint128_t(rhs.lower) + (__uint128_t(rhs.upper) << 64);
	__uint128_t result_u128;
	if (__builtin_mul_overflow(left, right, &result_u128)) {
		return false;
	}
	uint64_t upper = uint64_t(result_u128 >> 64);
	if (upper & 0x8000000000000000) { // overflow check?
		return false;
	}
	result.upper = uint64_t(upper);
	result.lower = uint64_t(result_u128 & 0xffffffffffffffff);
#else
	// TODO: implement
	return false;
#endif
	return true;
}

uhugeint_t Uhugeint::Multiply(uhugeint_t lhs, uhugeint_t rhs) {
	uhugeint_t result;
	if (!TryMultiply(lhs, rhs, result)) {
		throw OutOfRangeException("Overflow in UHUGEINT multiplication!");
	}
	return result;
}

//===--------------------------------------------------------------------===//
// Divide
//===--------------------------------------------------------------------===//
uint8_t Bits(uhugeint_t x) {
	uint8_t out = 0;
	if (x.upper) {
		out = 64;
		for (uint64_t upper = x.upper; upper; upper >>= 1) {
			++out;
		}
	} else {
		for (uint64_t lower = x.lower; lower; lower >>= 1) {
			++out;
		}
	}
	return out;
}

uhugeint_t Uhugeint::DivMod(uhugeint_t lhs, uhugeint_t rhs, uhugeint_t &remainder) {
	// division by zero not allowed
	D_ASSERT(!(rhs.upper == 0 && rhs.lower == 0));

	remainder = uhugeint_t(0);
	if (rhs == uhugeint_t(1)) {
		return lhs;
	} else if (lhs == rhs) {
		return uhugeint_t(1);
	} else if (lhs == uhugeint_t(0) || lhs < rhs) {
		remainder = lhs;
		return uhugeint_t(0);
	}

	uhugeint_t result = 0;
	for (uint8_t idx = Bits(lhs); idx > 0; --idx) {
		result <<= 1;
		remainder <<= 1;

		if (((lhs >> (idx - 1U)) & 1) != 0) {
			remainder += 1;
		}

		if (remainder >= rhs) {
			remainder -= rhs;
			result += 1;
		}
	}
	return result;
}

uhugeint_t Uhugeint::Divide(uhugeint_t lhs, uhugeint_t rhs) {
	uhugeint_t remainder;
	return Uhugeint::DivMod(lhs, rhs, remainder);
}

uhugeint_t Uhugeint::Modulo(uhugeint_t lhs, uhugeint_t rhs) {
	uhugeint_t remainder;
	Uhugeint::DivMod(lhs, rhs, remainder);
	return remainder;
}

//===--------------------------------------------------------------------===//
// Add/Subtract
//===--------------------------------------------------------------------===//
bool Uhugeint::AddInPlace(uhugeint_t &lhs, uhugeint_t rhs) {
	uint64_t new_upper = lhs.upper + rhs.upper + ((lhs.lower + rhs.lower) < lhs.lower);
	if (new_upper < lhs.upper) {
		return false;
	}
	lhs.upper = new_upper;
	lhs.lower += rhs.lower;
	return true;
}

bool Uhugeint::SubtractInPlace(uhugeint_t &lhs, uhugeint_t rhs) {
	uint64_t new_upper = lhs.upper - rhs.upper - ((lhs.lower - rhs.lower) > lhs.lower);
	if (new_upper > lhs.upper) {
		return false;
	}
	lhs.lower -= rhs.lower;
	return true;
}

uhugeint_t Uhugeint::Add(uhugeint_t lhs, uhugeint_t rhs) {
	if (!AddInPlace(lhs, rhs)) {
		throw OutOfRangeException("Overflow in UHUGEINT addition");
	}
	return lhs;
}

uhugeint_t Uhugeint::Subtract(uhugeint_t lhs, uhugeint_t rhs) {
	if (!SubtractInPlace(lhs, rhs)) {
		throw OutOfRangeException("Underflow in UHUGEINT addition");
	}
	return lhs;
}

//===--------------------------------------------------------------------===//
// Cast/Conversion
//===--------------------------------------------------------------------===//
template <class DST>
bool UhugeintTryCastInteger(uhugeint_t input, DST &result) {
	if (input.upper == 0 && input.lower <= uint64_t(NumericLimits<DST>::Maximum())) {
		result = DST(input.lower);
		return true;
	}
	return false;
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, int8_t &result) {
	return UhugeintTryCastInteger<int8_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, int16_t &result) {
	return UhugeintTryCastInteger<int16_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, int32_t &result) {
	return UhugeintTryCastInteger<int32_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, int64_t &result) {
	return UhugeintTryCastInteger<int64_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, uint8_t &result) {
	return UhugeintTryCastInteger<uint8_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, uint16_t &result) {
	return UhugeintTryCastInteger<uint16_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, uint32_t &result) {
	return UhugeintTryCastInteger<uint32_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, uint64_t &result) {
	return UhugeintTryCastInteger<uint64_t>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, uhugeint_t &result) {
	result = input;
	return true;
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, hugeint_t &result) {
	if (input > uhugeint_t(NumericLimits<hugeint_t>::Maximum())) {
		return false;
	}

	result.lower = input.lower;
	result.upper = input.upper;
	return true;
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, float &result) {
	double dbl_result;
	Uhugeint::TryCast(input, dbl_result);
	result = (float)dbl_result;
	return true;
}

template <class REAL_T>
bool CastUhugeintToFloating(uhugeint_t input, REAL_T &result) {
	result = REAL_T(input.lower) + REAL_T(input.upper) * REAL_T(NumericLimits<uint64_t>::Maximum());
	return true;
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, double &result) {
	return CastUhugeintToFloating<double>(input, result);
}

template <>
bool Uhugeint::TryCast(uhugeint_t input, long double &result) {
	return CastUhugeintToFloating<long double>(input, result);
}

template <class DST>
uhugeint_t UhugeintConvertInteger(DST input) {
	uhugeint_t result;
	result.lower = (uint64_t)input;
	result.upper = 0;
	return result;
}

template <>
bool Uhugeint::TryConvert(const char *value, uhugeint_t &result) {
	auto len = strlen(value);
	string_t string_val(value, len);
	return TryCast::Operation<string_t, uhugeint_t>(string_val, result, true);
}

template <>
bool Uhugeint::TryConvert(int8_t value, uhugeint_t &result) {
	if (value < 0) {
		return false;
	}
	result = UhugeintConvertInteger<int8_t>(value);
	return true;
}

template <>
bool Uhugeint::TryConvert(int16_t value, uhugeint_t &result) {
	if (value < 0) {
		return false;
	}
	result = UhugeintConvertInteger<int16_t>(value);
	return true;
}

template <>
bool Uhugeint::TryConvert(int32_t value, uhugeint_t &result) {
	if (value < 0) {
		return false;
	}
	result = UhugeintConvertInteger<int32_t>(value);
	return true;
}

template <>
bool Uhugeint::TryConvert(int64_t value, uhugeint_t &result) {
	if (value < 0) {
		return false;
	}
	result = UhugeintConvertInteger<int64_t>(value);
	return true;
}
template <>
bool Uhugeint::TryConvert(uint8_t value, uhugeint_t &result) {
	result = UhugeintConvertInteger<uint8_t>(value);
	return true;
}
template <>
bool Uhugeint::TryConvert(uint16_t value, uhugeint_t &result) {
	result = UhugeintConvertInteger<uint16_t>(value);
	return true;
}
template <>
bool Uhugeint::TryConvert(uint32_t value, uhugeint_t &result) {
	result = UhugeintConvertInteger<uint32_t>(value);
	return true;
}
template <>
bool Uhugeint::TryConvert(uint64_t value, uhugeint_t &result) {
	result = UhugeintConvertInteger<uint64_t>(value);
	return true;
}

template <>
bool Uhugeint::TryConvert(uhugeint_t value, uhugeint_t &result) {
	result = value;
	return true;
}

template <>
bool Uhugeint::TryConvert(float value, uhugeint_t &result) {
	return Uhugeint::TryConvert(double(value), result);
}

template <class REAL_T>
bool ConvertFloatingToUhugeint(REAL_T value, uhugeint_t &result) {
	if (!Value::IsFinite<REAL_T>(value)) {
		return false;
	}
	if (value < 0 || value >= 340282366920938463463374607431768211456.0) {
		return false;
	}
	result.lower = (uint64_t)fmod(value, REAL_T(NumericLimits<uint64_t>::Maximum()));
	result.upper = (uint64_t)(value / REAL_T(NumericLimits<uint64_t>::Maximum()));
	return true;
}

template <>
bool Uhugeint::TryConvert(double value, uhugeint_t &result) {
	return ConvertFloatingToUhugeint<double>(value, result);
}

template <>
bool Uhugeint::TryConvert(long double value, uhugeint_t &result) {
	return ConvertFloatingToUhugeint<long double>(value, result);
}

//===--------------------------------------------------------------------===//
// uhugeint_t operators
//===--------------------------------------------------------------------===//
uhugeint_t::uhugeint_t(uint64_t value) {
	this->lower = value;
	this->upper = 0;
}

uhugeint_t::uhugeint_t(const hugeint_t &value) {
	this->lower = value.lower;
	this->upper = value.upper;
}

bool uhugeint_t::operator==(const uhugeint_t &rhs) const {
	return Uhugeint::Equals(*this, rhs);
}

bool uhugeint_t::operator!=(const uhugeint_t &rhs) const {
	return Uhugeint::NotEquals(*this, rhs);
}

bool uhugeint_t::operator<(const uhugeint_t &rhs) const {
	return Uhugeint::LessThan(*this, rhs);
}

bool uhugeint_t::operator<=(const uhugeint_t &rhs) const {
	return Uhugeint::LessThanEquals(*this, rhs);
}

bool uhugeint_t::operator>(const uhugeint_t &rhs) const {
	return Uhugeint::GreaterThan(*this, rhs);
}

bool uhugeint_t::operator>=(const uhugeint_t &rhs) const {
	return Uhugeint::GreaterThanEquals(*this, rhs);
}

uhugeint_t uhugeint_t::operator+(const uhugeint_t &rhs) const {
	return Uhugeint::Add(*this, rhs);
}

uhugeint_t uhugeint_t::operator-(const uhugeint_t &rhs) const {
	return Uhugeint::Subtract(*this, rhs);
}

uhugeint_t uhugeint_t::operator*(const uhugeint_t &rhs) const {
	return Uhugeint::Multiply(*this, rhs);
}

uhugeint_t uhugeint_t::operator/(const uhugeint_t &rhs) const {
	return Uhugeint::Divide(*this, rhs);
}

uhugeint_t uhugeint_t::operator%(const uhugeint_t &rhs) const {
	return Uhugeint::Modulo(*this, rhs);
}

uhugeint_t uhugeint_t::operator-() const {
	return Uhugeint::Negate(*this);
}

uhugeint_t uhugeint_t::operator>>(const uhugeint_t &rhs) const {
	const uint64_t shift = rhs.lower;
	if (rhs.upper != 0 || shift >= 128) {
		return uhugeint_t(0);
	} else if (shift == 0) {
		return *this;
	} else if (shift == 64) {
		return uhugeint_t(0, upper);
	} else if (shift < 64) {
		return uhugeint_t(upper >> shift, (upper << (64 - shift)) + (lower >> shift));
	} else if ((128 > shift) && (shift > 64)) {
		return uhugeint_t(0, (upper >> (shift - 64)));
	}
	return uhugeint_t(0);
}

uhugeint_t uhugeint_t::operator<<(const uhugeint_t &rhs) const {
	const uint64_t shift = rhs.lower;
	if (rhs.upper != 0 || shift >= 128) {
		return uhugeint_t(0);
	} else if (shift == 0) {
		return *this;
	} else if (shift == 64) {
		return uhugeint_t(lower, 0);
	} else if (shift < 64) {
		return uhugeint_t((upper << shift) + (lower >> (64 - shift)), lower << shift);
	} else if ((128 > shift) && (shift > 64)) {
		return uhugeint_t(lower << (shift - 64), 0);
	}
	return uhugeint_t(0);
}

uhugeint_t uhugeint_t::operator&(const uhugeint_t &rhs) const {
	uhugeint_t result;
	result.lower = lower & rhs.lower;
	result.upper = upper & rhs.upper;
	return result;
}

uhugeint_t uhugeint_t::operator|(const uhugeint_t &rhs) const {
	uhugeint_t result;
	result.lower = lower | rhs.lower;
	result.upper = upper | rhs.upper;
	return result;
}

uhugeint_t uhugeint_t::operator^(const uhugeint_t &rhs) const {
	uhugeint_t result;
	result.lower = lower ^ rhs.lower;
	result.upper = upper ^ rhs.upper;
	return result;
}

uhugeint_t uhugeint_t::operator~() const {
	uhugeint_t result;
	result.lower = ~lower;
	result.upper = ~upper;
	return result;
}

uhugeint_t &uhugeint_t::operator+=(const uhugeint_t &rhs) {
	Uhugeint::AddInPlace(*this, rhs);
	return *this;
}
uhugeint_t &uhugeint_t::operator-=(const uhugeint_t &rhs) {
	Uhugeint::SubtractInPlace(*this, rhs);
	return *this;
}
uhugeint_t &uhugeint_t::operator*=(const uhugeint_t &rhs) {
	*this = Uhugeint::Multiply(*this, rhs);
	return *this;
}
uhugeint_t &uhugeint_t::operator/=(const uhugeint_t &rhs) {
	*this = Uhugeint::Divide(*this, rhs);
	return *this;
}
uhugeint_t &uhugeint_t::operator%=(const uhugeint_t &rhs) {
	*this = Uhugeint::Modulo(*this, rhs);
	return *this;
}
uhugeint_t &uhugeint_t::operator>>=(const uhugeint_t &rhs) {
	*this = *this >> rhs;
	return *this;
}
uhugeint_t &uhugeint_t::operator<<=(const uhugeint_t &rhs) {
	*this = *this << rhs;
	return *this;
}
uhugeint_t &uhugeint_t::operator&=(const uhugeint_t &rhs) {
	lower &= rhs.lower;
	upper &= rhs.upper;
	return *this;
}
uhugeint_t &uhugeint_t::operator|=(const uhugeint_t &rhs) {
	lower |= rhs.lower;
	upper |= rhs.upper;
	return *this;
}
uhugeint_t &uhugeint_t::operator^=(const uhugeint_t &rhs) {
	lower ^= rhs.lower;
	upper ^= rhs.upper;
	return *this;
}

template <class T>
static T NarrowCast(const uhugeint_t &input) {
	// NarrowCast is supposed to truncate (take lower)
	return static_cast<T>(input.lower);
}

uhugeint_t::operator uint8_t() const {
	return NarrowCast<uint8_t>(*this);
}
uhugeint_t::operator uint16_t() const {
	return NarrowCast<uint16_t>(*this);
}
uhugeint_t::operator uint32_t() const {
	return NarrowCast<uint32_t>(*this);
}
uhugeint_t::operator uint64_t() const {
	return NarrowCast<uint64_t>(*this);
}
uhugeint_t::operator int8_t() const {
	return NarrowCast<int8_t>(*this);
}
uhugeint_t::operator int16_t() const {
	return NarrowCast<int16_t>(*this);
}
uhugeint_t::operator int32_t() const {
	return NarrowCast<int32_t>(*this);
}
uhugeint_t::operator int64_t() const {
	return NarrowCast<int64_t>(*this);
}

string uhugeint_t::ToString() const {
	return Uhugeint::ToString(*this);
}

} // namespace duckdb