// A colour handheld screen with no light of its own: the room's light goes through each
// pixel's colour filter, off a reflective backing and back out. No filter stops all of it,
// so the darkest pixels stay grey (p_clear) and every pixel shows the backing: its colour
// (p_tint), its blotches and grain (p_grain). The pixel layer lies a little above the
// backing and throws a shadow on it (p_shadow); the front of the screen catches a soft band
// of glare (p_glare).
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

// What the filters make of a colour: paler than the picture asks for.
vec3 Cell(vec2 cell) {
    vec3 c = ToLinear(Source((cell + 0.5) / u_source_size).rgb);
    return mix(c, vec3(Luma(c)), 0.3);
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

// The backing around 1: broad pale blotches of colour, and a fine grain that gives way
// where a cell has too few output pixels.
vec3 Backing(vec2 t, float least) {
    vec3 blot = vec3(Noise(t * 0.07 + 1.0), Noise(t * 0.07 + 23.0), Noise(t * 0.07 + 57.0)) +
                0.5 * vec3(Noise(t * 0.2 + 5.0), Noise(t * 0.2 + 31.0), Noise(t * 0.2 + 77.0));
    float fine = clamp((least - 2.0) / 3.0, 0.0, 1.0) * (Noise(t * 2.5 + 11.0) - 0.5);
    vec3 tint = mix(vec3(1.0), vec3(1.0, 0.96, 0.86), p_tint);
    return tint * (1.0 + p_grain * (0.35 * (blot / 1.5 - 0.5) + 0.18 * fine));
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 room = u_output_size / u_source_size; // output pixels per cell
    float least = min(room.x, room.y);
    vec2 w = Footprint(room);
    vec3 c = Picture(t, w);
    float light = Luma(c);

    // No filter stops all the light.
    float least_light = 0.07 * p_clear;
    c = least_light + (1.0 - least_light) * c;

    // The shadow of the pixel layer, a little down and to the right, on the backing that
    // the lighter pixels show.
    float lifted = Luma(Picture(t - vec2(0.3, 0.35), w));
    c *= 1.0 - 0.75 * p_shadow * (1.0 - lifted) * clamp(light * 2.0, 0.0, 1.0);
    c *= 0.9 * Backing(t, least);

    // The lines between cells, as the LCD grid draws them.
    float fine = clamp(least - 2.0, 0.0, 1.0);
    float gap = 0.16 * p_grid;
    float drawn = max(gap, min(1.0 / least, 0.4));
    float lit_asked = (1.0 - gap) * (1.0 - gap), lit_drawn = (1.0 - drawn) * (1.0 - drawn);
    float strength = fine * (1.0 - lit_asked) / max(1.0 - lit_drawn, 0.001);
    vec2 lit = Lit(t, w, drawn);
    float open = lit_asked + strength * (lit.x * lit.y - lit_drawn);
    c *= 1.0 - 0.6 * (1.0 - open);

    // A soft diagonal band of glare with a faint rainbow at its edges, brighter over the
    // bright parts, where the backing gives it back.
    float across = (v_uv.x * 0.7 + (1.0 - v_uv.y)) - 0.62;
    float band = exp(-across * across / 0.03);
    vec3 rainbow = 0.5 + 0.5 * cos(2.0 * PI * (across * 2.5 + vec3(0.0, 0.33, 0.67)));
    c += p_glare * 0.09 * band * mix(vec3(1.0), rainbow, 0.35 * (1.0 - band)) * (0.35 + 0.65 * light);

    o_color = vec4(ToGamma(c * p_bright), 1.0);
}
