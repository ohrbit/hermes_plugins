---
publish: true
title: "Tag Hierarchy Demo"
tags: [demo, hierarchy, parent/child, parent/child/grandchild, standalone]
created: "2026-06-15"
graph:
  shape: cone
  color: "#16a085"
---

# Tag Hierarchy Demo

This note demonstrates **nested tags** using `/` separator:

```yaml
tags: [demo, hierarchy, parent/child, parent/child/grandchild, standalone]
```

## Tag Structure Created
```
demo
hierarchy
parent
  └── child
        └── grandchild
standalone
```

## Visual in Graph
- Each tag becomes an **octahedron node**
- Hierarchy creates **parent → child links** (dashed lines)
- Click a parent tag to highlight all descendants

## Related Notes by Tag
- [[Tag Hierarchy Child]] - shares `parent/child`
- [[Another Grandchild]] - shares `parent/child/grandchild`