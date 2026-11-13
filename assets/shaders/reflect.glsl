// A colour handheld screen with no light of its own: the room's light goes through each
// pixel's colour filter, off a grainy reflective backing and back out. Bright pixels show
// that backing through them (p_clear) with its grain (p_grain) and colour (p_tint); the
// pixel layer lies a little above it and throws a shadow on it (p_shadow); the front of
// the screen catches a soft band of glare (p_glare). Black is never quite black.
//
// Like the LCD grid, everything is measured in cells and averaged over each output pixel,
// so it looks the same at any size and fades out where there is no room for detail.
uniform float p_grid;
uniform float p_clear;
uniform float p_grain;
uniform float p_tint;
uniform float p_shadow;
uniform float p_glare;
uniform float p_bright;

// What the filters make of a colour: a little less pure, and darker in the middle tones.
vec3 Cell(vec2 cell) {
    vec3 c = ToLinear(Source((cell + 0.5) / u_source_size).rgb);
    return pow(mix(c, vec3(Luma(c)), 0.12), vec3(1.08));
}

// The colour an output pixel sees at `t` (in cells), the cells it lies across by their share.
vec3 Picture(vec2 t, vec2 w) {
    vec2 first = floor(t - 0.5);
    vec2 next = clamp((t - first - 1.0) / w + 0.5, 0.0, 1.0);
    return mix(mix(Cell(first), Cell(first + vec2(1.0, 0.0)), next.x),
               mix(Cell(first + vec2(0.0, 1.0)), Cell(first + vec2(1.0, 1.0)), next.x), next.y);
}

float Noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), f.x),
               mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// The backing's grain around 1: fine speckle, coarser blotches and faint horizontal fibres,
// fixed to the screen. The fine speckle gives way where a cell has too few output pixels.
float Grain(vec2 t, float least) {
    float fine = clamp((least - 2.0) / 3.0, 0.0, 1.0);
    float n = fine * 0.5 * (Noise(t * 3.0) - 0.5) + 0.35 * (Noise(t * 0.9 + 17.0) - 0.5) +
              0.3 * (Noise(vec2(t.x * 0.6, t.y * 5.0) + 41.0) - 0.5);
    return 1.0 + 0.4 * n;
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 room = u_output_size / u_source_size; // output pixels per cell
    float least = min(room.x, room.y);
    vec2 w = Footprint(room);
    vec3 c = Picture(t, w);
    float light = Luma(c);

    // The backing, seen through the bright pixels: its colour and grain, in proportion to
    // how much light gets through.
    vec3 backing = mix(vec3(1.0), vec3(1.0, 0.95, 0.83), p_tint) * mix(1.0, Grain(t, least), p_grain);
    c *= mix(vec3(1.0), backing, p_clear * clamp(light * 1.5, 0.0, 1.0));

    // The shadow of the pixel layer, a little down and to the right: the darker the pixel
    // that throws it, the less of the backing's light comes back.
    float lifted = Luma(Picture(t - vec2(0.18, 0.24), w));
    c *= 1.0 - 0.7 * p_shadow * clamp(light - lifted, 0.0, 1.0);

    // The lines between cells, as the LCD grid draws them.
    float fine = clamp(least - 2.0, 0.0, 1.0);
    float gap = 0.16 * p_grid;
    float drawn = max(gap, min(1.0 / least, 0.4));
    float lit_asked = (1.0 - gap) * (1.0 - gap), lit_drawn = (1.0 - drawn) * (1.0 - drawn);
    float strength = fine * (1.0 - lit_asked) / max(1.0 - lit_drawn, 0.001);
    vec2 lit = Lit(t, w, drawn);
    float open = lit_asked + strength * (lit.x * lit.y - lit_drawn);
    c *= (1.0 - 0.75 * (1.0 - open)) / (1.0 - 0.45 * (1.0 - lit_asked));

    // Some of the room's light comes back from the front whatever the pixels show.
    c = c * 0.88 + 0.015 * backing;

    // A soft diagonal band of glare with a faint rainbow at its edges, brighter over the
    // bright parts, where the backing gives it back.
    float across = (v_uv.x * 0.7 + (1.0 - v_uv.y)) - 0.62;
    float band = exp(-across * across / 0.03);
    vec3 rainbow = 0.5 + 0.5 * cos(2.0 * PI * (across * 2.5 + vec3(0.0, 0.33, 0.67)));
    c += p_glare * 0.09 * band * mix(vec3(1.0), rainbow, 0.35 * (1.0 - band)) * (0.35 + 0.65 * light);

    o_color = vec4(ToGamma(c * p_bright), 1.0);
}
