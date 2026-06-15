"""GalaxyBrain plugin API — 3D knowledge graph for Obsidian vaults."""

import json
import os
import re
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, HTTPException, Query
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

router = APIRouter(tags=["galaxy-brain"])

# ─── Static files ─────────────────────────────────────────────────────
_LIBS_DIR = Path(__file__).parent / "dist" / "libs"

@router.get("/static/libs/{filename}")
async def serve_lib(filename: str):
    """Serve Three.js and ForceGraph3D libraries."""
    file_path = _LIBS_DIR / filename
    if not file_path.exists() or not file_path.is_file():
        raise HTTPException(status_code=404, detail="Not found")
    return FileResponse(file_path)


# ─── Vault discovery ────────────────────────────────────────────────

_PRIMARY = Path(os.environ.get("OBSIDIAN_VAULT_PATH", "/root/obsidian-vault"))


def _discover_vaults() -> dict[str, Path]:
    """Find all vaults: primary + any with .obsidian in common locations."""
    vaults = {}
    if _PRIMARY.exists():
        vaults[_PRIMARY.name] = _PRIMARY
    parent = _PRIMARY.parent
    if parent.exists():
        for d in parent.iterdir():
            if d.is_dir() and (d / ".obsidian").exists() and d not in vaults.values():
                vaults[d.name] = d
    for extra in [
        Path.home() / "Documents" / "Obsidian",
        Path.home() / "obsidian-vaults",
        Path.home() / "vaults",
    ]:
        if extra.exists():
            for d in extra.iterdir():
                if d.is_dir() and (d / ".obsidian").exists() and d.name not in vaults:
                    vaults[d.name] = d
    return vaults


VAULTS = _discover_vaults()
DEFAULT_VAULT = _PRIMARY.name if _PRIMARY.name in VAULTS else next(iter(VAULTS), "")


# ─── Models ─────────────────────────────────────────────────────────


class GraphNode(BaseModel):
    id: str
    name: str
    type: str  # "file", "tag", "ghost"
    color: str
    shape: str = "sphere"
    val: int = 10
    folder: Optional[str] = None
    tags: list[str] = Field(default_factory=list)
    aliases: list[str] = Field(default_factory=list)
    frontmatter: dict = Field(default_factory=dict)
    collapsible: bool = False
    pinned: bool = False
    level: int = 0


class GraphLink(BaseModel):
    source: str
    target: str
    type: str = "link"  # "link", "tag", "hierarchy"


class GraphData(BaseModel):
    nodes: list[GraphNode]
    links: list[GraphLink]


class NoteDetail(BaseModel):
    id: str
    title: str
    content: str
    tags: list[str]
    aliases: list[str] = Field(default_factory=list)
    frontmatter: dict = Field(default_factory=dict)
    path: str
    folder: str


class VaultInfo(BaseModel):
    name: str
    path: str
    published_count: int = 0


# ─── Helpers ────────────────────────────────────────────────────────


def _extract_frontmatter(content: str) -> tuple[dict, str]:
    """Extract YAML frontmatter and body content."""
    fm_match = re.match(r"^---\s*\n(.*?)\n---", content, re.DOTALL)
    if fm_match:
        fm_text = fm_match.group(1)
        body = content[fm_match.end() :].lstrip()
        fm = {}
        try:
            import yaml
            fm = yaml.safe_load(fm_text) or {}
        except Exception:
            pass
        return fm, body
    return {}, content


def _extract_tags_from_content(content: str, frontmatter: dict) -> list[str]:
    """Extract all tags from frontmatter and inline #tags."""
    tags = set()
    if frontmatter.get("tags"):
        if isinstance(frontmatter["tags"], list):
            tags.update(frontmatter["tags"])
        elif isinstance(frontmatter["tags"], str):
            tags.add(frontmatter["tags"])
    inline_tags = re.findall(r"#(\w+)", content)
    tags.update(inline_tags)
    return list(tags)


def _extract_wikilinks(content: str) -> list[str]:
    """Extract [[wikilinks]] from content."""
    return list(set(re.findall(r"\[\[([^\]]+?)(?:\|[^\]]+)?\]\]", content)))


