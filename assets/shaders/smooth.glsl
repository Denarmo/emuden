// Smooth edges: pixel art with its stair steps cut along the diagonal. For every corner of
// a pixel it asks whether the two neighbours beside that corner agree with each other and
// not with the pixel; then an edge runs across the corner, and the corner takes their
// colour, with the cut one output pixel soft.
uniform float p_tolerance;

vec3 Pixel(vec2 cell) {
    return Source((cell + 0.5) / u_source_size).rgb;
}

bool Alike(vec3 a, vec3 b) {
    vec3 d = abs(a - b);
    return d.r * 0.3 + d.g * 0.6 + d.b * 0.1 + max(d.r, max(d.g, d.b)) * 0.5 < p_tolerance;
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 cell = floor(t);
    vec2 f = fract(t) - 0.5;
    vec2 side = vec2(f.x < 0.0 ? -1.0 : 1.0, f.y < 0.0 ? -1.0 : 1.0);
    vec3 here = Pixel(cell);
    vec3 beside = Pixel(cell + vec2(side.x, 0.0));
    vec3 above = Pixel(cell + vec2(0.0, side.y));
    vec3 across = Pixel(cell + side);
    vec3 c = here;
    if (Alike(beside, above) && !Alike(here, beside) && !Alike(here, across)) {
        // A line one pixel wide, or a dither pattern, keeps its shape: the neighbours on
        // the far sides must not be part of the same edge.
        vec3 far_beside = Pixel(cell - vec2(side.x, 0.0));
        vec3 far_above = Pixel(cell - vec2(0.0, side.y));
        if (!Alike(beside, far_beside) && !Alike(above, far_above)) {
            float past = abs(f.x) + abs(f.y) - 0.5;
            float soft = 0.75 * u_source_size.y / u_output_size.y;
            c = mix(here, 0.5 * (beside + above), smoothstep(-soft, soft, past));
        }
    }
    o_color = vec4(c, 1.0);
}
