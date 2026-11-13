// A colour handheld screen with no light of its own: the room's light goes through each
// pixel's colour filter, off a reflective backing and back out. The lighter a pixel, the
// more of the backing comes through it (p_clear), so pale colours look washed over the
// backing while black stays black. The backing has a colour (p_tint), broad blotches and a
// grain (p_grain). The pixel layer lies a little above it and throws a soft shadow on it
// (p_shadow); every pixel is three colour stripes (p_subpixel) with lines around it
// (p_grid); the front of the screen catches a soft band of glare (p_glare).
//
// Everything is measured in cells and averaged over each output pixel, so it looks the same
// at any size and fades out where there is no room for detail.
uniform float p_grid;
uniform float p_subpixel;
uniform float p_clear;
uniform float p_grain;
uniform float p_tint;
uniform float p_shadow;
uniform float p_glare;
uniform float p_bright;

// A pixel's colour as its filter passes it: the backing shows through the lighter ones.
vec3 Cell(vec2 cell) {
    vec3 g = Source((cell + 0.5) / u_source_size).rgb;
    float through = p_clear * pow(max(Luma(g), 0.0), 0.5);
    return ToLinear(mix(g, vec3(1.0), through));
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

// The backing, grey-green and darker than white: broad pale blotches of colour, and a fine grain that
// gives way where a cell has too few output pixels.
vec3 Backing(vec2 t, float least) {
    vec3 blot = vec3(Noise(t * 0.04 + 1.0), Noise(t * 0.04 + 23.0), Noise(t * 0.04 + 57.0)) +
                0.5 * vec3(Noise(t * 0.2 + 5.0), Noise(t * 0.2 + 31.0), Noise(t * 0.2 + 77.0));
    float fine = clamp((least - 2.0) / 3.0, 0.0, 1.0) * (Noise(t * 2.5 + 11.0) - 0.5);
    vec3 tint = mix(vec3(1.0), vec3(0.88, 1.0, 0.9), p_tint);
    return 0.58 * tint * (1.0 + p_grain * (0.4 * (blot / 1.5 - 0.5) + 0.08 * fine));
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 room = u_output_size / u_source_size; // output pixels per cell
    float least = min(room.x, room.y);
    vec2 w = Footprint(room);
    vec3 c = Picture(t, w);
    float light = Luma(c);

    // The shadow of the pixel layer, soft, down and to the right, on the lighter pixels.
    float lifted = 0.5 * (Luma(Picture(t - vec2(0.4, 0.5), w)) + Luma(Picture(t - vec2(0.75, 0.85), w)));
    c *= 1.0 - 0.7 * p_shadow * (1.0 - lifted) * clamp(light * 2.0, 0.0, 1.0);
    c *= Backing(t, least);

    // Three stripes a cell, as a wave that an output pixel averages; they need about five
    // output pixels a cell to show.
    float narrow = PI * w.x;
    float stripes = 0.8 * p_subpixel * clamp((room.x - 3.0) / 2.0, 0.0, 1.0) * sin(narrow) / narrow;
    c *= 1.0 + stripes * cos(2.0 * PI * (fract(t.x) - vec3(1.0, 3.0, 5.0) / 6.0));

    // The lines around each cell, at least an output pixel wide; they fade where there is
    // no room for them.
    float fine = clamp(least - 2.0, 0.0, 1.0);
    vec2 dark = 1.0 - Lit(t, w, max(0.12, 1.0 / least));
    c *= 1.0 - 0.4 * p_grid * fine * (dark.x + dark.y);

    // A soft diagonal band of glare with a faint rainbow at its edges, brighter over the
    // bright parts, where the backing gives it back.
    float across = (v_uv.x * 0.7 + (1.0 - v_uv.y)) - 0.62;
    float band = exp(-across * across / 0.03);
    vec3 rainbow = 0.5 + 0.5 * cos(2.0 * PI * (across * 2.5 + vec3(0.0, 0.33, 0.67)));
    c += p_glare * 0.06 * band * mix(vec3(1.0), rainbow, 0.35 * (1.0 - band)) * (0.35 + 0.65 * light);

    o_color = vec4(ToGamma(c * p_bright), 1.0);
}