def _tag_color(tag: str) -> str:
    """Deterministic color from tag name."""
    colors = [
        "#FF6B6B","#4ECDC4","#45B7D1","#96CEB4","#FFEAA7",
        "#DDA0DD","#98D8C8","#F7DC6F","#BB8FCE","#85C1E9",
        "#F8B500","#00CED1","#FF69B4","#32CD32","#FF7F50",
    ]
    h = sum(ord(c) for c in tag)
    return colors[h % len(colors)]


def _note_shape(fm: dict) -> str:
    """Get node shape from frontmatter graph.shape."""
    shape = fm.get("graph", {}).get("shape", "sphere")
    valid = {"sphere", "box", "cone", "cylinder", "dodecahedron", "torus", "torusknot", "octahedron"}
    return shape if shape in valid else "sphere"


def _note_color(fm: dict) -> str:
    """Get node color from frontmatter graph.color."""
    color = fm.get("graph", {}).get("color")
    if color and isinstance(color, str) and color.startswith("#") and len(color) == 7:
        # Validate hex color format
        try:
            int(color[1:], 16)
            return color
        except ValueError:
            pass
    return "#3498db"


def _scan_vault(vault_path: Path) -> tuple[dict, dict, list]:
    """
    Scan vault for published notes.
    Returns: (notes_by_id, tags_info, links)
    """
    notes = {}
    tag_hierarchy = {}  # parent -> set(children)
    
    if not vault_path.exists():
        return notes, tag_hierarchy, []
    
    for md_file in vault_path.rglob("*.md"):
        rel = md_file.relative_to(vault_path)
        try:
            content = md_file.read_text(encoding="utf-8", errors="replace")
        except Exception:
            continue
        
        fm, body = _extract_frontmatter(content)
        
        # Only include notes with publish: true
        if not fm.get("publish", False):
            continue
        
        note_id = md_file.stem
        tags = _extract_tags_from_content(body, fm)
        wikilinks = _extract_wikilinks(body)
        
        # Build tag hierarchy from tags like "parent/child"
        for tag in tags:
            if "/" in tag:
                parent = tag.split("/")[0]
                tag_hierarchy.setdefault(parent, set()).add(tag)
        
        notes[note_id] = {
            "id": note_id,
            "path": str(rel),
            "folder": str(rel.parent) if str(rel.parent) != "." else "root",
            "fm": fm,
            "body": body,
            "tags": tags,
            "wikilinks": wikilinks,
            "aliases": fm.get("aliases", []) if isinstance(fm.get("aliases"), list) else [],
        }
    
    # Build links between published notes only
    links = []
    for note_id, note in notes.items():
        for wl in note["wikilinks"]:
            target_id = wl.split("|")[0]  # Handle [[target|alias]]
            if target_id in notes:
                links.append({"source": note_id, "target": target_id, "type": "link"})
    
    return notes, tag_hierarchy, links


# Per-vault cache
_vault_caches: dict[str, tuple] = {}


def _get_vault_data(vault_name: str):
    """Get or build vault data (notes, tag_hierarchy, links)."""
    if vault_name not in VAULTS:
        raise HTTPException(status_code=404, detail=f"Vault '{vault_name}' not found")
    if vault_name not in _vault_caches or _vault_caches[vault_name] is None:
        _vault_caches[vault_name] = _scan_vault(VAULTS[vault_name])
    return _vault_caches[vault_name]


def _resolve_vault(vault: Optional[str]) -> str:
    return vault or DEFAULT_VAULT


