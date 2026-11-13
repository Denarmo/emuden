// A picture tube: one beam per row of the picture that widens where it is bright, the
// phosphor mask in front of it, the bulge of the glass and its dark corners.
//
// Without BLOOM the rows come from Source (a signal pass, or the picture itself). With
// BLOOM they come from Original and Source is the blurred picture, added as the light the
// glass scatters.
uniform float p_scanline;
uniform float p_mask;
uniform float p_mask_type; // 0 aperture grille, 1 slot mask, 2 fine dots
uniform float p_curve;
uniform float p_sharp;
uniform float p_vignette;
uniform float p_bright;
#ifdef BLOOM
uniform float p_bloom;
uniform float p_halation;
#define ROWS_SIZE u_original_size
#define ROWS(q) Original(q)
#else
#define ROWS_SIZE u_source_size
#define ROWS(q) Source(q)
#endif

// The glass bulges: the picture's sides stay where they are and its corners pull in.
vec2 Bulge(vec2 p) {
    vec2 c = p * 2.0 - 1.0;
    c *= 1.0 + p_curve * 0.11 * c.yx * c.yx;
    return c * 0.5 + 0.5;
}

// 1 inside the picture, 0 past its rounded corners.
float Inside(vec2 q) {
    if (p_curve <= 0.0) {
        return 1.0;
    }
    vec2 d = abs(q * 2.0 - 1.0);
    float radius = 0.03 + 0.07 * p_curve;
    vec2 e = max(d - (1.0 - radius), vec2(0.0)) / radius;
    float soft = 3.0 / (radius * min(u_output_size.x, u_output_size.y));
    return 1.0 - smoothstep(1.0 - soft, 1.0, length(e));
}

// A row of the picture at x, in linear light; black outside the picture.
vec3 Row(float x, float row) {
    vec2 size = ROWS_SIZE;
    if (row < 0.0 || row >= size.y) {
        return vec3(0.0);
    }
    float t = x * size.x - 0.5;
    float i = floor(t);
    float f = clamp((t - i - 0.5) * (1.0 + 5.0 * p_sharp) + 0.5, 0.0, 1.0);
    f = f * f * (3.0 - 2.0 * f);
    float y = (row + 0.5) / size.y;
    vec3 a = ToLinear(ROWS(vec2((i + 0.5) / size.x, y)).rgb);
    vec3 b = ToLinear(ROWS(vec2((i + 1.5) / size.x, y)).rgb);
    return mix(a, b, f);
}

// How much of a row's light reaches a point `d` rows from its middle.
vec3 Beam(float d, vec3 c) {
    vec3 width = vec3(mix(0.45, 0.2, p_scanline)) + 0.2 * clamp(c, vec3(0.0), vec3(1.0));
    vec3 gain = min(1.0 / (width * 2.5066), vec3(1.4));
    return gain * exp(-d * d / (2.0 * width * width));
}

vec3 Mask(vec2 pixel) {
    // Wider cells on a dense screen, or they are too fine to see.
    pixel = floor(pixel / max(1.0, floor(u_output_size.y / 1000.0 + 0.5)));
    vec3 cell;
    if (p_mask_type < 0.5) {
        float i = mod(pixel.x, 3.0);
        cell = vec3(float(i < 0.5), float(i > 0.5 && i < 1.5), float(i > 1.5));
    } else if (p_mask_type < 1.5) {
        float i = mod(pixel.x, 3.0);
        float row = pixel.y + 2.0 * mod(floor(pixel.x / 3.0), 2.0);
        float open = float(mod(row, 4.0) > 0.5);
        cell = vec3(float(i < 0.5), float(i > 0.5 && i < 1.5), float(i > 1.5)) * open;
    } else {
        float i = mod(pixel.x + pixel.y, 2.0);
        cell = i < 0.5 ? vec3(1.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    }
    return mix(vec3(1.0), cell, p_mask) / (1.0 - 0.6 * p_mask);
}

void main() {
    vec2 q = Bulge(v_uv);
    float inside = Inside(q);
    if (inside <= 0.0 || q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float rows = ROWS_SIZE.y;
    float y = q.y * rows - 0.5;
    float row = floor(y);
    float f = y - row;
    vec3 a = Row(q.x, row);
    vec3 b = Row(q.x, row + 1.0);
    vec3 flat_light = mix(a, b, f * f * (3.0 - 2.0 * f));
    vec3 beams = a * Beam(f, a) + b * Beam(1.0 - f, b);
    // Under about three output pixels a row the beams cannot be told apart.
    float room = clamp((u_output_size.y / rows - 2.0) / 1.5, 0.0, 1.0);
    vec3 c = mix(flat_light, beams, room * min(1.0, p_scanline * 4.0));
    c *= Mask(floor(v_uv * u_output_size));
#ifdef BLOOM
    vec3 soft = ToLinear(Source(q).rgb);
    // Light is added where there is room for it: bright parts do not wash out.
    c = min(c, vec3(1.0));
    c += (1.0 - c) * min(p_bloom * 0.7 * soft * (0.3 + Luma(soft)), vec3(1.0));
    c = mix(c, soft, p_halation * 0.3);
#endif
    vec2 e = q * 2.0 - 1.0;
    c *= 1.0 - p_vignette * 0.3 * dot(e, e);
    o_color = vec4(ToGamma(c * p_bright) * inside, 1.0);
}
