#ifndef RAGDOLL_TEST_VECTOR_H
#define RAGDOLL_TEST_VECTOR_H
#include <cmath>
// Same float vector operations used by the engine solver, without engine
// allocation, rendering, collision or proprietary model dependencies.
struct CVector {
	float x, y, z;
	CVector() : x(0), y(0), z(0) {}
	CVector(float a, float b, float c) : x(a), y(b), z(c) {}
	CVector operator+(const CVector &b) const { return CVector(x+b.x, y+b.y, z+b.z); }
	CVector operator-(const CVector &b) const { return CVector(x-b.x, y-b.y, z-b.z); }
	CVector operator-() const { return CVector(-x, -y, -z); }
	CVector operator*(float s) const { return CVector(x*s, y*s, z*s); }
	CVector operator/(float s) const { return *this*(1.0f/s); }
	CVector &operator+=(const CVector &b) { return *this = *this+b; }
	CVector &operator-=(const CVector &b) { return *this = *this-b; }
	CVector &operator*=(float s) { return *this = *this*s; }
	float MagnitudeSqr() const { return x*x+y*y+z*z; }
	float Magnitude() const { return std::sqrt(MagnitudeSqr()); }
	void Normalise() { const float n=Magnitude(); if(n>0) *this*=1.0f/n; }
};
inline CVector operator*(float s, const CVector &v) { return v*s; }
inline float DotProduct(const CVector &a, const CVector &b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline CVector CrossProduct(const CVector &a, const CVector &b) {
	return CVector(a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x);
}
#endif
