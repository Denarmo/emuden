// Smooth edges: pixel art with its stair steps turned into straight edges. For every
// corner of a pixel it compares how much the picture changes along the diagonal through
// that corner with how much it changes across it; where the diagonal is the calmer way an
// edge runs there, and the corner takes the colour of the pixels beyond it. Steps of two
// pixels are cut along their own slope, so shallow and steep lines come out straight too.
// Corners that are meant to be corners, single pixels and dither patterns are kept: an
// edge needs the far side to agree with it, a pixel of a checker has no edge at all, and
// a single pixel standing on an edge is left standing.
//
// The neighbours of the pixel e, as they are named below:
//
//         a1 b1 c1
//      a0 a  b  c  c4
//      d0 d  e  f  f4
//      g0 g  h  i  i4
//         g5 h5 i5
uniform float p_tolerance;

vec3 Pixel(vec2 cell) {
    return Source((cell + 0.5) / u_source_size).rgb;
}

float Apart(vec3 a, vec3 b) {
    vec3 d = abs(a - b);
    return d.r * 0.3 + d.g * 0.6 + d.b * 0.1 + max(d.r, max(d.g, d.b)) * 0.5;
}

bool Alike(vec3 a, vec3 b) {
    return Apart(a, b) < p_tolerance;
}

// The corner of e between f (beside it) and h (below it), with i past the corner; the
// other names are the neighbours as seen from that corner. `p` is the place in e, from
// its middle, with the corner at (0.5, 0.5). Gives how far past the cut the place is, in
// pixels (negative: e keeps it), and the colour past the cut.
float Corner(vec2 p, vec3 e, vec3 f, vec3 h, vec3 i, vec3 b, vec3 d, vec3 c, vec3 g, vec3 f4, vec3 i4,
             vec3 h5, vec3 i5, out vec3 colour) {
    colour = e;
    if (Alike(e, f) || Alike(e, h)) {
        return -1.0;
    }
    // A pixel of a checker pattern: both diagonals through it are its own colour.
    if (Alike(e, i) && Alike(e, c) && Alike(e, g)) {
        return -1.0;
    }
    // A tooth one pixel wide on a straight edge (f or h standing on i, with e's colour on
    // its other sides) is a pattern, not a step: the dots of a dither beside an outline.
    if ((Alike(i, f) && Alike(f4, e) && Alike(c, e)) || (Alike(i, h) && Alike(h5, e) && Alike(g, e))) {
        return -1.0;
    }
    float along = Apart(e, c) + Apart(e, g) + Apart(i, h5) + Apart(i, f4) + 4.0 * Apart(h, f);
    float across = Apart(h, d) + Apart(h, i5) + Apart(f, i4) + Apart(f, b) + 4.0 * Apart(e, i);
    if (along >= across) {
        return -1.0;
    }
    bool wanted = (!Alike(f, b) && !Alike(h, d)) || (Alike(e, i) && !Alike(f, i4) && !Alike(h, i5)) ||
                  Alike(e, g) || Alike(e, c);
    if (!wanted) {
        return -1.0;
    }
    colour = Apart(e, f) <= Apart(e, h) ? f : h;
    float past = (p.x + p.y - 0.5) * 0.7071;
    // A step two pixels long: the edge goes on to g (shallow) or to c (steep).
    float to_g = Apart(f, g), to_c = Apart(h, c);
    if (2.0 * to_g <= to_c && !Alike(e, g) && !Alike(d, g)) {
        past = max(past, (p.x + 2.0 * p.y - 0.5) * 0.4472);
    }
    if (2.0 * to_c <= to_g && !Alike(e, c) && !Alike(b, c)) {
        past = max(past, (2.0 * p.x + p.y - 0.5) * 0.4472);
    }
    return past;
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 cell = floor(t);
    vec2 p = fract(t) - 0.5;
    vec3 e = Pixel(cell);
    vec3 b = Pixel(cell + vec2(0.0, -1.0));
    vec3 d = Pixel(cell + vec2(-1.0, 0.0));
    vec3 f = Pixel(cell + vec2(1.0, 0.0));
    vec3 h = Pixel(cell + vec2(0.0, 1.0));
    if (Alike(e, b) && Alike(e, d) && Alike(e, f) && Alike(e, h)) {
        o_color = vec4(e, 1.0);
        return;
    }
    vec3 a = Pixel(cell + vec2(-1.0, -1.0));
    vec3 c = Pixel(cell + vec2(1.0, -1.0));
    vec3 g = Pixel(cell + vec2(-1.0, 1.0));
    vec3 i = Pixel(cell + vec2(1.0, 1.0));
    vec3 a1 = Pixel(cell + vec2(-1.0, -2.0));
    vec3 b1 = Pixel(cell + vec2(0.0, -2.0));
    vec3 c1 = Pixel(cell + vec2(1.0, -2.0));
    vec3 a0 = Pixel(cell + vec2(-2.0, -1.0));
    vec3 d0 = Pixel(cell + vec2(-2.0, 0.0));
    vec3 g0 = Pixel(cell + vec2(-2.0, 1.0));
    vec3 c4 = Pixel(cell + vec2(2.0, -1.0));
    vec3 f4 = Pixel(cell + vec2(2.0, 0.0));
    vec3 i4 = Pixel(cell + vec2(2.0, 1.0));
    vec3 g5 = Pixel(cell + vec2(-1.0, 2.0));
    vec3 h5 = Pixel(cell + vec2(0.0, 2.0));
    vec3 i5 = Pixel(cell + vec2(1.0, 2.0));

    // The four corners, each seen with the picture turned a quarter further.
    vec3 colour, other;
    float past = Corner(p, e, f, h, i, b, d, c, g, f4, i4, h5, i5, colour);
    float next = Corner(vec2(p.y, -p.x), e, h, d, g, f, b, i, a, h5, g5, d0, g0, other);
    if (next > past) {
        past = next;
        colour = other;
    }
    next = Corner(-p, e, d, b, a, h, f, g, c, d0, a0, b1, a1, other);
    if (next > past) {
        past = next;
        colour = other;
    }
    next = Corner(vec2(-p.y, p.x), e, b, f, c, d, h, a, i, b1, c1, f4, c4, other);
    if (next > past) {
        past = next;
        colour = other;
    }
    // The cut is one output pixel soft.
    float soft = 0.5 * max(u_source_size.x / u_output_size.x, u_source_size.y / u_output_size.y);
    o_color = vec4(mix(e, colour, smoothstep(-soft, soft, past)), 1.0);
}
