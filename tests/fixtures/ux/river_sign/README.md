# River Sign UX Fixture

This project-authored fixture supplies three visibly different, low-complexity
STL designs for the v0.7 beginner journey. It avoids relying on the user's model
library or on assets with unknown licensing.

Regenerate the committed binary STL files with:

```bash
openscad -q --export-format binstl -D 'variant=0' \
  -o river_sign_primary.stl river_sign_fixture.scad
openscad -q --export-format binstl -D 'variant=1' \
  -o river_sign_alternate.stl river_sign_fixture.scad
openscad -q --export-format binstl -D 'variant=2' \
  -o river_sign_preview_only.stl river_sign_fixture.scad
```

The variants are intentionally named by task role rather than artistic quality:

- Primary: three river bands;
- Alternate: mountain relief;
- Preview Only: fish relief that must never enter the project during the preview task.

Dimensions are 120 x 60 mm with a 3 mm plaque, a 1.2 mm border, and relief up to
1.5 mm above the plaque. Use Virtual CNC only during the canonical task.
