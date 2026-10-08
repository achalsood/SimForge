#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(push_constant) uniform Draw { mat4 mvp; vec4 tint; } draw;
layout(location = 0) out vec3 color;
void main()
{
    gl_Position = draw.mvp * vec4(inPosition, 1.0);
    color = inColor * draw.tint.rgb;
}
