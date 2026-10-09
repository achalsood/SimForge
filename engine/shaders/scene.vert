#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inStateWeight;
layout(push_constant) uniform Draw { mat4 mvp; mat3 normalMatrix; vec4 tint; } draw;
layout(location = 0) out vec3 color;
layout(location = 1) out vec3 normal;
void main()
{
    gl_Position = draw.mvp * vec4(inPosition, 1.0);
    // Presentation state can replace material hue without losing baked face/detail shading.
    float luminance = dot(inColor, vec3(0.2126, 0.7152, 0.0722));
    vec3 stateColor = mix(inColor, vec3(luminance), draw.tint.a) * draw.tint.rgb;
    color = mix(stateColor, inColor, draw.tint.a * (1.0 - inStateWeight));
    normal = draw.normalMatrix * inNormal;
}
