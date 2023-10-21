#version 450 core
layout(location = 0) out vec4 outColor;
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

layout(set=2, binding=0) uniform sampler2D sTexture;

layout(push_constant) uniform uPushConstant { float sec; uint frame; } pc;

void main()
{
    outColor = In.Color * texture(sTexture, In.UV.st) * -1 + vec4(pc.sec, 1.f, 1.f, 1.f);
}
