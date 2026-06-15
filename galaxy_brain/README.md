# GalaxyBrain for Hermes Agent

A 3D interactive knowledge graph visualization plugin for Hermes Agent, bringing your Obsidian vault to life as a navigable 3D constellation of notes and tags.

![GalaxyBrain](https://github.com/ohrbit/hermes_plugins/blob/main/galaxy_brain/docs/images/hero.jpg)

## Human Notes
This is entierly written by nemotron3-ultra:free with the hermes-agent harness and slight customiziation but basicly out of the box via telegram chat.
i actually have no idea what im doing and the process workflow was so lala, 3h to create it, 1 bug and we went on a 18h library hunt.. part my fault, i could have used versioning and roll back but yeah, in the end its working. some features have bugs and are not working and i dont think the model will be able to do it in time.
ive contacted both of the original authors maybe they want to create a proper version of the plugin with all the fancy features of the original galaxy brain.

im currently testing googles new [okf](https://github.com/GoogleCloudPlatform/knowledge-catalog/tree/main/okf) integration

## Features

### ✅ Implemented
- **3D Force-Directed Graph** — Notes and tags rendered as nodes in 3D space with physics-based layout (490+ nodes supported)
- **Collapsible Tag Clusters** — Click any tag to collapse/expand its connected notes
- **Interactive Navigation** — Zoom (scroll), pan (left-drag), rotate (right-drag) with native OrbitControls
- **Note Detail View** — Click a note to view rendered Markdown with wikilinks, code blocks, tables, and callouts
- **Multi-Vault Support** — Switch between multiple Obsidian vaults via dropdown
- **Dark/Light Theme** — Auto-syncs with Hermes dashboard theme, persisted in localStorage
- **Welcome Callout** — First-time user guidance with dismissible overlay
- **Responsive Canvas** — Fixed 600px height, full-width, works in sidebar and full-screen
- **3D Node Shapes** — 7 geometric shapes per note frontmatter: sphere, box, cylinder, cone, torus, torusknot, dodecahedron (tags = octahedron)
- **Pinned Nodes** — `pinned: true` notes always visible, never hidden by collapsible logic
- **Ghost Nodes** — Unlinked/uncreated notes shown as wireframe spheres with "+" sprite
- **Tag Highlighting (NEW)** — URL param `?highlight=tag` pulses the tag with animated point light, scales connected notes 1.35×, dims others to 15% opacity
- **Deep-Link Focus (NEW)** — URL param `?focus=note-id` auto-flies camera to node after sim settles
- **Shift+Right-Click Focus (NEW)** — Hold Shift + right-click any node to smoothly fly camera to it (1200ms)

### 🚧 Planned / In Progress
- **Animated Callout Arrow** — Pulsing "start here" arrow pointing to `callout: true` node on first load
- **Collapsed Node "+" Sprite** — Floating plus badge on collapsible cluster parents (visual indicator)
- **Hover Preview Panel** — Rich bottom-left panel with excerpt, node type, click hints (expand/navigate)
- **IntersectionObserver Lazy Init** — Defer Three.js initialization until graph scrolls into viewport
- **WebGL Cleanup on Unmount** — Proper renderer disposal to prevent GPU context leaks
- **LocalGraph Progressive Expansion** — Per-note neighborhood graph with 1-hop + lazy load on demand
- **Tag Hierarchy Links** — Visual `tag-hierarchy` edges between parent/child tags
- **Backlinks/Forward Links Sidebar** — In LocalGraph: lists of notes linking to/from current note
- **Ghost Node Filter Toggle** — Option to hide/unlink ghost nodes to reduce visual noise
- **Shape Legend UI** — Visual key for the 7 node shapes + tag/ghost types
- **URL State Persistence** — Sync camera position, collapsed state, highlight to URL for shareable links

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  GalaxyBrain Plugin (dashboard/dist/index.js)               │
├─────────────────────────────────────────────────────────────┤
│  • GalaxyBrainPlugin — Main component, vault/graph loading  │
│  • Graph3D — 3D graph wrapper component                     │
│  • react-force-graph-3d (UMD) — React component, bundles    │
│    Three.js + OrbitControls                                 │
├─────────────────────────────────────────────────────────────┤
│  API Endpoints (provided by hermes-obsidian-memory)         │
│  • GET /api/plugins/galaxy-brain/vaults                     │
│  • GET /api/plugins/galaxy-brain/graph?vault=<name>         │
│  • GET /api/plugins/galaxy-brain/node/:id?vault=<name>      │
└─────────────────────────────────────────────────────────────┘
```

## Installation

### Prerequisites

- Hermes Agent with dashboard enabled
- Obsidian vault(s) with published notes
- `hermes-obsidian-memory` plugin installed and configured

### Install GalaxyBrain

```bash
# From the Hermes Agent root directory
mkdir -p ~/.hermes/plugins/galaxy-brain/dashboard/static/libs
```

Copy the plugin files:

```
~/.hermes/plugins/galaxy-brain/
├── plugin.yaml
├── dashboard/
│   ├── plugin.js          # Development source
│   ├── dist/
│   │   └── index.js       # Served by dashboard (ES5)
│   └── static/
│       └── libs/
│           ├── react-force-graph-3d.umd.min.js
│           └── three.min.js (optional, bundled in UMD)
```

### Download Required Libraries

```bash
cd ~/.hermes/plugins/galaxy-brain/dashboard/static/libs

# react-force-graph-3d UMD (React component, includes Three.js + OrbitControls)
curl -sL "https://unpkg.com/react-force-graph-3d@1.29.1/dist/react-force-graph-3d.js" \
  -o react-force-graph-3d.umd.min.js
```

### Register Plugin

Add to your Hermes config or install via plugin manager.

## Usage

1. **Open Dashboard** → Navigate to the GalaxyBrain tab
2. **Select Vault** — Use dropdown if multiple vaults exist
3. **Explore Graph** — 490+ nodes render with shapes: notes (various geometries), tags (octahedra), ghosts (wireframe)
4. **Click Tag** — Collapses/expands its note cluster
5. **Click Note** — Opens detail view with full Markdown rendering
6. **Navigate** — Scroll to zoom, left-drag to pan, right-drag to rotate
7. **Toggle Theme** — Sun/moon button syncs with dashboard

### URL Parameters (NEW)

| Param | Example | Effect |
|-------|---------|--------|
| `?highlight=tag` | `?highlight=demo` | Pulses #demo tag, brightens 30 connected notes, dims others |
| `?focus=node-id` | `?focus=GalaxyBrain%20Features` | Auto-flies camera to node after 4s |
| Combined | `?highlight=shapes&focus=Shape%20-%20Sphere` | Both effects together |

### Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Shift + Right-click | Focus camera on node (smooth fly) |
| Left-drag | Pan camera |
| Right-drag | Orbit/rotate camera |
| Scroll | Zoom in/out |
| Click tag | Collapse/expand cluster |
| Click note | Open detail view |

## Note Frontmatter

Notes appear in the graph when they have `publish: true`:

```markdown
---
publish: true
title: "My Note"
tags: [topic, subtopic]
graph:
  shape: sphere        # sphere | box | cylinder | cone | torus | torusknot | dodecahedron
  color: "#3498db"     # hex color
  pinned: true         # always visible, never collapsible
  collapsible: true    # tag-like behavior (collapse children)
  callout: "Welcome!"  # show callout on this node
---
# My Note

Content with [[wikilinks]] and **markdown**.
```

| Property | Type | Description |
|----------|------|-------------|
| `publish` | boolean | **Required** — include note in graph |
| `title` | string | Display name (defaults to filename) |
| `tags` | string[] | Tags for clustering |
| `graph.shape` | string | Layout shape for this node's cluster (7 options) |
| `graph.color` | string | Node color (hex) |
| `graph.pinned` | boolean | Never collapse, always visible |
| `graph.collapsible` | boolean | Act as tag cluster head |
| `graph.callout` | string | Show welcome message on hover |

## Configuration

### Environment Variables

```bash
# Optional: override primary Obsidian vault path
export OBSIDIAN_VAULT_PATH=/path/to/your/vault
```

### Dashboard Plugin Config

The plugin auto-discovers vaults from `hermes-obsidian-memory`. No additional config needed.

## Development

### Source Structure

```
dashboard/
├── plugin.js          # ES5 source (edit this)
├── dist/
│   └── index.js       # Served version (copy of plugin.js)
```

### Building

The dashboard serves `dist/index.js` directly. After editing `plugin.js`:

```bash
cp dashboard/plugin.js dashboard/dist/index.js
# Restart dashboard
hermes dashboard --no-open --skip-build --host 0.0.0.0
```

### Hard Refresh Required

After each deploy, hard refresh browser: `Ctrl+Shift+R` / `Cmd+Shift+R`

## API Integration

GalaxyBrain expects these endpoints from `hermes-obsidian-memory`:

### `/vaults`
```json
[
  { "name": "vault-name", "path": "/path/to/vault", "published_count": 42 }
]
```

### `/graph?vault=<name>`
```json
{
  "nodes": [
    { "id": "note-name", "name": "Display Name", "type": "file", "color": "#3498db", "shape": "sphere", "tags": ["tag1"], "frontmatter": {...} },
    { "id": "tag:tag1", "name": "#tag1", "type": "tag", "color": "#FF6B6B", "shape": "octahedron", "collapsible": true }
  ],
  "links": [
    { "source": "note-name", "target": "tag:tag1" }
  ]
}
```

### `/node/:id?vault=<name>`
```json
{
  "id": "note-name",
  "title": "Display Name",
  "content": "# Markdown content\nWith [[wikilinks]]",
  "tags": ["tag1"],
  "frontmatter": { "graph": { "color": "#3498db", "shape": "sphere" } }
}
```

## Troubleshooting

### Graph Not Rendering
- Check console for `[galaxy-brain]` logs
- Verify `window.ForceGraph3D: object THREE: object` appears (React component + Three.js)
- Hard refresh browser (Ctrl+Shift+R)

### Only One Node Visible
- Check `visibleNodes` logic in console
- Should show: `[galaxy-brain] No collapsed tags, showing all 490 nodes`

### Zoom/Pan Not Working
- Ensure `enableNavigationControls: true` in ForceGraph3D props
- Use scroll (zoom), left-drag (pan), right-drag (rotate)

### Library Load Errors
- Verify `react-force-graph-3d.umd.min.js` exists in `static/libs/`
- Check Network tab for 404s on `/api/plugins/galaxy-brain/static/libs/...`

### Highlight Not Working
- Must navigate WITH URL param (e.g., `?highlight=demo`) — refresh with param
- Check console for `[galaxy-brain]` highlightTag detection
- Tag ID format: `tag:your-tag-name` (check node IDs in graph API)

### Right-Click Not Focusing
- Use **Shift + Right-click** (prevents conflict with OrbitControls right-drag rotate)

## Credits

- **react-force-graph-3d** by [vasturiano](https://github.com/vasturiano/react-force-graph-3d)
- **GalaxyBrain** original by [nerd-sniped](https://github.com/nerd-sniped/GalaxyBrain)
- **hermes-obsidian-memory** by [kyssta-exe](https://github.com/kyssta-exe/hermes-obsidian-memory)
- Built for **Hermes Agent** by [Nous Research](https://nousresearch.com)

## License

MIT — Free for personal and commercial use.
