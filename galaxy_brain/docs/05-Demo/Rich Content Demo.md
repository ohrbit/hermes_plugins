---
publish: true
title: "Rich Content Demo"
tags: [demo, rich-content, markdown, rendering]
created: "2026-06-15"
graph:
  shape: dodecahedron
  color: "#e91e63"
---

# Rich Content Demo

Shows all Markdown rendering features in the detail panel.

## Headers
### H3 Header
#### H4 Header
##### H5 Header

## Text Formatting
**Bold text** and *italic text* and ***bold italic***
`Inline code` with purple background
~~Strikethrough~~ (if supported)

## Lists

### Unordered
- First item
- Second item
  - Nested item
  - Another nested
- Third item

### Ordered
1. First step
2. Second step
3. Third step

### Task Lists
- [x] Completed task
- [ ] Pending task
- [ ] Another pending

## Code Blocks

### Python
```python
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n-1) + fibonacci(n-2)

print(fibonacci(10))
```

### JavaScript
```javascript
const greet = (name) => {
    console.log(`Hello, ${name}!`);
};

greet("GalaxyBrain");
```

### Bash
```bash
# Server commands
ssh user@host
systemctl status nginx
journalctl -u ssh -f
```

### Plain Text
```
No language specified
Just monospace text
```

## Blockquotes
> **Note:** Blockquotes render with a left border
>> Nested blockquote
>>> Deep nesting

> **Tip:** Use for callouts, warnings, or quotes

## Tables

| Feature | Supported | Notes |
|---------|:---------:|-------|
| Headers | ✅ | H1-H6 |
| Lists | ✅ | Ordered/Unordered |
| Tasks | ✅ | Checkboxes |
| Code | ✅ | Syntax highlight |
| Tables | ✅ | Pipes align |
| Quotes | ✅ | Nested support |
| Links | ✅ | Wikilinks + URLs |

## Horizontal Rule

---

## Wikilinks
- [[Normal Note]] - internal link
- [[Aliases Demo|Custom Label]] - with alias
- [[Non Existent Note]] - creates ghost node

## External Links
- [GalaxyBrain GitHub](https://github.com/nerd-sniped/GalaxyBrain)
- [Obsidian](https://obsidian.md)

## Images (Wikilink Embed)
> Note: `![[image.png]]` syntax renders as embedded image

---

**All rendering happens client-side** in the detail panel - no server processing needed!