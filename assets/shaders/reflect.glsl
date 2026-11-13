// A colour handheld screen with no light of its own: the room's light goes through each
// pixel's colour filter, off a grey-green reflective backing and back out. Every colour
// washes towards the backing, the lighter ones the most, and even black lets a little
// through (p_wash). The backing has broad blotches (p_texture) and its green (p_backing).
// Each pixel is three colour stripes (p_stripes) with lines between the rows (p_lines),
// and the pixel layer throws a shadow on the backing (p_shadows). The
// front of the screen can catch a soft band of glare (p_glare).
//
// The numbers come from measuring a capture of such a screen against the picture it
// showed. Everything is measured in cells and averaged over each output pixel, so it looks
// the same at any size and fades out where there is no room for detail.
uniform float p_wash;
uniform float p_texture;
uniform float p_backing;
uniform float p_stripes;
uniform float p_lines;
uniform float p_shadows;
uniform float p_glare;
uniform float p_bright;

// A pixel as the screen shows it over a white backing, in display (gamma) values: a curve
// in the pixel's own colour and its lightness.
vec3 Cell(vec2 cell) {
    vec3 g = Source((cell + 0.5) / u_source_size).rgb;
    float l = Luma(g);
    vec3 washed = 0.089 + 0.341 * g + vec3(1.147 * l - 0.576 * l * l) + 0.1 * (g - l);
    return mix(g, washed, p_wash);
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

// The backing in display values: grey-green, with broad blotches mostly in its red and blue.
vec3 Backing(vec2 t) {
    vec3 green = mix(vec3(0.66), vec3(0.569, 0.773, 0.537), p_backing);
    vec3 blot = vec3(Noise(t * 0.04 + 1.0), Noise(t * 0.04 + 23.0), Noise(t * 0.04 + 57.0)) +
                0.5 * vec3(Noise(t * 0.12 + 5.0), Noise(t * 0.12 + 31.0), Noise(t * 0.12 + 77.0));
    return green * (1.0 + p_texture * vec3(0.24, 0.08, 0.18) * (blot / 1.5 - 0.5));
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 room = u_output_size / u_source_size; // output pixels per cell
    float least = min(room.x, room.y);
    vec2 w = Footprint(room);

    // Three stripes a cell, red, green and blue from the left; they need about four output
    // pixels a cell to show. Each colour of a dark pixel ends at its own stripe, which leaves
    // a blue edge on the left of a dark stroke and a red one on its right.
    float show = clamp((room.x - 2.5) / 1.5, 0.0, 1.0);
    float edge = 0.2 * p_stripes * show;
    vec3 c = Picture(t, w);
    c.r = mix(c.r, Picture(t + vec2(edge, 0.0), w).r, 0.7);
    c.b = mix(c.b, Picture(t - vec2(edge, 0.0), w).b, 0.7);
    float light = Luma(c);

    // The pixel layer's shadow, as measured: a dark pixel shades the pixel a little over one
    // row below it. Beside it, the colour edges above already darken as much as measured.
    float below = Luma(Picture(t - vec2(0.0, 1.05), w)) + Luma(Picture(t - vec2(0.0, 1.25), w));
    float shade = 0.26 * (1.0 - 0.5 * below);
    c *= 1.0 - p_shadows * shade * clamp(light * 2.0, 0.0, 1.0);
    c *= Backing(t);

    // The lines between rows, a little nearer the bottom of a pixel than its top, and fainter
    // ones between columns, a little over a quarter of a cell wide in all; they fade where
    // there is no room for them.
    float fine = clamp(least - 2.0, 0.0, 1.0);
    vec2 gap = 1.0 - Lit(t + vec2(0.0, 0.04), w, 0.28);
    c *= 1.0 - p_lines * fine * (0.17 * gap.y + 0.06 * gap.x);

    // The stripes, as a wave that an output pixel averages. The backing is the colour of a
    // cell's middle, where green is strongest: a whole cell is less green.
    float narrow = PI * w.x;
    float stripes = p_stripes * show * sin(narrow) / narrow;
    vec3 phase = 2.0 * PI * (fract(t.x) - vec3(0.2, 0.5, 0.8));
    c *= vec3(1.03, 0.81, 1.05) + stripes * vec3(0.27, 0.26, 0.27) * cos(phase);

    // A soft diagonal band of glare with a faint rainbow at its edges.
    float across = (v_uv.x * 0.7 + (1.0 - v_uv.y)) - 0.62;
    float band = exp(-across * across / 0.03);
    vec3 rainbow = 0.5 + 0.5 * cos(2.0 * PI * (across * 2.5 + vec3(0.0, 0.33, 0.67)));
    c += p_glare * 0.06 * band * mix(vec3(1.0), rainbow, 0.35 * (1.0 - band)) * (0.35 + 0.65 * light);

    o_color = vec4(clamp(c * p_bright, 0.0, 1.0), 1.0);
}
