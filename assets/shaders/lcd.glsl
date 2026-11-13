// A colour handheld screen: every pixel a cell of three colour stripes with dark lines
// around it, the duller colours of those panels, and (p_bleed) the light of a backlight
// leaking in from the edges where the picture should be black.
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

float Stripe(float d) {
    return 1.0 - smoothstep(0.12, 0.4, abs(d));
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 f = fract(t);
    vec3 c = ToLinear(Source((floor(t) + 0.5) / u_source_size).rgb);
    c = mix(c, Panel(c), p_color);

    vec2 room = u_output_size / u_source_size; // output pixels per cell
    float fine = clamp(min(room.x, room.y) - 2.0, 0.0, 1.0);
    vec2 edge = min(f, 1.0 - f);
    vec2 lit = smoothstep(vec2(0.0), vec2(0.08) + 0.75 / room, edge);
    c *= mix(1.0, lit.x * lit.y, p_grid * fine) * mix(1.0, 1.12, p_grid * fine);

    float stripes = p_subpixel * clamp((room.x - 3.0) / 2.0, 0.0, 1.0);
    vec3 sub = vec3(Stripe(f.x - 1.0 / 6.0), Stripe(f.x - 0.5), Stripe(f.x - 5.0 / 6.0));
    c *= mix(vec3(1.0), sub * 1.75, stripes);

    vec2 q = abs(v_uv * 2.0 - 1.0);
    float leak = 0.02 + 0.1 * pow(v_uv.y, 5.0) + 0.07 * pow(max(q.x, q.y), 10.0);
    leak *= 0.85 + 0.15 * sin(v_uv.x * 9.0 + 1.3) * sin(v_uv.y * 5.0 + 0.4);
    c += p_bleed * leak * vec3(0.8, 0.9, 1.0) * (1.0 - Luma(c));
    o_color = vec4(ToGamma(c * p_bright), 1.0);
}
