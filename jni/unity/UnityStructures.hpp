#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Unity {
    struct Color;
    struct Quaternion;
    struct Vector2;
    struct Vector3;
    struct Vector4;

    inline constexpr float Deg2Rad = static_cast<float>(M_PI) / 180.0f;
    inline constexpr float Rad2Deg = 180.0f / static_cast<float>(M_PI);

    inline float Clamp01(float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    inline bool IsFinite(float value) {
        return std::isfinite(value);
    }

    struct Color {
        union {
            struct { float r, g, b, a; };
            float data[4];
        };

        constexpr Color() : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}
        constexpr Color(float r, float g, float b) : r(r), g(g), b(b), a(1.0f) {}
        constexpr Color(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
        explicit Color(Vector4 v);

        static Color HSVToRGB(float h, float s, float v, bool hdr = true);
        static Color Lerp(Color a, Color b, float t);

        Color RGBMultiplied(float multiplier) const { return {r * multiplier, g * multiplier, b * multiplier, a}; }
        Color RGBMultiplied(Color multiplier) const { return {r * multiplier.r, g * multiplier.g, b * multiplier.b, a}; }

        friend bool operator==(const Color& lhs, const Color& rhs) {
            return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
        }

        friend bool operator!=(const Color& lhs, const Color& rhs) {
            return !(lhs == rhs);
        }

        static const Color black;
        static const Color red;
        static const Color green;
        static const Color blue;
        static const Color white;
        static const Color orange;
        static const Color yellow;
        static const Color cyan;
        static const Color magenta;
    };

    struct Color32 {
        union {
            struct { uint8_t r, g, b, a; };
            uint32_t rgba;
        };

        constexpr Color32() : r(0), g(0), b(0), a(255) {}
        constexpr Color32(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
    };

    struct Vector2 {
        union {
            struct { float x, y; };
            float data[2];
        };

        constexpr Vector2() : x(0.0f), y(0.0f) {}
        constexpr Vector2(float x, float y) : x(x), y(y) {}

        float* GetPtr() { return data; }
        const float* GetPtr() const { return data; }

        float& operator[](int index) { return data[index]; }
        const float& operator[](int index) const { return data[index]; }

        static float Angle(Vector2 from, Vector2 to);
        static Vector2 ClampMagnitude(Vector2 vector, float maxLength);
        static float Component(Vector2 a, Vector2 b) { return Dot(a, b) / Magnitude(b); }
        static float Distance(Vector2 a, Vector2 b) { return Magnitude(a - b); }
        static float Dot(Vector2 lhs, Vector2 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }
        static Vector2 FromPolar(float radius, float theta);
        static Vector2 Lerp(Vector2 from, Vector2 to, float t) { return LerpUnclamped(from, to, Clamp01(t)); }
        static Vector2 LerpUnclamped(Vector2 from, Vector2 to, float t) { return (to - from) * t + from; }
        static float Magnitude(Vector2 vector) { return std::sqrt(SqrMagnitude(vector)); }
        static Vector2 Max(Vector2 lhs, Vector2 rhs);
        static Vector2 Min(Vector2 lhs, Vector2 rhs);
        static Vector2 MoveTowards(Vector2 current, Vector2 target, float maxDistanceDelta);
        static Vector2 Normalize(Vector2 vector);
        static void OrthoNormalize(Vector2& normal, Vector2& tangent);
        static Vector2 Perpendicular(Vector2 direction) { return {-direction.y, direction.x}; }
        static Vector2 Project(Vector2 a, Vector2 b);
        static Vector2 Reflect(Vector2 direction, Vector2 normal);
        static Vector2 Reject(Vector2 a, Vector2 b) { return a - Project(a, b); }
        static Vector2 RotateTowards(Vector2 current, Vector2 target, float maxRadiansDelta, float maxMagnitudeDelta);
        static Vector2 Scale(Vector2 a, Vector2 b) { return a * b; }
        static Vector2 Slerp(Vector2 a, Vector2 b, float t);
        static Vector2 SlerpUnclamped(Vector2 a, Vector2 b, float t);
        static Vector2 SmoothDamp(Vector2 current, Vector2 target, Vector2& currentVelocity, float smoothTime, float maxSpeed, float deltaTime);
        static float SqrMagnitude(Vector2 vector) { return vector.x * vector.x + vector.y * vector.y; }
        static void ToPolar(Vector2 vector, float& radius, float& theta);

        void Normalize() { *this = Normalize(*this); }
        void Scale(Vector2 scale) { *this = *this * scale; }
        float magnitude() const { return Magnitude(*this); }
        Vector2 normalized() const { return Normalize(*this); }
        float sqrMagnitude() const { return SqrMagnitude(*this); }

        bool operator==(Vector2 value) const { return x == value.x && y == value.y; }
        bool operator!=(Vector2 value) const { return !(*this == value); }
        Vector2& operator+=(float value) { x += value; y += value; return *this; }
        Vector2& operator-=(float value) { x -= value; y -= value; return *this; }
        Vector2& operator*=(float value) { x *= value; y *= value; return *this; }
        Vector2& operator/=(float value) { float inv = 1.0f / value; x *= inv; y *= inv; return *this; }
        Vector2& operator+=(Vector2 value) { x += value.x; y += value.y; return *this; }
        Vector2& operator-=(Vector2 value) { x -= value.x; y -= value.y; return *this; }
        operator Vector3() const;

        friend Vector2 operator+(Vector2 lhs, float rhs) { return lhs += rhs; }
        friend Vector2 operator-(Vector2 lhs, float rhs) { return lhs -= rhs; }
        friend Vector2 operator*(Vector2 lhs, float rhs) { return lhs *= rhs; }
        friend Vector2 operator/(Vector2 lhs, float rhs) { return lhs /= rhs; }
        friend Vector2 operator+(float lhs, Vector2 rhs) { return rhs + lhs; }
        friend Vector2 operator-(float lhs, Vector2 rhs) { return {lhs - rhs.x, lhs - rhs.y}; }
        friend Vector2 operator*(float lhs, Vector2 rhs) { return rhs * lhs; }
        friend Vector2 operator/(float lhs, Vector2 rhs) { return {lhs / rhs.x, lhs / rhs.y}; }
        friend Vector2 operator+(Vector2 lhs, Vector2 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y}; }
        friend Vector2 operator-(Vector2 lhs, Vector2 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y}; }
        friend Vector2 operator*(Vector2 lhs, Vector2 rhs) { return {lhs.x * rhs.x, lhs.y * rhs.y}; }
        friend Vector2 operator/(Vector2 lhs, Vector2 rhs) { return {lhs.x / rhs.x, lhs.y / rhs.y}; }
        Vector2 operator-() const { return {-x, -y}; }

        static const Vector2 positiveInfinity;
        static const Vector2 negativeInfinity;
        static const Vector2 down;
        static const Vector2 left;
        static const Vector2 one;
        static const Vector2 right;
        static const Vector2 up;
        static const Vector2 zero;
    };

    struct Vector2Int {
        union {
            struct { int x, y; };
            int data[2];
        };

        constexpr Vector2Int() : x(0), y(0) {}
        constexpr Vector2Int(int x, int y) : x(x), y(y) {}
    };

    struct Vector3 {
        union {
            struct { float x, y, z; };
            float data[3];
        };

        constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
        constexpr Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

        operator Vector4() const;
        operator Vector2() const { return {x, y}; }
        operator Vector2&() { return *reinterpret_cast<Vector2*>(this); }

        float* GetPtr() { return data; }
        const float* GetPtr() const { return data; }

        float& operator[](int index) { return data[index]; }
        const float& operator[](int index) const { return data[index]; }

        static float Angle(Vector3 from, Vector3 to);
        static float AngleBetween(Vector3 from, Vector3 to) { return Angle(Normalize(from), Normalize(to)); }
        static Vector3 ClampMagnitude(Vector3 vector, float maxLength);
        static Vector3 Cross(Vector3 lhs, Vector3 rhs);
        static float Component(Vector3 a, Vector3 b) { return Dot(a, b) / Magnitude(b); }
        static float Distance(Vector3 a, Vector3 b) { return Magnitude(a - b); }
        static float Dot(Vector3 lhs, Vector3 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }
        static Vector3 FromSpherical(float radius, float theta, float phi);
        static Vector3 FromString(const std::string& value);
        static Vector3 Lerp(Vector3 from, Vector3 to, float t) { return LerpUnclamped(from, to, Clamp01(t)); }
        static Vector3 LerpUnclamped(Vector3 from, Vector3 to, float t) { return (to - from) * t + from; }
        static float Magnitude(Vector3 vector) { return std::sqrt(SqrMagnitude(vector)); }
        static Vector3 Max(Vector3 lhs, Vector3 rhs);
        static Vector3 Min(Vector3 lhs, Vector3 rhs);
        static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta);
        static Vector3 Normalize(Vector3 vector);
        static float NormalizeAngle(float value, bool is180 = true);
        static Vector3 NormalizeEuler(Vector3 vector, bool is180 = true);
        static Vector3 Orthogonal(Vector3 vector);
        static void OrthoNormalize(Vector3& normal, Vector3& tangent);
        static void OrthoNormalize(Vector3& normal, Vector3& tangent, Vector3& binormal);
        static void OrthoNormalizeFast(Vector3& normal, Vector3& tangent, Vector3& binormal);
        static Vector3 OrthoNormalVectorFast(const Vector3& normal);
        static Vector3 Project(Vector3 vector, Vector3 onNormal);
        static Vector3 ProjectOnPlane(Vector3 vector, Vector3 planeNormal);
        static Vector3 Reflect(Vector3 direction, Vector3 normal);
        static Vector3 RotateTowards(Vector3 current, Vector3 target, float maxRadiansDelta, float maxMagnitudeDelta);
        static Vector3 Scale(Vector3 a, Vector3 b) { return a * b; }
        static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis);
        static Vector3 Slerp(Vector3 a, Vector3 b, float t);
        static Vector3 SlerpUnclamped(Vector3 a, Vector3 b, float t);
        static Vector3 SmoothDamp(Vector3 current, Vector3 target, Vector3& currentVelocity, float smoothTime, float maxSpeed, float deltaTime);
        static float SqrMagnitude(Vector3 vector) { return vector.x * vector.x + vector.y * vector.y + vector.z * vector.z; }
        static void ToSpherical(Vector3 vector, float& radius, float& theta, float& phi);

        void Normalize() { *this = Normalize(*this); }
        void Scale(Vector3 scale) { *this = *this * scale; }
        Vector3 orthogonal() const { return Orthogonal(*this); }
        float magnitude() const { return Magnitude(*this); }
        Vector3 normalized() const { return Normalize(*this); }
        Vector3 normalizedEuler(bool is180 = true) const { return NormalizeEuler(*this, is180); }
        float sqrMagnitude() const { return SqrMagnitude(*this); }

        bool operator==(Vector3 value) const { return x == value.x && y == value.y && z == value.z; }
        bool operator!=(Vector3 value) const { return !(*this == value); }
        Vector3& operator+=(Vector3 value) { x += value.x; y += value.y; z += value.z; return *this; }
        Vector3& operator-=(Vector3 value) { x -= value.x; y -= value.y; z -= value.z; return *this; }
        Vector3& operator*=(float value) { x *= value; y *= value; z *= value; return *this; }
        Vector3& operator/=(float value) { float inv = 1.0f / value; x *= inv; y *= inv; z *= inv; return *this; }
        Vector3& operator*=(int value) { return *this *= static_cast<float>(value); }
        Vector3& operator/=(int value) { return *this /= static_cast<float>(value); }
        Vector3& operator/=(Vector3 value) { x /= value.x; y /= value.y; z /= value.z; return *this; }

        friend Vector3 operator+(Vector3 lhs, Vector3 rhs) { return lhs += rhs; }
        friend Vector3 operator-(Vector3 lhs, Vector3 rhs) { return lhs -= rhs; }
        friend Vector3 operator*(Vector3 lhs, float rhs) { return lhs *= rhs; }
        friend Vector3 operator*(Vector3 lhs, int rhs) { return lhs *= rhs; }
        friend Vector3 operator*(float lhs, Vector3 rhs) { return rhs * lhs; }
        friend Vector3 operator*(int lhs, Vector3 rhs) { return rhs * lhs; }
        friend Vector3 operator*(Vector3 lhs, Vector3 rhs) { return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z}; }
        friend Vector3 operator/(Vector3 lhs, float rhs) { return lhs /= rhs; }
        friend Vector3 operator/(Vector3 lhs, int rhs) { return lhs /= rhs; }
        friend Vector3 operator/(Vector3 lhs, Vector3 rhs) { return lhs /= rhs; }
        Vector3 operator-() const { return {-x, -y, -z}; }

        static constexpr float kEpsilon = 1E-05f;
        static constexpr float kEpsilonNormalSqrt = 1E-15f;
        static const Vector3 positiveInfinity;
        static const Vector3 negativeInfinity;
        static const Vector3 back;
        static const Vector3 down;
        static const Vector3 forward;
        static const Vector3 left;
        static const Vector3 one;
        static const Vector3 right;
        static const Vector3 up;
        static const Vector3 zero;
    };

    struct Vector3Int {
        union {
            struct { int x, y, z; };
            int data[3];
        };

        constexpr Vector3Int() : x(0), y(0), z(0) {}
        constexpr Vector3Int(int x, int y, int z) : x(x), y(y), z(z) {}
    };

    struct Vector4 {
        union {
            struct { float x, y, z, w; };
            float data[4];
        };

        constexpr Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
        constexpr Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
        constexpr Vector4(Vector3 value, float w) : x(value.x), y(value.y), z(value.z), w(w) {}
        explicit Vector4(Color value);

        float* GetPtr() { return data; }
        const float* GetPtr() const { return data; }

        float& operator[](int index) { return data[index]; }
        const float& operator[](int index) const { return data[index]; }

        static bool CompareApproximately(Vector4 a, Vector4 b, float maxDistance = Vector3::kEpsilon) {
            return SqrMagnitude(b - a) <= maxDistance * maxDistance;
        }

        static float Component(Vector4 a, Vector4 b) { return Dot(a, b) / Magnitude(b); }
        static float Distance(Vector4 a, Vector4 b) { return Magnitude(a - b); }
        static float Dot(Vector4 lhs, Vector4 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w; }
        static bool IsFinite(Vector4 value) { return Unity::IsFinite(value.x) && Unity::IsFinite(value.y) && Unity::IsFinite(value.z) && Unity::IsFinite(value.w); }
        static Vector4 Lerp(Vector4 from, Vector4 to, float t) { return LerpUnclamped(from, to, Clamp01(t)); }
        static Vector4 LerpUnclamped(Vector4 from, Vector4 to, float t) { return (to - from) * t + from; }
        static float Magnitude(Vector4 value) { return std::sqrt(Dot(value, value)); }
        static Vector4 Normalize(Vector4 value);
        static Vector4 Project(Vector4 a, Vector4 b) { return b * (Dot(a, b) / Dot(b, b)); }
        static float SqrMagnitude(Vector4 value) { return Dot(value, value); }

        void Normalize() { *this = Normalize(*this); }
        bool operator==(Vector4 value) const { return x == value.x && y == value.y && z == value.z && w == value.w; }
        bool operator!=(Vector4 value) const { return !(*this == value); }
        operator Vector3() const { return {x, y, z}; }

        friend Vector4 operator*(Vector4 lhs, Vector4 rhs) { return {lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w}; }
        friend Vector4 operator*(Vector4 lhs, float rhs) { return {lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs}; }
        friend Vector4 operator+(Vector4 lhs, Vector4 rhs) { return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w}; }
        friend Vector4 operator-(Vector4 lhs, Vector4 rhs) { return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w}; }
        friend Vector4 operator/(Vector4 lhs, float rhs) { float inv = 1.0f / rhs; return {lhs.x * inv, lhs.y * inv, lhs.z * inv, lhs.w * inv}; }
        friend Vector4 operator/(Vector4 lhs, Vector4 rhs) { return {lhs.x / rhs.x, lhs.y / rhs.y, lhs.z / rhs.z, lhs.w / rhs.w}; }
        Vector4 operator-() const { return {-x, -y, -z, -w}; }

        static const Vector4 positiveInfinity;
        static const Vector4 negativeInfinity;
        static const Vector4 zero;
        static const Vector4 one;
    };

    struct Quaternion {
        union {
            struct { float x, y, z, w; };
            float data[4];
        };

        constexpr Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
        explicit Quaternion(const float values[]) : x(values[0]), y(values[1]), z(values[2]), w(values[3]) {}
        constexpr Quaternion(Vector3 vector, float scalar) : x(vector.x), y(vector.y), z(vector.z), w(scalar) {}
        constexpr Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
        Quaternion(float yaw, float pitch, float roll) { *this = FromEuler(yaw, pitch, roll); }

        static Vector3 Up(Quaternion value) { return value * Vector3::up; }
        static Vector3 Down(Quaternion value) { return value * Vector3::down; }
        static Vector3 Left(Quaternion value) { return value * Vector3::left; }
        static Vector3 Right(Quaternion value) { return value * Vector3::right; }
        static Vector3 Forward(Quaternion value) { return value * Vector3::forward; }
        static Vector3 Back(Quaternion value) { return value * Vector3::back; }
        static float Angle(Quaternion a, Quaternion b);
        static Quaternion Conjugate(Quaternion rotation) { return {-rotation.x, -rotation.y, -rotation.z, rotation.w}; }
        static float Dot(Quaternion lhs, Quaternion rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w; }
        static Quaternion FromAngleAxis(float angle, Vector3 axis);
        static Quaternion FromEuler(Vector3 rotation) { return FromEuler(rotation.x, rotation.y, rotation.z); }
        static Quaternion FromEuler(float yaw, float pitch, float roll, bool fromDeg = true);
        static Quaternion FromToRotation(Vector3 fromVector, Vector3 toVector);
        static Quaternion Inverse(Quaternion rotation);
        static Quaternion Lerp(Quaternion a, Quaternion b, float t);
        static Quaternion LerpUnclamped(Quaternion a, Quaternion b, float t);
        static Quaternion LookRotation(Vector3 forward) { return LookRotation(forward, Vector3::up); }
        static Quaternion LookRotation(Vector3 forward, Vector3 upwards);
        static float Norm(Quaternion rotation) { return std::sqrt(Dot(rotation, rotation)); }
        static Quaternion Normalize(Quaternion rotation);
        static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxRadiansDelta);
        static Quaternion Slerp(Quaternion a, Quaternion b, float t);
        static Quaternion SlerpUnclamped(Quaternion a, Quaternion b, float t);
        static void ToAngleAxis(Quaternion rotation, float& angle, Vector3& axis);
        static Vector3 ToEuler(Quaternion value, bool toDeg = true);
        static Vector3 RotateVectorByQuaternion(Quaternion lhs, Vector3 rhs);

        Vector3 eulerAngles() const { return ToEuler(*this); }
        Quaternion normalized() const { return Normalize(*this); }

        Quaternion& operator+=(Quaternion value) { x += value.x; y += value.y; z += value.z; w += value.w; return *this; }
        Quaternion& operator-=(Quaternion value) { x -= value.x; y -= value.y; z -= value.z; w -= value.w; return *this; }
        Quaternion& operator*=(Quaternion value);
        Quaternion& operator*=(float value) { x *= value; y *= value; z *= value; w *= value; return *this; }
        Quaternion& operator/=(Quaternion value);
        Quaternion& operator/=(float value) { x /= value; y /= value; z /= value; w /= value; return *this; }
        Quaternion operator-() const { return {-x, -y, -z, -w}; }
        Quaternion operator*(float value) const { return {x * value, y * value, z * value, w * value}; }

        friend Quaternion operator+(Quaternion lhs, Quaternion rhs) { return lhs += rhs; }
        friend Quaternion operator-(Quaternion lhs, Quaternion rhs) { return lhs -= rhs; }
        friend Quaternion operator*(Quaternion lhs, Quaternion rhs) { return lhs *= rhs; }
        friend Quaternion operator*(float lhs, Quaternion rhs) { return rhs *= lhs; }
        friend Quaternion operator/(Quaternion lhs, Quaternion rhs) { return lhs /= rhs; }
        friend Quaternion operator/(Quaternion lhs, float rhs) { return lhs /= rhs; }
        friend Vector3 operator*(Quaternion lhs, Vector3 rhs) { return RotateVectorByQuaternion(lhs, rhs); }

        static const Quaternion identity;
    };

    inline const Color Color::black{0.0f, 0.0f, 0.0f, 1.0f};
    inline const Color Color::red{1.0f, 0.0f, 0.0f, 1.0f};
    inline const Color Color::green{0.0f, 1.0f, 0.0f, 1.0f};
    inline const Color Color::blue{0.0f, 0.0f, 1.0f, 1.0f};
    inline const Color Color::white{1.0f, 1.0f, 1.0f, 1.0f};
    inline const Color Color::orange{1.0f, 0.5f, 0.0f, 1.0f};
    inline const Color Color::yellow{1.0f, 0.92156863f, 0.01568628f, 1.0f};
    inline const Color Color::cyan{0.0f, 1.0f, 1.0f, 1.0f};
    inline const Color Color::magenta{1.0f, 0.0f, 1.0f, 1.0f};

    inline const Vector2 Vector2::positiveInfinity{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    inline const Vector2 Vector2::negativeInfinity{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    inline const Vector2 Vector2::down{0.0f, -1.0f};
    inline const Vector2 Vector2::left{-1.0f, 0.0f};
    inline const Vector2 Vector2::one{1.0f, 1.0f};
    inline const Vector2 Vector2::right{1.0f, 0.0f};
    inline const Vector2 Vector2::up{0.0f, 1.0f};
    inline const Vector2 Vector2::zero{0.0f, 0.0f};

    inline const Vector3 Vector3::positiveInfinity{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    inline const Vector3 Vector3::negativeInfinity{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    inline const Vector3 Vector3::back{0.0f, 0.0f, -1.0f};
    inline const Vector3 Vector3::down{0.0f, -1.0f, 0.0f};
    inline const Vector3 Vector3::forward{0.0f, 0.0f, 1.0f};
    inline const Vector3 Vector3::left{-1.0f, 0.0f, 0.0f};
    inline const Vector3 Vector3::one{1.0f, 1.0f, 1.0f};
    inline const Vector3 Vector3::right{1.0f, 0.0f, 0.0f};
    inline const Vector3 Vector3::up{0.0f, 1.0f, 0.0f};
    inline const Vector3 Vector3::zero{0.0f, 0.0f, 0.0f};

    inline const Vector4 Vector4::positiveInfinity{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
    inline const Vector4 Vector4::negativeInfinity{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    inline const Vector4 Vector4::zero{0.0f, 0.0f, 0.0f, 0.0f};
    inline const Vector4 Vector4::one{1.0f, 1.0f, 1.0f, 1.0f};

    inline const Quaternion Quaternion::identity{0.0f, 0.0f, 0.0f, 1.0f};

    inline Color::Color(Vector4 value) : r(value.x), g(value.y), b(value.z), a(value.w) {}

    inline Color Color::HSVToRGB(float h, float s, float v, bool hdr) {
        if (s == 0.0f) return {v, v, v};
        if (v == 0.0f) return {0.0f, 0.0f, 0.0f};

        float sector = h * 6.0f;
        int index = static_cast<int>(std::floor(sector));
        float fraction = sector - static_cast<float>(index);
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * fraction);
        float t = v * (1.0f - s * (1.0f - fraction));

        Color result{};
        switch (index) {
            case -1: result = {v, p, q}; break;
            case 0:
            case 6: result = {v, t, p}; break;
            case 1: result = {q, v, p}; break;
            case 2: result = {p, v, t}; break;
            case 3: result = {p, q, v}; break;
            case 4: result = {t, p, v}; break;
            case 5: result = {v, p, q}; break;
            default: result = {0.0f, 0.0f, 0.0f}; break;
        }

        if (hdr) return result;
        return {Clamp01(result.r), Clamp01(result.g), Clamp01(result.b), result.a};
    }

    inline Color Color::Lerp(Color a, Color b, float t) {
        t = Clamp01(t);
        return {
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        };
    }

    inline float Vector2::Angle(Vector2 from, Vector2 to) {
        float denominatorSq = SqrMagnitude(from) * SqrMagnitude(to);
        if (denominatorSq == 0.0f) return 0.0f;
        return std::acos(std::clamp(Dot(from, to) * (1.0f / std::sqrt(denominatorSq)), -1.0f, 1.0f));
    }

    inline Vector2 Vector2::ClampMagnitude(Vector2 vector, float maxLength) {
        float length = Magnitude(vector);
        return length > maxLength ? vector * (maxLength / length) : vector;
    }

    inline Vector2 Vector2::FromPolar(float radius, float theta) {
        return {radius * std::cos(theta), radius * std::sin(theta)};
    }

    inline Vector2 Vector2::Max(Vector2 lhs, Vector2 rhs) {
        return {std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y)};
    }

    inline Vector2 Vector2::Min(Vector2 lhs, Vector2 rhs) {
        return {std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y)};
    }

    inline Vector2 Vector2::MoveTowards(Vector2 current, Vector2 target, float maxDistanceDelta) {
        Vector2 delta = target - current;
        float distance = Magnitude(delta);
        if (distance <= maxDistanceDelta || distance == 0.0f) return target;
        return current + delta * (maxDistanceDelta / distance);
    }

    inline Vector2 Vector2::Normalize(Vector2 vector) {
        float magnitude = Magnitude(vector);
        return magnitude == 0.0f ? zero : vector * (1.0f / magnitude);
    }

    inline void Vector2::OrthoNormalize(Vector2& normal, Vector2& tangent) {
        normal = Normalize(normal);
        tangent = Normalize(Reject(tangent, normal));
    }

    inline Vector2 Vector2::Project(Vector2 a, Vector2 b) {
        float sqrMagnitude = SqrMagnitude(b);
        return sqrMagnitude == 0.0f ? zero : b * (Dot(a, b) / sqrMagnitude);
    }

    inline Vector2 Vector2::Reflect(Vector2 direction, Vector2 normal) {
        return direction - 2.0f * Project(direction, normal);
    }

    inline Vector2 Vector2::RotateTowards(Vector2 current, Vector2 target, float maxRadiansDelta, float maxMagnitudeDelta) {
        float currentMagnitude = Magnitude(current);
        float targetMagnitude = Magnitude(target);
        float newMagnitude = std::clamp(currentMagnitude + maxMagnitudeDelta * static_cast<float>((targetMagnitude > currentMagnitude) - (currentMagnitude > targetMagnitude)),
                                        std::min(currentMagnitude, targetMagnitude),
                                        std::max(currentMagnitude, targetMagnitude));
        float totalAngle = Angle(current, target) - maxRadiansDelta;
        if (totalAngle <= 0.0f) return Normalize(target) * newMagnitude;
        if (totalAngle >= static_cast<float>(M_PI)) return -Normalize(target) * newMagnitude;

        float axis = current.x * target.y - current.y * target.x;
        axis = std::fabs(axis) > 0.00001f ? axis / std::fabs(axis) : 1.0f;
        current = Normalize(current);
        return (current * std::cos(maxRadiansDelta) + Perpendicular(current) * std::sin(maxRadiansDelta) * axis) * newMagnitude;
    }

    inline Vector2 Vector2::Slerp(Vector2 a, Vector2 b, float t) {
        if (t < 0.0f) return a;
        if (t > 1.0f) return b;
        return SlerpUnclamped(a, b, t);
    }

    inline Vector2 Vector2::SlerpUnclamped(Vector2 a, Vector2 b, float t) {
        float magA = Magnitude(a);
        float magB = Magnitude(b);
        if (magA == 0.0f || magB == 0.0f) return LerpUnclamped(a, b, t);
        a /= magA;
        b /= magB;
        float dot = std::clamp(Dot(a, b), -1.0f, 1.0f);
        float theta = std::acos(dot) * t;
        Vector2 relative = Normalize(b - a * dot);
        return (a * std::cos(theta) + relative * std::sin(theta)) * (magA + (magB - magA) * t);
    }

    inline Vector2 Vector2::SmoothDamp(Vector2 current, Vector2 target, Vector2& currentVelocity, float smoothTime, float maxSpeed, float deltaTime) {
        smoothTime = std::max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        Vector2 change = current - target;
        Vector2 originalTarget = target;
        float maxChange = maxSpeed * smoothTime;
        float maxChangeSq = maxChange * maxChange;
        if (SqrMagnitude(change) > maxChangeSq) change = Normalize(change) * maxChange;
        target = current - change;
        Vector2 temp = (currentVelocity + omega * change) * deltaTime;
        currentVelocity = (currentVelocity - omega * temp) * exp;
        Vector2 output = target + (change + temp) * exp;
        if (Dot(originalTarget - current, output - originalTarget) > 0.0f) {
            currentVelocity = (output - originalTarget) / deltaTime;
            return originalTarget;
        }
        return output;
    }

    inline void Vector2::ToPolar(Vector2 vector, float& radius, float& theta) {
        radius = Magnitude(vector);
        theta = std::atan2(vector.y, vector.x);
    }

    inline Vector2::operator Vector3() const {
        return {x, y, 0.0f};
    }

    inline Vector3::operator Vector4() const {
        return {x, y, z, 0.0f};
    }

    inline Vector3 Vector3::Cross(Vector3 lhs, Vector3 rhs) {
        return {
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x
        };
    }

    inline float Vector3::Angle(Vector3 from, Vector3 to) {
        float denominatorSq = SqrMagnitude(from) * SqrMagnitude(to);
        if (denominatorSq == 0.0f) return 0.0f;
        return std::acos(std::clamp(Dot(from, to) * (1.0f / std::sqrt(denominatorSq)), -1.0f, 1.0f));
    }

    inline Vector3 Vector3::ClampMagnitude(Vector3 vector, float maxLength) {
        float length = Magnitude(vector);
        return length > maxLength ? vector * (maxLength / length) : vector;
    }

    inline Vector3 Vector3::FromSpherical(float radius, float theta, float phi) {
        return {
            radius * std::sin(theta) * std::cos(phi),
            radius * std::sin(theta) * std::sin(phi),
            radius * std::cos(theta)
        };
    }

    inline Vector3 Vector3::FromString(const std::string& value) {
        std::vector<float> values;
        std::stringstream stream(value);
        std::string part;

        while (std::getline(stream, part, ',')) {
            if (!part.empty() && part.back() == 'f') part.pop_back();
            if (!part.empty()) values.push_back(std::stof(part));
        }

        return values.size() == 3 ? Vector3(values[0], values[1], values[2]) : zero;
    }

    inline Vector3 Vector3::Max(Vector3 lhs, Vector3 rhs) {
        return {std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y), std::max(lhs.z, rhs.z)};
    }

    inline Vector3 Vector3::Min(Vector3 lhs, Vector3 rhs) {
        return {std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y), std::min(lhs.z, rhs.z)};
    }

    inline Vector3 Vector3::MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta) {
        Vector3 delta = target - current;
        float distance = Magnitude(delta);
        if (distance <= maxDistanceDelta || distance == 0.0f) return target;
        return current + delta * (maxDistanceDelta / distance);
    }

    inline Vector3 Vector3::Normalize(Vector3 vector) {
        float magnitude = Magnitude(vector);
        return magnitude == 0.0f ? zero : vector * (1.0f / magnitude);
    }

    inline float Vector3::NormalizeAngle(float value, bool is180) {
        float res = std::fmod(value, 360.0f);
        if (res > 180.0f) res -= 360.0f;
        else if (res < -180.0f) res += 360.0f;

        if (is180) return res;
        return res < 0.0f ? res + 360.0f : res;
    }

    inline Vector3 Vector3::NormalizeEuler(Vector3 vector, bool is180) {
        return {
            NormalizeAngle(vector.x, is180),
            NormalizeAngle(vector.y, is180),
            NormalizeAngle(vector.z, is180)
        };
    }

    inline Vector3 Vector3::Orthogonal(Vector3 vector) {
        return vector.z < vector.x ? Vector3(vector.y, -vector.x, 0.0f) : Vector3(0.0f, -vector.z, vector.y);
    }

    inline void Vector3::OrthoNormalize(Vector3& normal, Vector3& tangent) {
        Vector3 n = normal;
        Vector3 t = tangent;

        float magN = Magnitude(n);
        n = magN > kEpsilon ? n * (1.0f / magN) : right;

        t -= Dot(n, t) * n;
        float magT = Magnitude(t);
        t = magT > kEpsilon ? t * (1.0f / magT) : OrthoNormalVectorFast(n);

        normal = n;
        tangent = t;
    }

    inline void Vector3::OrthoNormalize(Vector3& normal, Vector3& tangent, Vector3& binormal) {
        Vector3 n = normal;
        Vector3 t = tangent;
        Vector3 b = binormal;

        float magN = Magnitude(n);
        n = magN > kEpsilon ? n * (1.0f / magN) : right;

        t -= Dot(n, t) * n;
        float magT = Magnitude(t);
        t = magT > kEpsilon ? t * (1.0f / magT) : OrthoNormalVectorFast(n);

        b -= Dot(n, b) * n + Dot(t, b) * t;
        float magB = Magnitude(b);
        b = magB > kEpsilon ? b * (1.0f / magB) : Cross(n, t);

        normal = n;
        tangent = t;
        binormal = b;
    }

    inline void Vector3::OrthoNormalizeFast(Vector3& normal, Vector3& tangent, Vector3& binormal) {
        Vector3 n = Normalize(normal);
        Vector3 t = Normalize(tangent - Dot(n, tangent) * n);
        Vector3 b = Normalize(binormal - Dot(n, binormal) * n - Dot(t, binormal) * t);

        normal = n;
        tangent = t;
        binormal = b;
    }

    inline Vector3 Vector3::OrthoNormalVectorFast(const Vector3& normal) {
        constexpr float oneOverSqrt2 = 0.7071067811865475244f;
        if (std::fabs(normal.z) > oneOverSqrt2) {
            float scale = 1.0f / std::sqrt(normal.y * normal.y + normal.z * normal.z);
            return {0.0f, -normal.z * scale, normal.y * scale};
        }

        float scale = 1.0f / std::sqrt(normal.x * normal.x + normal.y * normal.y);
        return {-normal.y * scale, normal.x * scale, 0.0f};
    }

    inline Vector3 Vector3::Project(Vector3 vector, Vector3 onNormal) {
        float sqrMagnitude = Dot(onNormal, onNormal);
        if (sqrMagnitude < kEpsilon) return zero;
        return onNormal * (Dot(vector, onNormal) / sqrMagnitude);
    }

    inline Vector3 Vector3::ProjectOnPlane(Vector3 vector, Vector3 planeNormal) {
        float sqrMagnitude = Dot(planeNormal, planeNormal);
        if (sqrMagnitude < kEpsilon) return vector;
        return vector - planeNormal * (Dot(vector, planeNormal) / sqrMagnitude);
    }

    inline Vector3 Vector3::Reflect(Vector3 direction, Vector3 normal) {
        return direction - 2.0f * Dot(normal, direction) * normal;
    }

    inline Vector3 Vector3::RotateTowards(Vector3 current, Vector3 target, float maxRadiansDelta, float maxMagnitudeDelta) {
        float currentMagnitude = Magnitude(current);
        float targetMagnitude = Magnitude(target);
        float newMagnitude = std::clamp(currentMagnitude + maxMagnitudeDelta * static_cast<float>((targetMagnitude > currentMagnitude) - (currentMagnitude > targetMagnitude)),
                                        std::min(currentMagnitude, targetMagnitude),
                                        std::max(currentMagnitude, targetMagnitude));
        float totalAngle = Angle(current, target) - maxRadiansDelta;
        if (totalAngle <= 0.0f) return Normalize(target) * newMagnitude;
        if (totalAngle >= static_cast<float>(M_PI)) return -Normalize(target) * newMagnitude;

        Vector3 axis = Cross(current, target);
        float axisMagnitude = Magnitude(axis);
        axis = axisMagnitude == 0.0f ? Normalize(Cross(current, current + Vector3(3.95f, 5.32f, -4.24f))) : axis / axisMagnitude;
        current = Normalize(current);
        return (current * std::cos(maxRadiansDelta) + Cross(axis, current) * std::sin(maxRadiansDelta)) * newMagnitude;
    }

    inline float Vector3::SignedAngle(Vector3 from, Vector3 to, Vector3 axis) {
        float sign = Dot(axis, Cross(from, to)) >= 0.0f ? 1.0f : -1.0f;
        return Angle(from, to) * sign;
    }

    inline Vector3 Vector3::Slerp(Vector3 a, Vector3 b, float t) {
        if (t < 0.0f) return a;
        if (t > 1.0f) return b;
        return SlerpUnclamped(a, b, t);
    }

    inline Vector3 Vector3::SlerpUnclamped(Vector3 a, Vector3 b, float t) {
        float magA = Magnitude(a);
        float magB = Magnitude(b);
        if (magA == 0.0f || magB == 0.0f) return LerpUnclamped(a, b, t);
        a /= magA;
        b /= magB;
        float dot = std::clamp(Dot(a, b), -1.0f, 1.0f);
        float theta = std::acos(dot) * t;
        Vector3 relative = Normalize(b - a * dot);
        return (a * std::cos(theta) + relative * std::sin(theta)) * (magA + (magB - magA) * t);
    }

    inline Vector3 Vector3::SmoothDamp(Vector3 current, Vector3 target, Vector3& currentVelocity, float smoothTime, float maxSpeed, float deltaTime) {
        smoothTime = std::max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        Vector3 change = current - target;
        Vector3 originalTarget = target;
        float maxChange = maxSpeed * smoothTime;
        float maxChangeSq = maxChange * maxChange;
        if (SqrMagnitude(change) > maxChangeSq) change = Normalize(change) * maxChange;
        target = current - change;
        Vector3 temp = (currentVelocity + omega * change) * deltaTime;
        currentVelocity = (currentVelocity - omega * temp) * exp;
        Vector3 output = target + (change + temp) * exp;
        if (Dot(originalTarget - current, output - originalTarget) > 0.0f) {
            currentVelocity = (output - originalTarget) / deltaTime;
            return originalTarget;
        }
        return output;
    }

    inline void Vector3::ToSpherical(Vector3 vector, float& radius, float& theta, float& phi) {
        radius = Magnitude(vector);
        if (radius == 0.0f) {
            theta = 0.0f;
            phi = 0.0f;
            return;
        }
        theta = std::acos(std::clamp(vector.z / radius, -1.0f, 1.0f));
        phi = std::atan2(vector.y, vector.x);
    }


    inline Vector4::Vector4(Color value) : x(value.r), y(value.g), z(value.b), w(value.a) {}

    inline Vector4 Vector4::Normalize(Vector4 value) {
        float magnitude = Magnitude(value);
        return magnitude > Vector3::kEpsilon ? value * (1.0f / magnitude) : zero;
    }

    inline Quaternion& Quaternion::operator*=(Quaternion value) {
        float newX = w * value.x + x * value.w + y * value.z - z * value.y;
        float newY = w * value.y + y * value.w + z * value.x - x * value.z;
        float newZ = w * value.z + z * value.w + x * value.y - y * value.x;
        float newW = w * value.w - x * value.x - y * value.y - z * value.z;
        x = newX;
        y = newY;
        z = newZ;
        w = newW;
        return *this;
    }

    inline Quaternion& Quaternion::operator/=(Quaternion value) {
        float newX = w / value.x + x / value.w + y / value.z - z / value.y;
        float newY = w / value.y + y / value.w + z / value.x - x / value.z;
        float newZ = w / value.z + z / value.w + x / value.y - y / value.x;
        float newW = w / value.w - x / value.x - y / value.y - z / value.z;
        x = newX;
        y = newY;
        z = newZ;
        w = newW;
        return *this;
    }

    inline float Quaternion::Angle(Quaternion a, Quaternion b) {
        return std::acos(std::min(std::fabs(Dot(a, b)), 1.0f)) * 2.0f;
    }

    inline Quaternion Quaternion::FromAngleAxis(float angle, Vector3 axis) {
        float magnitude = Vector3::Magnitude(axis);
        if (magnitude == 0.0f) return identity;
        float scale = std::sin(angle * 0.5f) / magnitude;
        return {axis.x * scale, axis.y * scale, axis.z * scale, std::cos(angle * 0.5f)};
    }

    inline Quaternion Quaternion::FromEuler(float yaw, float pitch, float roll, bool fromDeg) {
        if (fromDeg) {
            yaw *= Deg2Rad;
            pitch *= Deg2Rad;
            roll *= Deg2Rad;
        }

        float cy = std::cos(yaw * 0.5f);
        float sy = std::sin(yaw * 0.5f);
        float cp = std::cos(pitch * 0.5f);
        float sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f);
        float sr = std::sin(roll * 0.5f);

        return {
            cp * sy * cr + sp * cy * sr,
            sp * cy * cr - cp * sy * sr,
            cp * cy * sr - sp * sy * cr,
            cp * cy * cr + sp * sy * sr
        };
    }

    inline Quaternion Quaternion::FromToRotation(Vector3 fromVector, Vector3 toVector) {
        float dot = Vector3::Dot(fromVector, toVector);
        float magnitude = std::sqrt(Vector3::SqrMagnitude(fromVector) * Vector3::SqrMagnitude(toVector));
        if (magnitude == 0.0f) return identity;
        if (std::fabs(dot / magnitude + 1.0f) < 0.00001f) {
            return {Vector3::Normalize(Vector3::Orthogonal(fromVector)), 0.0f};
        }
        return Normalize(Quaternion(Vector3::Cross(fromVector, toVector), dot + magnitude));
    }

    inline Quaternion Quaternion::Inverse(Quaternion rotation) {
        float norm = Norm(rotation);
        return norm == 0.0f ? identity : Conjugate(rotation) / (norm * norm);
    }

    inline Quaternion Quaternion::Lerp(Quaternion a, Quaternion b, float t) {
        if (t < 0.0f) return Normalize(a);
        if (t > 1.0f) return Normalize(b);
        return LerpUnclamped(a, b, t);
    }

    inline Quaternion Quaternion::LerpUnclamped(Quaternion a, Quaternion b, float t) {
        Quaternion result = Dot(a, b) >= 0.0f ? a * (1.0f - t) + b * t : a * (1.0f - t) - b * t;
        return Normalize(result);
    }

    inline Quaternion Quaternion::LookRotation(Vector3 forward, Vector3 upwards) {
        forward = Vector3::Normalize(forward);
        upwards = Vector3::Normalize(upwards);
        constexpr float small = 0.0000000001f;
        if (Vector3::SqrMagnitude(forward) < small || Vector3::SqrMagnitude(upwards) < small) return identity;
        if (1.0f - std::fabs(Vector3::Dot(forward, upwards)) < small) return FromToRotation(Vector3::forward, forward);

        Vector3 right = Vector3::Normalize(Vector3::Cross(upwards, forward));
        upwards = Vector3::Cross(forward, right);
        Quaternion result{};
        float trace = right.x + upwards.y + forward.z;

        if (trace > 0.0f) {
            result.w = std::sqrt(1.0f + trace) * 0.5f;
            float reciprocal = 1.0f / (4.0f * result.w);
            result.x = (upwards.z - forward.y) * reciprocal;
            result.y = (forward.x - right.z) * reciprocal;
            result.z = (right.y - upwards.x) * reciprocal;
        } else if (right.x >= upwards.y && right.x >= forward.z) {
            result.x = std::sqrt(1.0f + right.x - upwards.y - forward.z) * 0.5f;
            float reciprocal = 1.0f / (4.0f * result.x);
            result.w = (upwards.z - forward.y) * reciprocal;
            result.z = (forward.x + right.z) * reciprocal;
            result.y = (right.y + upwards.x) * reciprocal;
        } else if (upwards.y > forward.z) {
            result.y = std::sqrt(1.0f - right.x + upwards.y - forward.z) * 0.5f;
            float reciprocal = 1.0f / (4.0f * result.y);
            result.z = (upwards.z + forward.y) * reciprocal;
            result.w = (forward.x - right.z) * reciprocal;
            result.x = (right.y + upwards.x) * reciprocal;
        } else {
            result.z = std::sqrt(1.0f - right.x - upwards.y + forward.z) * 0.5f;
            float reciprocal = 1.0f / (4.0f * result.z);
            result.y = (upwards.z + forward.y) * reciprocal;
            result.x = (forward.x + right.z) * reciprocal;
            result.w = (right.y - upwards.x) * reciprocal;
        }

        return result;
    }

    inline Quaternion Quaternion::Normalize(Quaternion rotation) {
        float norm = Norm(rotation);
        return norm == 0.0f ? identity : rotation / norm;
    }

    inline Quaternion Quaternion::RotateTowards(Quaternion from, Quaternion to, float maxRadiansDelta) {
        float angle = Angle(from, to);
        if (angle == 0.0f) return to;
        maxRadiansDelta = std::max(maxRadiansDelta, angle - static_cast<float>(M_PI));
        return SlerpUnclamped(from, to, std::min(1.0f, maxRadiansDelta / angle));
    }

    inline Quaternion Quaternion::Slerp(Quaternion a, Quaternion b, float t) {
        if (t < 0.0f) return Normalize(a);
        if (t > 1.0f) return Normalize(b);
        return SlerpUnclamped(a, b, t);
    }

    inline Quaternion Quaternion::SlerpUnclamped(Quaternion a, Quaternion b, float t) {
        float dot = Dot(a, b);
        if (dot < 0.0f) {
            b = -b;
            dot = -dot;
        }

        if (dot < 0.95f) {
            float angle = std::acos(dot);
            return (a * std::sin(angle * (1.0f - t)) + b * std::sin(angle * t)) * (1.0f / std::sin(angle));
        }

        return LerpUnclamped(a, b, t);
    }

    inline void Quaternion::ToAngleAxis(Quaternion rotation, float& angle, Vector3& axis) {
        if (rotation.w > 1.0f) rotation = Normalize(rotation);
        angle = 2.0f * std::acos(rotation.w);
        float scale = std::sqrt(1.0f - rotation.w * rotation.w);
        axis = scale < 0.00001f ? Vector3::right : Vector3(rotation.x / scale, rotation.y / scale, rotation.z / scale);
    }

    inline Vector3 Quaternion::ToEuler(Quaternion value, bool toDeg) {
        Vector3 rotation{};
        float xy = value.x * value.y;
        float xw = value.x * value.w;
        float yz = value.y * value.z;
        float zw = value.z * value.w;
        float singularity = yz - xw;

        rotation.x = -std::asin(std::clamp(2.0f * singularity, -1.0f, 1.0f));

        if (std::fabs(singularity) < 0.499999f) {
            float xx = value.x * value.x;
            float yy = value.y * value.y;
            float yw = value.y * value.w;
            float zz = value.z * value.z;
            float ww = value.w * value.w;
            rotation.y = std::atan2(2.0f * (value.x * value.z + yw), zz - xx - yy + ww);
            rotation.z = std::atan2(2.0f * (xy + zw), yy - zz - xx + ww);
        } else {
            float a = xy + zw;
            float b = -yz + xw;
            float c = xy - zw;
            float e = yz + xw;
            rotation.y = std::atan2(a * e + b * c, b * e - a * c);
            rotation.z = 0.0f;
        }

        return toDeg ? rotation * Rad2Deg : rotation;
    }

    inline Vector3 Quaternion::RotateVectorByQuaternion(Quaternion lhs, Vector3 rhs) {
        float x2 = lhs.x * 2.0f;
        float y2 = lhs.y * 2.0f;
        float z2 = lhs.z * 2.0f;
        float xx = lhs.x * x2;
        float yy = lhs.y * y2;
        float zz = lhs.z * z2;
        float xy = lhs.x * y2;
        float xz = lhs.x * z2;
        float yz = lhs.y * z2;
        float wx = lhs.w * x2;
        float wy = lhs.w * y2;
        float wz = lhs.w * z2;

        return {
            (1.0f - (yy + zz)) * rhs.x + (xy - wz) * rhs.y + (xz + wy) * rhs.z,
            (xy + wz) * rhs.x + (1.0f - (xx + zz)) * rhs.y + (yz - wx) * rhs.z,
            (xz - wy) * rhs.x + (yz + wx) * rhs.y + (1.0f - (xx + yy)) * rhs.z
        };
    }
}
