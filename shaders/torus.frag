#version 450

layout(location=0) in vec3 worldNormal;
layout(location=0) out vec4 color;

void main() {
	float diffuse = max(dot(normalize(worldNormal), normalize(vec3(-0.4, 0.7, 1.0))), 0.0);
	color = vec4(vec3(0.20, 0.62, 0.86) * (0.24 + 0.76 * diffuse), 1.0);
}
