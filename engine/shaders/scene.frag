#version 450
layout(location = 0) in vec3 color;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec4 outColor;
layout(constant_id = 0) const bool encodeSRGB = false;
void main()
{
    // Fixed world-space key light; enough neutral fill to preserve state hues.
    float diffuse = max(dot(normalize(normal), normalize(vec3(-0.45, 0.85, 0.35))), 0.0);
    vec3 lit = color * (0.48 + 0.52 * diffuse);
    vec3 encoded = mix(12.92 * lit, 1.055 * pow(lit, vec3(1.0 / 2.4)) - 0.055, greaterThan(lit, vec3(0.0031308)));
    outColor = vec4(encodeSRGB ? encoded : lit, 1.0);
}
