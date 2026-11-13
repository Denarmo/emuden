// A reflective dot matrix screen in one colour: dots that only get darker, with gaps
// between them, lying a little above the surface they throw their shadow on.
//
// Everything is measured in the picture's own pixels (cells), and every output pixel takes
// the share of each dot that falls inside it, so the dots are as even at 7.5 output pixels
// a cell as at 8, and the picture is as bright at any size: the gaps are never thinner than
// an output pixel (they get paler instead of narrower), and under about three output
// pixels a cell the dots give way to their average.
uniform float p_palette; // 1 green, 2 olive, 3 pocket grey, 4 light (blue-green)
uniform float p_grid;    // the gaps: 0 none .. 1 a quarter of the cell
uniform float p_round;   // 0 square dots .. 1 round ones
uniform float p_shadow;
uniform float p_unlit;   // how much a dot that is off still shows
uniform float p_contrast;

// The surface behind the dots.
vec3 Paper() {
    if (p_palette < 1.5) {
        return vec3(0.62, 0.74, 0.10);
    }
    if (p_palette < 2.5) {
        return vec3(0.68, 0.71, 0.42);
    }
    if (p_palette < 3.5) {
        return vec3(0.78, 0.80, 0.74);
    }
    return vec3(0.38, 0.82, 0.68);
}

// A dot that is fully on.
vec3 Dark() {
    if (p_palette < 1.5) {
        return vec3(0.06, 0.22, 0.06);
    }
    if (p_palette < 2.5) {
        return vec3(0.15, 0.22, 0.15);
    }
    if (p_palette < 3.5) {
        return vec3(0.10, 0.11, 0.11);
    }
    return vec3(0.02, 0.17, 0.17);
}

// How dark the dot of a pixel is: 0 off, 1 fully on.
float Ink(vec2 cell) {
    if (cell.x < 0.0 || cell.y < 0.0 || cell.x >= u_source_size.x || cell.y >= u_source_size.y) {
        return 0.0;
    }
    float dark = 1.0 - Luma(Source((cell + 0.5) / u_source_size).rgb);
    return clamp((dark - 0.5) * p_contrast + 0.5, 0.0, 1.0);
}

// The share of the stretch `w` around x that lies within `h` of zero, on each axis.
vec2 Share(vec2 x, vec2 w, float h) {
    return clamp((min(x + 0.5 * w, vec2(h)) - max(x - 0.5 * w, vec2(-h))) / w, 0.0, 1.0);
}

// The share of an output pixel (`w` cells wide, `p` from the dot's middle) a dot covers.
float Cover(vec2 p, vec2 w, float half_size, float radius) {
    vec2 box = Share(p, w, half_size);
    vec2 d = abs(p) - (half_size - radius);
    float away = length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - radius;
    return min(box.x * box.y, clamp(0.5 - away / max(w.x, w.y), 0.0, 1.0));
}

// The area of a dot, in cells.
float Area(float half_size, float radius) {
    return 4.0 * half_size * half_size - (4.0 - PI) * radius * radius;
}

void main() {
    vec2 t = v_uv * u_source_size;
    vec2 room = u_output_size / u_source_size; // output pixels per cell
    float least = min(room.x, room.y);
    vec2 w = Footprint(room);
    float fine = clamp(least - 2.0, 0.0, 1.0);

    // The gap as asked for, and as drawn: at least one output pixel. A gap drawn wider
    // than asked is made paler by `strength`, so that it takes the same light away.
    float gap = 0.25 * p_grid;
    float drawn = max(gap, min(1.0 / least, 0.4));
    float half_asked = 0.5 - 0.5 * gap, half_drawn = 0.5 - 0.5 * drawn;
    float area_asked = Area(half_asked, p_round * half_asked);
    float area_drawn = Area(half_drawn, p_round * half_drawn);
    float strength = fine * (1.0 - area_asked) / max(1.0 - area_drawn, 0.001);
    float radius = p_round * half_drawn;

    // The four cells an output pixel can touch.
    vec2 first = floor(t - 0.5);
    vec2 soft = w + 0.3;
    vec2 lifted = t - vec2(0.1, 0.14);
    vec2 first_lifted = floor(lifted - 0.5);
    float shadow = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 cell = first_lifted + vec2(float(i % 2), float(i / 2));
        shadow += Ink(cell) * Cover(lifted - cell - 0.5, soft, half_drawn, radius);
    }
    vec3 paper = Paper() * (1.0 - 0.3 * p_shadow * shadow);
    vec3 dark = Dark();

    // In linear light, or the soft edges would come out too dark.
    vec3 sum = vec3(0.0);
    float covered = 0.0;
    for (int i = 0; i < 4; i++) {
        vec2 cell = first + vec2(float(i % 2), float(i / 2));
        vec2 p = t - cell - 0.5;
        vec2 own = Share(p, w, 0.5);
        float part = own.x * own.y;
        float cover = part * area_asked + strength * (Cover(p, w, half_drawn, radius) - part * area_drawn);
        float ink = p_unlit + (1.0 - p_unlit) * Ink(cell);
        vec3 shade = mix(paper, dark, ink);
        sum += cover * shade * shade;
        covered += cover;
    }
    vec3 c = sqrt(sum + (1.0 - covered) * paper * paper);
    vec2 q = v_uv * 2.0 - 1.0;
    c *= 1.0 - 0.06 * dot(q, q);
    o_color = vec4(c, 1.0);
}
