---
name: Release ZIP structure
description: How to package the ZIP for GitHub releases
type: project
---

When creating a GitHub release ZIP, do NOT include the full build path.
The ZIP root must be `Next Timer.pak/` directly.

**How:** cd into `build/tg5040` before zipping, e.g.:
```bash
cd build/tg5040 && zip -r "../../Next Timer.zip" "Next Timer.pak"
```

**Why:** The previous v1.0 release accidentally included `build/tg5040/` in the ZIP path. Users should extract and get `Next Timer.pak/` at the top level, ready to drop into the `Tools` folder.
