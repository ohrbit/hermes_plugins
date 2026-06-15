---
publish: true
title: "GalaxyBrain Features"
tags: [demo, features, showcase]
created: "2026-06-15"
updated: "2026-06-15"
graph:
  shape: dodecahedron
  color: "#9b59b6"
  callout: true
  calloutText: "Start here - GalaxyBrain feature showcase"
---

# GalaxyBrain Features Showcase

This note demonstrates all the visual features available in GalaxyBrain.

## Node Shapes

Different `graph.shape` values in frontmatter:

| Shape | Frontmatter Value | Visual |
|-------|-------------------|--------|
| Sphere | `sphere` | Round ball (default) |
| Box | `box` | Cube |
| Cylinder | `cylinder` | Tube |
| Cone | `cone` | Pointed cone |
| Dodecahedron | `dodecahedron` | 12-faced polyhedron |
| Torus | `torus` | Donut |
| Torus Knot | `torusknot` | Twisted torus |
| Octahedron | `octahedron` | 8-faced (used for tags) |

## Node Colors

Set `graph.color` to any valid hex color:

```yaml
graph:
  color: "#e74c3c"  # Red
  color: "#3498db"  # Blue
  color: "#2ecc71"  # Green
  color: "#f39c12"  # Orange
  color: "#9b59b6"  # Purple
  color: "#1abc9c"  # Teal
```

## Special Properties

### Pinned Nodes
```yaml
graph:
  pinned: true
```
Pinned nodes **never collapse** when their tag is collapsed. They stay visible as anchor points.

### Collapsible Nodes
```yaml
graph:
  collapsible: true
```
Click the node in the graph to **collapse/expand** its connected notes. Great for organizing large clusters.

### Callout Nodes
```yaml
graph:
  callout: true
  calloutText: "Custom welcome message"
```
Shows a centered welcome callout when the graph loads. Only one callout node should have this enabled.

## Tags & Hierarchy

Tags create **tag nodes** (octahedrons) automatically:

```yaml
tags: [parent/child, parent/grandchild, standalone]
```

- `parent/child` creates hierarchy: parent tag → child tag
- Click a tag node to highlight all connected notes
- Tags with `/` nest automatically

## Wikilinks

Connect notes with `[[Wikilinks]]`:

- `[[Another Note]]` - links to note by title
- `[[Another Note|Custom Label]]` - custom display text
- Unpublished targets become **ghost nodes** (white spheres)

## Aliases

```yaml
aliases: ["Short Name", "Alternative Title"]
```

Shows as `@Short Name` badges in the detail panel. Clicking an alias navigates to the note.

## Content Rendering

The detail panel renders full Markdown:

### Code Blocks
```python
def hello():
    print("Syntax highlighting works!")
```

### Checkboxes
- [x] Completed task
- [ ] Pending task

### Blockquotes
> GalaxyBrain renders blockquotes with a left border

### Tables
| Feature | Status |
|---------|--------|
| Shapes | ✅ |
| Colors | ✅ |
| Tags | ✅ |

---

**Explore the other demo notes** to see each feature in action!