#version 450
layout(set = 0, binding = 0) uniform sampler2D gameTexture;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PresentationPush {
    vec2 sourceSize;
    float scanlineStrength;
    float sharpenStrength;
    float maskStrength;
    uint flags;
    float gamma;
    float saturation;
    float contrast;
    float brightness;
    float curvature;
    float vignette;
    int maskStyle;
    float overscan;
    float temperature;
    float outScaleX;
    float outScaleY;
    float compositeBlur;
    int scaleMode;
    int colorProfile;
} pc;
const uint FLAG_LINEAR = 1u;
const uint FLAG_PSEUDO_HIRES = 2u;
ivec2 clampCoord(ivec2 p) {
    return clamp(p, ivec2(0), ivec2(pc.sourceSize) - ivec2(1));
}
vec4 sourceTexel(ivec2 p) {
    p = clampCoord(p);
    if ((pc.flags & FLAG_PSEUDO_HIRES) != 0u) {
        int pairX = p.x & ~1;
        ivec2 a = clampCoord(ivec2(pairX, p.y));
        ivec2 b = clampCoord(ivec2(pairX + 1, p.y));
        return 0.5 * (texelFetch(gameTexture, a, 0) + texelFetch(gameTexture, b, 0));
    }
    return texelFetch(gameTexture, p, 0);
}
vec4 sampleNearest(vec2 uv) {
    ivec2 p = ivec2(floor(clamp(uv, vec2(0.0), vec2(0.999999)) * pc.sourceSize));
    return sourceTexel(p);
}
vec4 sampleLinear(vec2 uv) {
    vec2 pos = clamp(uv, vec2(0.0), vec2(1.0)) * pc.sourceSize - vec2(0.5);
    ivec2 p = ivec2(floor(pos));
    vec2 f = fract(pos);
    vec4 a = mix(sourceTexel(p), sourceTexel(p + ivec2(1, 0)), f.x);
    vec4 b = mix(sourceTexel(p + ivec2(0, 1)), sourceTexel(p + ivec2(1, 1)), f.x);
    return mix(a, b, f.y);
}
vec4 sampleSharpBilinear(vec2 uv) {
    float prescale = max(floor(min(pc.outScaleX, pc.outScaleY)), 1.0);
    vec2 texel = clamp(uv, vec2(0.0), vec2(1.0)) * pc.sourceSize;
    vec2 base = floor(texel);
    vec2 centerDist = (texel - base) - vec2(0.5);
    vec2 region = vec2(0.5 - 0.5 / prescale);
    vec2 f = (centerDist - clamp(centerDist, -region, region)) * prescale + vec2(0.5);
    return sampleLinear((base + f) / pc.sourceSize);
}
vec4 cubicWeights(float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return vec4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2);
}
vec4 sampleBicubic(vec2 uv) {
    vec2 pos = clamp(uv, vec2(0.0), vec2(1.0)) * pc.sourceSize - vec2(0.5);
    ivec2 p = ivec2(floor(pos));
    vec2 f = fract(pos);
    vec4 wx = cubicWeights(f.x);
    vec4 wy = cubicWeights(f.y);
    vec4 sum = vec4(0.0);
    for (int j = 0; j < 4; ++j) {
        vec4 row = vec4(0.0);
        for (int i = 0; i < 4; ++i) row += sourceTexel(p + ivec2(i - 1, j - 1)) * wx[i];
        sum += row * wy[j];
    }
    return clamp(sum, 0.0, 1.0);
}
vec4 sampleBase(vec2 uv) {
    if (pc.scaleMode == 2) return sampleSharpBilinear(uv);
    if (pc.scaleMode == 3) return sampleBicubic(uv);
    return (pc.flags & FLAG_LINEAR) != 0u ? sampleLinear(uv) : sampleNearest(uv);
}
vec3 toYIQ(vec3 c) {
    return vec3(dot(c, vec3(0.299, 0.587, 0.114)), dot(c, vec3(0.596, -0.274, -0.322)), dot(c, vec3(0.211, -0.523, 0.312)));
}
vec3 fromYIQ(vec3 c) {
    return vec3(c.x + 0.956 * c.y + 0.621 * c.z, c.x - 0.272 * c.y - 0.647 * c.z, c.x - 1.106 * c.y + 1.703 * c.z);
}
vec3 applyColorProfile(vec3 c) {
    if (pc.colorProfile == 0) return c;
    if (pc.colorProfile == 3) {
        float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
        return clamp(mix(vec3(luma), c, 1.28), 0.0, 1.0);
    }
    vec3 lin = pow(max(c, vec3(0.0)), vec3(2.2));
    if (pc.colorProfile == 1) {
        lin = mat3(1.0654, -0.0196, 0.0016, -0.0554, 1.0364, 0.0044, -0.0010, -0.0167, 0.9940) * lin;
    } else {
        lin = mat3(1.0440, 0.0, 0.0, -0.0440, 1.0, 0.0118, 0.0, 0.0, 0.9882) * lin;
    }
    return pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2));
}
void main() {
    vec2 crop = vec2(pc.overscan);
    vec2 uv = mix(crop, vec2(1.0) - crop, clamp(vUV, vec2(0.0), vec2(1.0)));
    vec2 centered = uv * 2.0 - 1.0;
    if (pc.curvature > 0.001) {
        float r2 = dot(centered, centered);
        centered *= 1.0 + pc.curvature * r2;
        uv = centered * 0.5 + 0.5;
    }
    vec4 color = sampleBase(uv);
    if (pc.compositeBlur > 0.001) {
        float step = ((pc.flags & FLAG_PSEUDO_HIRES) != 0u ? 2.0 : 1.0) / pc.sourceSize.x;
        vec3 y0 = toYIQ(color.rgb);
        vec3 ym1 = toYIQ(sampleBase(uv - vec2(step, 0.0)).rgb);
        vec3 yp1 = toYIQ(sampleBase(uv + vec2(step, 0.0)).rgb);
        vec3 ym2 = toYIQ(sampleBase(uv - vec2(2.0 * step, 0.0)).rgb);
        vec3 yp2 = toYIQ(sampleBase(uv + vec2(2.0 * step, 0.0)).rgb);
        float luma = mix(y0.x, 0.25 * ym1.x + 0.5 * y0.x + 0.25 * yp1.x, pc.compositeBlur * 0.6);
        vec2 chroma = (ym2.yz + 2.0 * ym1.yz + 3.0 * y0.yz + 2.0 * yp1.yz + yp2.yz) / 9.0;
        chroma = mix(y0.yz, chroma, pc.compositeBlur);
        color.rgb = clamp(fromYIQ(vec3(luma, chroma)), 0.0, 1.0);
    }
    color.rgb = applyColorProfile(color.rgb);
    if (pc.sharpenStrength > 0.001) {
        vec2 texel = 1.0 / pc.sourceSize;
        vec3 around = 0.25 * (
            sampleBase(uv + vec2( texel.x, 0.0)).rgb +
            sampleBase(uv + vec2(-texel.x, 0.0)).rgb +
            sampleBase(uv + vec2(0.0,  texel.y)).rgb +
            sampleBase(uv + vec2(0.0, -texel.y)).rgb);
        color.rgb = clamp(color.rgb + (color.rgb - around) * pc.sharpenStrength, 0.0, 1.0);
    }
    if (pc.scanlineStrength > 0.001) {
        int sourceY = int(floor(uv.y * pc.sourceSize.y));
        if ((sourceY & 1) != 0) color.rgb *= 1.0 - pc.scanlineStrength;
    }
    if (pc.maskStrength > 0.001) {
        int phase = int(gl_FragCoord.x) % 3;
        vec3 mask = vec3(1.0 - pc.maskStrength * 0.28);
        if (pc.maskStyle == 0) mask[phase] = 1.0;
        else if (pc.maskStyle == 1) mask = vec3(1.0 - pc.maskStrength * 0.18);
        else { int cell = (int(gl_FragCoord.x) + int(gl_FragCoord.y)) % 3; mask[cell] = 1.0; }
        color.rgb *= mask;
    }
    color.rgb = max(color.rgb + pc.brightness, vec3(0.0));
    color.rgb = (color.rgb - 0.5) * pc.contrast + 0.5;
    float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    color.rgb = mix(vec3(luminance), color.rgb, pc.saturation);
    color.rgb = pow(max(color.rgb, vec3(0.0)), vec3(1.0 / max(pc.gamma, 0.01)));
    color.r *= 1.0 + pc.temperature * 0.10;
    color.b *= 1.0 - pc.temperature * 0.10;
    if (pc.vignette > 0.001) {
        float edge = smoothstep(0.25, 1.15, dot(centered, centered));
        color.rgb *= 1.0 - pc.vignette * edge;
    }
    outColor = vec4(color.rgb, 1.0);
}
