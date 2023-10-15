#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform TimeBuffer{
	float time;
	uvec4 frames;
} time;

void main()
{
    outColor = vec4(fragColor.r,
                    fragColor.g,
                    fragColor.b,
                    1.0);
    /*
    outColor = vec4(fragColor.r * cos(time.time * 5),
                    fragColor.g * sin(time.time * 5),
                    fragColor.b * cos(time.time * 5) * sin(time.time * 5),
                    1.0);
    */
}
