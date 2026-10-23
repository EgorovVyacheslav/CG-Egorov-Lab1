#version 450

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=0) out vec3 worldNormal;

layout(push_constant) uniform Transform { mat4 mvp; mat4 model; } transform;

void main() {
	gl_Position = transform.mvp * vec4(position, 1.0);
	worldNormal = mat3(transform.model) * normal;
}