def _build_graph(notes: dict, tag_hierarchy: dict, links: list) -> GraphData:
    """Build graph data with nodes and links for 3D visualization."""
    nodes = []
    graph_links = []
    tag_counts = {}
    
    # Count tag usage
    for note in notes.values():
        for tag in note["tags"]:
            tag_counts[tag] = tag_counts.get(tag, 0) + 1
    
    # File nodes
    for note_id, note in notes.items():
        fm = note["fm"]
        nodes.append(GraphNode(
            id=note_id,
            name=note_id,
            type="file",
            color=_note_color(fm),
            shape=_note_shape(fm),
            val=max(10, min(50, len(note["body"]) // 20)),
            folder=note["folder"],
            tags=note["tags"],
            aliases=note["aliases"],
            frontmatter=fm,
            collapsible=fm.get("graph", {}).get("collapsible", False),
            pinned=fm.get("graph", {}).get("pinned", False),
        ))
    
    # Tag nodes
    for tag, count in tag_counts.items():
        nodes.append(GraphNode(
            id=f"tag:{tag}",
            name=f"#{tag}",
            type="tag",
            color=_tag_color(tag),
            shape="octahedron",
            val=max(8, min(30, count * 3)),
            tags=[tag],
            level=tag.count("/"),
        ))
    
    # Tag hierarchy links (parent -> child)
    for parent, children in tag_hierarchy.items():
        for child in children:
            if parent in tag_counts and child in tag_counts:
                graph_links.append(GraphLink(
                    source=f"tag:{parent}",
                    target=f"tag:{child}",
                    type="hierarchy"
                ))
    
    # File-to-tag links
    for note_id, note in notes.items():
        for tag in note["tags"]:
            if tag in tag_counts:
                graph_links.append(GraphLink(
                    source=note_id,
                    target=f"tag:{tag}",
                    type="tag"
                ))
    
    # Build links between published notes only
        links = []
        all_wikilink_targets = set()  # Track ALL wikilink targets from published notes
        for note_id, note in notes.items():
            for wl in note["wikilinks"]:
                target_id = wl.split("|")[0]  # Handle [[target|alias]]
                all_wikilink_targets.add(target_id)
                if target_id in notes:
                    links.append({"source": note_id, "target": target_id, "type": "link"})

        # Ghost nodes for ALL unresolved wikilinks (including references to unpublished notes)
        resolved_ids = set(notes.keys())
        for ghost_id in all_wikilink_targets - resolved_ids:
            nodes.append(GraphNode(
                id=f"ghost:{ghost_id}",
                name=ghost_id,
                type="ghost",
                color="#ffffff",
                shape="sphere",
                val=6,
                folder="ghost",
            ))
            # Link from any note that references this ghost
            for note_id, note in notes.items():
                for wl in note["wikilinks"]:
                    wl_target = wl.split("|")[0]
                    if wl_target == ghost_id:
                        graph_links.append(GraphLink(
                            source=note_id,
                            target=f"ghost:{ghost_id}",
                            type="link"
                        ))
                        break  # Only need one link per ghost per note
    
    return GraphData(nodes=nodes, links=graph_links)


def invalidate_cache(vault_name: str = None):
    if vault_name:
        _vault_caches[vault_name] = None
    else:
        for k in _vault_caches:
            _vault_caches[k] = None


# ─── Routes ─────────────────────────────────────────────────────────


@router.get("/vaults", response_model=list[VaultInfo])
async def list_vaults():
    """List all discovered vaults with published note counts."""
    result = []
    for name, path in sorted(VAULTS.items()):
        notes, _, _ = _get_vault_data(name)
        published = sum(1 for n in notes.values() if n["fm"].get("publish", False))
        result.append(VaultInfo(name=name, path=str(path), published_count=published))
    return result


@router.get("/graph", response_model=GraphData)
async def get_graph(vault: Optional[str] = Query(None)):
    """Get full graph data for 3D visualization."""
    vault_name = _resolve_vault(vault)
    notes, tag_hierarchy, links = _get_vault_data(vault_name)
    return _build_graph(notes, tag_hierarchy, links)


@router.get("/node/{node_id}", response_model=NoteDetail)
async def get_node(node_id: str, vault: Optional[str] = Query(None)):
    """Get detailed note content for detail view."""
    vault_name = _resolve_vault(vault)
    notes, _, _ = _get_vault_data(vault_name)
    
    if node_id not in notes:
        raise HTTPException(status_code=404, detail=f"Note '{node_id}' not found")
    
    note = notes[node_id]
    return NoteDetail(
        id=node_id,
        title=note["fm"].get("title", node_id),
        content=note["body"],
        tags=note["tags"],
        aliases=note["aliases"],
        frontmatter=note["fm"],
        path=note["path"],
        folder=note["folder"],
    )


@router.post("/refresh")
async def refresh_vault(vault: Optional[str] = Query(None)):
    """Force refresh vault cache."""
    vault_name = _resolve_vault(vault)
    invalidate_cache(vault_name)
    notes, _, _ = _get_vault_data(vault_name)
    published = sum(1 for n in notes.values() if n["fm"].get("publish", False))
    return {"status": "ok", "vault": vault_name, "published": published}