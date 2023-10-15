#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec3 fragColor;

layout(location = 0) in vec3 inpos;
layout(location = 2) in uvec3 inColor;

layout(set = 0, binding = 0) uniform CameraBuffer{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} camera;

layout(set = 0, binding = 1) uniform TimeBuffer{
	float time;
	uvec4 frames;
} time;

layout(set = 1, binding = 0) uniform ModelBuffer{
	mat4 transform;
} model;

void main() {
    gl_Position = camera.viewproj * model.transform * vec4(inpos, 1.f);

    fragColor = vec3(inColor.r / 255, inColor.g / 255, inColor.b / 255);
}
