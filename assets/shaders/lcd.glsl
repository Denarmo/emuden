// A colour handheld screen: every pixel a cell of three colour stripes with dark lines
// around it, the duller colours of those panels, and (p_bleed) the light of a backlight
// leaking in from the edges where the picture should be black.
//
// The lines and stripes are measured in the picture's own pixels and averaged over each
// output pixel, so they are even at any size and the picture keeps its brightness: a line
// is never thinner than an output pixel (it gets paler instead), and lines and stripes
// fade out where there is no room for them.
uniform float p_grid;
uniform float p_subpixel;
uniform float p_color;
uniform float p_bleed;
uniform float p_bright;

// What such a panel makes of a colour: its primaries are less pure, and it is darker in
// the middle tones. White stays white.
vec3 Panel(vec3 c) {
    vec3 mixed = vec3(dot(c, vec3(0.82, 0.14, 0.04)), dot(c, vec3(0.08, 0.72, 0.20)),
                      dot(c, vec3(0.10, 0.14, 0.76)));
    return pow(mixed, vec3(1.18));
}

vec3 Cell(vec2 cell) {
    vec3 c = ToLinear(Source((cell + 0.5) / u_source_size).rgb);
    return mix(c, Panel(c), p_color);
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 room = u_output_size / u_source_size; // output pixels per cell
    float least = min(room.x, room.y);
    vec2 w = Footprint(room);

    // The cells an output pixel lies across, by their share of it.
    vec2 first = floor(t - 0.5);
    vec2 next = clamp((t - first - 1.0) / w + 0.5, 0.0, 1.0);
    vec3 c = mix(mix(Cell(first), Cell(first + vec2(1.0, 0.0)), next.x),
                 mix(Cell(first + vec2(0.0, 1.0)), Cell(first + vec2(1.0, 1.0)), next.x), next.y);

    // The lines between cells: as wide as asked but at least an output pixel, and paler
    // when drawn wider, so they take the same light away.
    float fine = clamp(least - 2.0, 0.0, 1.0);
    float gap = 0.16 * p_grid;
    float drawn = max(gap, min(1.0 / least, 0.4));
    float lit_asked = (1.0 - gap) * (1.0 - gap), lit_drawn = (1.0 - drawn) * (1.0 - drawn);
    float strength = fine * (1.0 - lit_asked) / max(1.0 - lit_drawn, 0.001);
    vec2 lit = Lit(t, w, drawn);
    float open = lit_asked + strength * (lit.x * lit.y - lit_drawn);
    // Lit cells are a little brighter than the plain picture, the lines much darker.
    c *= (1.0 - 0.8 * (1.0 - open)) / (1.0 - 0.5 * (1.0 - lit_asked));

    // Three stripes a cell, as a wave that an output pixel averages; they need about five
    // output pixels a cell to show.
    float narrow = PI * w.x;
    float stripes = 0.7 * p_subpixel * clamp((room.x - 3.0) / 2.0, 0.0, 1.0) * sin(narrow) / narrow;
    vec3 phase = 2.0 * PI * (fract(t.x) - vec3(1.0, 3.0, 5.0) / 6.0);
    c *= 1.0 + stripes * cos(phase);

    vec2 q = abs(v_uv * 2.0 - 1.0);
    float leak = 0.02 + 0.1 * pow(v_uv.y, 5.0) + 0.07 * pow(max(q.x, q.y), 10.0);
    leak *= 0.85 + 0.15 * sin(v_uv.x * 9.0 + 1.3) * sin(v_uv.y * 5.0 + 0.4);
    c += p_bleed * leak * vec3(0.8, 0.9, 1.0) * (1.0 - Luma(c));
    o_color = vec4(ToGamma(c * p_bright), 1.0);
}
