---
publish: true
title: "Aliases Demo"
tags: [demo, aliases, naming]
created: "2026-06-15"
aliases: ["AD", "Alias Demo Note", "Short Name"]
graph:
  shape: cylinder
  color: "#8e44ad"
---

# Aliases Demo

This note has multiple **aliases** in frontmatter:

```yaml
aliases: ["AD", "Alias Demo Note", "Short Name"]
```

## How Aliases Work
- Display as `@AD`, `@Alias Demo Note`, `@Short Name` badges in detail panel
- Click an alias badge to navigate (same note)
- Useful for: short names, alternative titles, acronyms

## Wikilink with Alias
Link to this note using custom text:
- `[[Aliases Demo|Custom Link Text]]` → shows as "Custom Link Text"

## Related
- [[Ghost Node Target]]  # This creates a ghost node (unpublished)
- [[Normal Note]]