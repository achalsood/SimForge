#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(push_constant) uniform Draw { mat4 mvp; vec4 tint; } draw;
layout(location = 0) out vec3 color;
void main()
{
    gl_Position = draw.mvp * vec4(inPosition, 1.0);
    // Presentation state can replace material hue without losing baked face/detail shading.
    float luminance = dot(inColor, vec3(0.2126, 0.7152, 0.0722));
    color = mix(inColor, vec3(luminance), draw.tint.a) * draw.tint.rgb;
}
