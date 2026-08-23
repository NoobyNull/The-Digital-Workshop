// Digital Workshop v0.7 canonical beginner-journey fixture.
// Generate with variant=0 (Primary), 1 (Alternate), or 2 (Preview Only).

$fn = 48;
variant = is_undef(variant) ? 0 : variant;

module rounded_rect(width, height, radius) {
    offset(r = radius)
        square([width - radius * 2, height - radius * 2], center = true);
}

module plaque() {
    difference() {
        linear_extrude(height = 3)
            rounded_rect(120, 60, 5);

        for (x = [-50, 50]) {
            translate([x, 0, -0.1])
                cylinder(h = 3.2, r = 2.5);
        }
    }
}

module rim_2d() {
    difference() {
        rounded_rect(116, 56, 4);
        rounded_rect(108, 48, 3);
    }
}

module wave_2d(y, phase) {
    for (i = [-4 : 3]) {
        hull() {
            translate([i * 10, y + sin(i * 45 + phase) * 3])
                circle(r = 1.5);
            translate([(i + 1) * 10, y + sin((i + 1) * 45 + phase) * 3])
                circle(r = 1.5);
        }
    }
}

module primary_relief_2d() {
    wave_2d(8, 0);
    wave_2d(0, 35);
    wave_2d(-8, 70);
}

module alternate_relief_2d() {
    polygon(points = [[-42, -14], [-25, 12], [-9, -14]]);
    polygon(points = [[-20, -14], [2, 18], [24, -14]]);
    polygon(points = [[12, -14], [31, 10], [44, -14]]);
    translate([35, 15]) circle(r = 4);
}

module preview_only_relief_2d() {
    hull() {
        translate([-12, 0]) circle(r = 8);
        translate([15, 0]) circle(r = 5);
    }
    polygon(points = [[-17, 0], [-34, 13], [-31, 0], [-34, -13]]);
    translate([10, 2]) circle(r = 1.2);
    translate([30, 11]) circle(r = 2.2);
    translate([38, 16]) circle(r = 1.5);
}

union() {
    plaque();

    translate([0, 0, 3])
        linear_extrude(height = 1.2)
            rim_2d();

    translate([0, 0, 3])
        linear_extrude(height = 1.5) {
            if (variant == 0)
                primary_relief_2d();
            else if (variant == 1)
                alternate_relief_2d();
            else
                preview_only_relief_2d();
        }
}
