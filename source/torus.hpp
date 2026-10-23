#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace torus {

// вариант 7: тор R=1.4, r=0.5
constexpr float pi = 3.14159265358979323846f, major_radius = 1.4f, minor_radius = 0.5f;
constexpr uint32_t major_segments = 64, minor_segments = 32;

struct Vertex { float position[3]; float normal[3]; };
struct Mesh { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };

inline Mesh generate() {
	Mesh mesh;
	for (uint32_t i = 0; i < major_segments; ++i)
		for (uint32_t j = 0; j < minor_segments; ++j) {
			float u = 2 * pi * i / major_segments, v = 2 * pi * j / minor_segments;
			float radial = major_radius + minor_radius * std::cos(v);
			mesh.vertices.push_back({
				{radial * std::cos(u), radial * std::sin(u), minor_radius * std::sin(v)},
				{std::cos(v) * std::cos(u), std::cos(v) * std::sin(u), std::sin(v)},
			});
		}

	for (uint32_t i = 0; i < major_segments; ++i)
		for (uint32_t j = 0; j < minor_segments; ++j) {
			uint32_t a = i * minor_segments + j;
			uint32_t b = ((i + 1) % major_segments) * minor_segments + j;
			uint32_t c = ((i + 1) % major_segments) * minor_segments + (j + 1) % minor_segments;
			uint32_t d = i * minor_segments + (j + 1) % minor_segments;
			mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
		}
	return mesh;
}

struct Mat4 { float data[16]{}; };

inline Mat4 identity() {
	Mat4 m;
	m.data[0] = m.data[5] = m.data[10] = m.data[15] = 1;
	return m;
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
	Mat4 m;
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			for (int k = 0; k < 4; ++k)
				m.data[c * 4 + r] += a.data[k * 4 + r] * b.data[c * 4 + k];
	return m;
}

inline Mat4 rotate(float x, float y) {
	auto rx = identity(), ry = identity();
	rx.data[5] = rx.data[10] = std::cos(x);
	rx.data[6] = std::sin(x);
	rx.data[9] = -std::sin(x);
	ry.data[0] = ry.data[10] = std::cos(y);
	ry.data[2] = -std::sin(y);
	ry.data[8] = std::sin(y);
	return multiply(ry, rx);
}

inline Mat4 perspective(float aspect) {
	constexpr float n = 0.1f, f = 100.0f;
	const float cot = 1 / std::tan(45 * pi / 360);
	Mat4 p;
	p.data[0] = cot / aspect;
	p.data[5] = -cot;
	p.data[10] = f / (n - f);
	p.data[11] = -1;
	p.data[14] = f * n / (n - f);
	return p;
}

}
