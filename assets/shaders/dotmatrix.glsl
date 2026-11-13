// A reflective dot matrix screen in one colour: square dots that only get darker, with
// gaps between them, lying a little above the surface they throw their shadow on. The two
// colours (p_light_*, p_dark_*) make it the green of the first handheld or the grey of
// the pocket one.
uniform float p_grid;
uniform float p_shadow;
uniform float p_contrast;
uniform float p_light_r;
uniform float p_light_g;
uniform float p_light_b;
uniform float p_dark_r;
uniform float p_dark_g;
uniform float p_dark_b;

// How dark the dot of a pixel is: 0 off, 1 fully on.
float Ink(vec2 cell) {
    if (cell.x < 0.0 || cell.y < 0.0 || cell.x >= u_source_size.x || cell.y >= u_source_size.y) {
        return 0.0;
    }
    float dark = 1.0 - Luma(Source((cell + 0.5) / u_source_size).rgb);
    return clamp((dark - 0.5) * p_contrast + 0.5, 0.0, 1.0);
}

// How much of the dot covers a point of its cell (f in 0..1).
float Dot(vec2 f, float room) {
    vec2 d = abs(f - 0.5) * 2.0;
    float edge = 1.0 - 0.3 * p_grid;
    float soft = min(1.5 / room, 0.4);
    vec2 a = vec2(1.0) - smoothstep(vec2(edge - soft), vec2(edge + 0.02), d);
    return a.x * a.y;
}

void main() {
    vec2 t = v_uv * u_source_size;
    float room = u_output_size.y / u_source_size.y; // output pixels per dot
    // Under about three there is nothing to draw gaps with.
    float fine = clamp(room - 2.0, 0.0, 1.0);

    // A dot that is off still shows faintly.
    float ink = 0.04 + 0.96 * Ink(floor(t));
    float cover = mix(1.0, Dot(fract(t), room), fine);
    vec2 ts = t - vec2(0.14, 0.2);
    float shadow = Ink(floor(ts)) * mix(1.0, Dot(fract(ts), room), fine);

    vec3 light = vec3(p_light_r, p_light_g, p_light_b);
    vec3 dark = vec3(p_dark_r, p_dark_g, p_dark_b);
    vec3 c = light * (1.0 - 0.3 * p_shadow * shadow * fine);
    c = mix(c, dark, ink * cover);
    vec2 q = v_uv * 2.0 - 1.0;
    c *= 1.0 - 0.06 * dot(q, q);
    o_color = vec4(c, 1.0);
}
