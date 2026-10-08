#version 450
layout(location = 0) in vec3 color;
layout(location = 0) out vec4 outColor;
layout(constant_id = 0) const bool encodeSRGB = false;
void main()
{
    vec3 encoded = mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, greaterThan(color, vec3(0.0031308)));
    outColor = vec4(encodeSRGB ? encoded : color, 1.0);
}
