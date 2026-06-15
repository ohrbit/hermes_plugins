---
publish: true
title: "Ghost Node Demo"
tags: [demo, ghost, unpublished]
created: "2026-06-15"
graph:
  shape: torus
  color: "#7f8c8d"
---

# Ghost Node Demo

This note references an **unpublished note** creating a **ghost node**.

## Ghost Nodes
Ghost nodes appear as **white spheres** with thin borders for notes that are:
- Referenced via `[[Wikilink]]` but NOT published (`publish: true`)
- Linked from published notes but don't exist in vault

## Example
This note links to: `[[Unpublished Secret Note]]`

Since "Unpublished Secret Note" doesn't have `publish: true` in its frontmatter (or doesn't exist), it appears as a **ghost node** in the graph.

## Ghost Node Properties
- White color (`#ffffff`)
- Sphere shape
- Small size (val: 6)
- Label shows the target name
- Connected via normal link lines

## Try It
Create a new note without `publish: true` and link to it from here - it'll appear as a ghost!