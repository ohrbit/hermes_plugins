/**
 * GalaxyBrain Dashboard Plugin v2
 * 3D interactive knowledge graph for Obsidian vaults
 * Uses react-force-graph-3d UMD (React component, bundles Three.js + OrbitControls)
 * ES5 compatible for dashboard runtime
 */
(function () {
    'use strict';

    var registry = window.__HERMES_PLUGINS__;
    var sdk = window.__HERMES_PLUGIN_SDK__;
    if (!registry || !registry.register) return;
    if (!sdk) return;

    var React = sdk.React;
    var hooks = sdk.hooks;
    var fetchJSON = sdk.fetchJSON;
    var components = sdk.components;
    var utils = sdk.utils;

    var useState = React.useState;
    var useEffect = React.useEffect;
    var useRef = React.useRef;
    var useMemo = React.useMemo;
    var useCallback = React.useCallback;

    // Color Constants
    var BG_DARK = '#0a0a0a';
    var BG_LIGHT = '#f5f5f5';
    var STORAGE_KEY = 'galaxybrain_theme';

    // Load External Libraries
    function loadScript(src, globalName) {
        return new Promise(function(resolve, reject) {
            if (window[globalName]) {
                resolve(window[globalName]);
                return;
            }
            var existing = document.querySelector('script[src="' + src + '"]');
            if (existing) {
                existing.addEventListener('load', function() { resolve(window[globalName]); });
                existing.addEventListener('error', reject);
                return;
            }
            var script = document.createElement('script');
            script.src = src;
            script.onload = function() { resolve(window[globalName]); };
            script.onerror = reject;
            document.head.appendChild(script);
        });
    }

    // 3D Graph Component - uses react-force-graph-3d as React component
    function Graph3D(props) {
        var nodes = props.nodes;
        var links = props.links;
        var onNodeClick = props.onNodeClick;
        var highlightedTag = props.highlightedTag;
        var isDark = props.isDark;
        var libsReady = props.libsReady;

        var collapsedNodes = useState(new Set());
        var showBuildCta = useState(true);
        var showCallout = useState(true);

        var bgColor = isDark ? BG_DARK : BG_LIGHT;
        var uiTextColor = isDark ? '#e0e0e0' : '#111111';
        var uiBgColor = isDark ? 'rgba(20,20,20,0.9)' : 'rgba(240,240,240,0.9)';
        var uiBorder = isDark ? 'rgba(255,255,255,0.12)' : 'rgba(0,0,0,0.12)';

        var visibleNodes = useMemo(function() {
            if (!collapsedNodes[0] || collapsedNodes[0].size === 0) {
                console.log('[galaxy-brain] No collapsed tags, showing all', nodes.length, 'nodes');
                return nodes;
            }
            var expanded = new Set();
            function getExp(id) {
                if (expanded.has(id)) return expanded;
                expanded.add(id);
                links.forEach(function(l) {
                    if (l.source === id && !collapsedNodes[0].has(l.source)) getExp(l.target);
                    if (l.target === id && !collapsedNodes[0].has(l.target)) getExp(l.source);
                });
                return expanded;
            }
            var seedNodes = [];
            nodes.forEach(function(n) {
                if (n.pinned || !n.collapsible) {
                    expanded.add(n.id);
                    seedNodes.push(n.id);
                }
            });
            seedNodes.forEach(function(id) { getExp(id); });
            if (highlightedTag) getExp('tag:' + highlightedTag);
            var result = nodes.filter(function(n) { return expanded.has(n.id); });
            console.log('[galaxy-brain] visibleNodes:', result.length, '/', nodes.length, 'expanded:', expanded.size);
            return result;
        }, [nodes, links, collapsedNodes[0], highlightedTag]);

        var visibleLinks = useMemo(function() {
            if (!collapsedNodes[0] || collapsedNodes[0].size === 0) return links;
            var expanded = new Set();
            function getExp(id) {
                if (expanded.has(id)) return expanded;
                expanded.add(id);
                links.forEach(function(l) {
                    if (l.source === id && !collapsedNodes[0].has(l.source)) getExp(l.target);
                    if (l.target === id && !collapsedNodes[0].has(l.target)) getExp(l.source);
                });
                return expanded;
            }
            var seedNodes = [];
            nodes.forEach(function(n) {
                if (n.pinned || !n.collapsible) {
                    expanded.add(n.id);
                    seedNodes.push(n.id);
                }
            });
            seedNodes.forEach(function(id) { getExp(id); });
            if (highlightedTag) getExp('tag:' + highlightedTag);
            var result = links.filter(function(l) { return expanded.has(l.source) && expanded.has(l.target); });
            console.log('[galaxy-brain] visibleLinks:', result.length, '/', links.length);
            return result;
        }, [nodes, links, collapsedNodes[0], highlightedTag]);

        var handleLocalNodeClick = useCallback(function(node) {
            if (!node || !node.collapsible) return;
            var newCollapsed = new Set(collapsedNodes[0]);
            if (newCollapsed.has(node.id)) newCollapsed.delete(node.id);
            else newCollapsed.add(node.id);
            collapsedNodes[1](newCollapsed);
            showBuildCta[1](false);
        }, [collapsedNodes[0]]);

        var handleCalloutClose = useCallback(function() { return showCallout[1](false); }, []);
        var handleCtaClose = useCallback(function() { return showBuildCta[1](false); }, []);

        if (!libsReady) {
            return React.createElement('div', { style: { position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: uiTextColor, background: bgColor, borderRadius: 8 } }, 'Loading 3D libraries...');
        }

        var ForceGraph3D = window.ForceGraph3D;
        if (!ForceGraph3D) {
            return React.createElement('div', { style: { position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#ff6b6b', background: bgColor, borderRadius: 8 } }, 'ForceGraph3D not loaded');
        }

        var graphData = useMemo(function() { return { nodes: visibleNodes, links: visibleLinks }; }, [visibleNodes, visibleLinks]);
        var nodeColor = useCallback(function(n) { return n.color; }, []);
        var nodeLabel = useCallback(function(n) { return n.name; }, []);
        var nodeThreeObjectExtend = useCallback(function(obj, node) { obj.userData = obj.userData || {}; obj.userData.galaxyBrainNode = node; }, []);
        var handleNodeClick = useCallback(function(node, event) {
            if (!node) return;
            if (node.collapsible) handleLocalNodeClick(node);
            else onNodeClick(node);
        }, [handleLocalNodeClick, onNodeClick]);
        var handleNodeHover = useCallback(function(node, prevNode) {}, []);

        return React.createElement('div', { style: { position: 'relative', width: '100%', height: 600, overflow: 'hidden', background: bgColor, borderRadius: 8 } },
            React.createElement('div', { style: { position: 'absolute', top: 12, left: 12, display: 'flex', gap: 10, flexWrap: 'wrap', zIndex: 10, maxWidth: '80%' } },
                React.createElement('span', { style: { fontSize: 11, display: 'flex', alignItems: 'center', gap: 4, color: uiTextColor } }, React.createElement('span', { style: { width: 10, height: 10, borderRadius: '50%', background: '#3498db', display: 'inline-block' } }), 'Notes'),
                React.createElement('span', { style: { fontSize: 11, display: 'flex', alignItems: 'center', gap: 4, color: uiTextColor } }, React.createElement('span', { style: { width: 10, height: 10, borderRadius: '50%', background: '#FF6B6B', display: 'inline-block' } }), 'Tags'),
                React.createElement('span', { style: { fontSize: 11, display: 'flex', alignItems: 'center', gap: 4, color: uiTextColor } }, React.createElement('span', { style: { width: 10, height: 10, borderRadius: '50%', background: '#ffffff', border: '1px solid', display: 'inline-block' } }), 'Ghost (unlinked)')
            ),
            showCallout[0] && React.createElement('div', { style: { position: 'absolute', top: '50%', left: '50%', transform: 'translate(-50%, -50%)', background: uiBgColor, border: '1px solid ' + uiBorder, borderRadius: 8, padding: '20px 24px', maxWidth: 320, zIndex: 20, boxShadow: '0 8px 32px rgba(0,0,0,0.3)', textAlign: 'center' } },
                React.createElement('div', { style: { display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 12 } },
                    React.createElement('strong', { style: { color: uiTextColor, fontSize: 14 } }, 'Welcome to GalaxyBrain'),
                    React.createElement('button', { onClick: handleCalloutClose, style: { background: 'none', border: 'none', color: uiTextColor, cursor: 'pointer', fontSize: 18, padding: 0, lineHeight: 1 } }, '\u00d7')
                ),
                React.createElement('p', { style: { margin: '0 0 12px', color: uiTextColor, fontSize: 13, opacity: 0.8 } }, 'Click tags to collapse/expand their notes. Drag to pan, scroll to zoom.'),
                React.createElement('p', { style: { margin: 0, color: uiTextColor, fontSize: 12, opacity: 0.7 } }, 'Add { publish: true, tags: [...] } frontmatter to notes in Obsidian.')
            ),
            showBuildCta[0] && React.createElement('div', { style: { position: 'absolute', bottom: 60, right: 12, background: uiBgColor, border: '1px solid ' + uiBorder, borderRadius: 8, padding: '12px 16px', maxWidth: 300, zIndex: 15, boxShadow: '0 4px 24px rgba(0,0,0,0.3)' } }, React.createElement('div', { style: { display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 } }, React.createElement('strong', { style: { color: uiTextColor, fontSize: 12 } }, 'Need setup instructions?'), React.createElement('button', { onClick: handleCtaClose, style: { background: 'none', border: 'none', color: uiTextColor, cursor: 'pointer', fontSize: 16, padding: 0, lineHeight: 1 } }, '\u00d7')), React.createElement('p', { style: { margin: 0, color: uiTextColor, fontSize: 11, opacity: 0.7 } }, 'See GalaxyBrain docs for graph config options (shape, color, collapsible, pinned, callout).')),
            React.createElement(ForceGraph3D, {
                graphData: graphData,
                nodeId: 'id',
                nodeColor: nodeColor,
                nodeLabel: nodeLabel,
                nodeThreeObjectExtend: nodeThreeObjectExtend,
                onNodeClick: handleNodeClick,
                onNodeHover: handleNodeHover,
                linkColor: '#888',
                linkWidth: 1,
                linkDirectionalParticles: 0,
                enableNodeDrag: true,
                enableNavigationControls: true,
                backgroundColor: bgColor,
                width: '100%',
                height: '100%',
                showNavInfo: false,
                cameraPosition: { x: 0, y: 0, z: 200 },
            })
        );
    }

    function renderMarkdown(text) {
        if (!text) return '';
        var h = text.replace(/^---[\s\S]*?---\n/m, '');
        h = h.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        h = h.replace(/```(\w*)\n([\s\S]*?)```/g, function(_, l, c) { return '<pre class="code-block" data-lang="' + l + '"><code>' + c.trim() + '</code></pre>'; });
        h = h.replace(/^# (.+)$/gm, '<h1 class="md-h1">$1</h1>');
        h = h.replace(/^## (.+)$/gm, '<h2 class="md-h2">$1</h2>');
        h = h.replace(/^### (.+)$/gm, '<h3 class="md-h3">$1</h3>');
        h = h.replace(/^- \[x\] (.+)$/gm, '<div class="md-check"><span class="md-check-box checked">\u2713</span>$1</div>');
        h = h.replace(/^- \[ \] (.+)$/gm, '<div class="md-check"><span class="md-check-box"></span>$1</div>');
        h = h.replace(/!\[\[([^\]]+)\]\]/g, '<div class="md-img-embed"><img src="$1" /></div>');
        h = h.replace(/\[\[([^\]]+)\]\]/g, '<a class="md-wikilink" data-note="$1" href="#">$1</a>');
        h = h.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
        h = h.replace(/\*(.+?)\*/g, '<em>$1</em>');
        h = h.replace(/`([^`]+)`/g, '<code class="md-inline-code">$1</code>');
        h = h.replace(/\n\n+/g, '</p><p class="md-p">');
        h = h.replace(/\n/g, '<br/>');
        h = '<p class="md-p">' + h + '</p>';
        h = h.replace(/<p class="md-p">\s*<\/p>/g, '');
        return h;
    }

    function injectStyles() {
        if (document.getElementById('galaxybrain-plugin-styles')) return;
        var s = document.createElement('style');
        s.id = 'galaxybrain-plugin-styles';
        s.textContent = '' +
            '.code-block{background:rgba(0,0,0,.4);border:1px solid rgba(255,255,255,.08);border-radius:6px;padding:12px 14px;margin:10px 0;overflow-x:auto;font-size:12px;line-height:1.5;font-family:\'SF Mono\',\'Fira Code\',monospace}' +
            '.code-block code{color:#e5e5e5}' +
            '.code-block::before{content:attr(data-lang);display:block;font-size:10px;color:rgba(255,255,255,.3);text-transform:uppercase;margin-bottom:6px}' +
            '.md-h1{font-size:22px;font-weight:700;margin:16px 0 8px;color:#e5e5e5}' +
            '.md-h2{font-size:18px;font-weight:700;margin:14px 0 6px;color:#e5e5e5}' +
            '.md-h3{font-size:15px;font-weight:700;margin:12px 0 4px;color:#e5e5e5}' +
            '.md-check{display:flex;align-items:center;gap:8px;margin:3px 0;font-size:13px}' +
            '.md-check-box{width:16px;height:16px;border:1.5px solid rgba(255,255,255,.25);border-radius:3px;display:inline-flex;align-items:center;justify-content:center;font-size:11px;flex-shrink:0}' +
            '.md-check-box.checked{background:#a8e619;border-color:#a8e619;color:#000}' +
            '.md-img-embed{max-width:100%;margin:8px 0;text-align:center}' +
            '.md-img-embed img{max-width:100%;border-radius:6px}' +
            '.md-wikilink{color:#60a5fa;cursor:pointer;text-decoration:none;border-bottom:1px dashed rgba(96,165,250,.3)}' +
            '.md-wikilink:hover{color:#93c5fd;border-bottom-color:#93c5fd}' +
            '.md-inline-code{background:rgba(255,255,255,.08);padding:1px 5px;border-radius:3px;font-size:12px;font-family:\'SF Mono\',\'Fira Code\',monospace}' +
            '.md-p{margin:4px 0;color:#e5e5e5;line-height:1.6}' +
            '.note-detail{background:rgba(0,0,0,.2);border:1px solid rgba(255,255,255,.06);border-radius:8px;padding:16px;max-height:60vh;overflow:auto}' +
            '.note-meta{display:flex;gap:12px;margin-bottom:12px;flex-wrap:wrap;font-size:12px;color:rgba(255,255,255,.5)}' +
            '.tag-badge{background:rgba(96,165,250,.15);border:1px solid rgba(96,165,250,.3);border-radius:999px;padding:2px 8px;font-size:11px;color:#60a5fa}' +
            '.ghost-badge{background:rgba(255,170,0,.15);border:1px solid rgba(255,170,0,.3);border-radius:999px;padding:2px 8px;font-size:11px;color:#ffaa00}';
        document.head.appendChild(s);
    }

    function GalaxyBrainPlugin() {
        var vaults = useState([]);
        var currentVault = useState(null);
        var graphData = useState(null);
        var loadError = useState(null);
        var selectedNode = useState(null);
        var nodeDetail = useState(null);
        var view = useState('graph'); // 'graph' | 'note' | 'split'
        var isDark = useState(function() {
            try { return (localStorage.getItem(STORAGE_KEY) || 'dark') !== 'light'; }
            catch (e) { return true; }
        });
        var highlightedTag = useState(null);
        var loading = useState(true);
        var libsReady = useState(false);
        // Sidebar state
        var sidebarOpen = useState(false);
        var sidebarWidth = useState(380);
        var isResizing = useState(false);
        var BASE = '/api/plugins/galaxy-brain';

        useEffect(function() {
            function onThemeChange(e) { isDark[1](e.detail.theme !== 'light'); }
            function onStorage(e) { if (e.key === STORAGE_KEY) isDark[1]((e.newValue || 'dark') !== 'light'); }
            window.addEventListener('theme-change', onThemeChange);
            window.addEventListener('storage', onStorage);
            return function() { window.removeEventListener('theme-change', onThemeChange); window.removeEventListener('storage', onStorage); };
        }, []);

        useEffect(function() {
            var cancelled = false;
            function loadLibs() {
                console.log('[galaxy-brain] Loading 3D libraries...');
                window.React = React;
                return loadScript('/api/plugins/galaxy-brain/static/libs/react-force-graph-3d.umd.min.js', 'ForceGraph3D')
                    .then(function() { console.log('[galaxy-brain] window.ForceGraph3D:', typeof window.ForceGraph3D); if (!cancelled) libsReady[1](true); })
                    .catch(function(e) { console.error('[galaxy-brain] Failed to load 3D libraries:', e); });
            }
            loadLibs();
            return function() { cancelled = true; };
        }, []);

        useEffect(function() {
            fetchJSON(BASE + '/vaults').then(function(v) { vaults[1](v); if (v.length > 0 && !currentVault[0]) currentVault[1](v[0].name); }).catch(function(err) { console.error('[galaxy-brain] Failed to load vaults:', err); loadError[1]('Failed to load vaults'); });
        }, []);

        useEffect(function() {
            if (!currentVault[0]) return;
            loading[1](true); loadError[1](null);
            var q = '?vault=' + encodeURIComponent(currentVault[0]);
            fetchJSON(BASE + '/graph' + q).then(function(g) { graphData[1](g); loading[1](false); selectedNode[1](null); nodeDetail[1](null); view[1]('graph'); }).catch(function(err) { console.error('[galaxy-brain] Failed to load graph:', err); loadError[1]('Failed to load graph: ' + err.message); loading[1](false); });
        }, [currentVault[0]]);

        var handleNodeClick = useCallback(function(node) {
            if (!node) return;
            selectedNode[1](node);
            if (node.type === 'file') {
                sidebarOpen[1](true);
                fetchJSON(BASE + '/node/' + encodeURIComponent(node.id) + '?vault=' + encodeURIComponent(currentVault[0])).then(nodeDetail[1]).catch(function() { return nodeDetail[1](null); });
            } else if (node.type === 'tag') {
                var tagName = node.name.replace(/^#/, '');
                highlightedTag[1](tagName);
                setTimeout(function() { return highlightedTag[1](null); }, 3000);
            }
        }, [currentVault[0]]);

        var handleSidebarResize = useCallback(function(e) {
            if (!isResizing[0]) return;
            var newWidth = window.innerWidth - e.clientX;
            if (newWidth >= 280 && newWidth <= 600) {
                sidebarWidth[1](newWidth);
            }
        }, []);

        var handleResizeStart = useCallback(function() {
            isResizing[1](true);
            document.body.style.cursor = 'ew-resize';
            document.body.style.userSelect = 'none';
            window.addEventListener('mousemove', handleSidebarResize);
            window.addEventListener('mouseup', handleResizeEnd);
        }, [handleSidebarResize]);

        var handleResizeEnd = useCallback(function() {
            isResizing[1](false);
            document.body.style.cursor = '';
            document.body.style.userSelect = '';
            window.removeEventListener('mousemove', handleSidebarResize);
            window.removeEventListener('mouseup', handleResizeEnd);
        }, []);

        var handleCloseSidebar = useCallback(function() {
            sidebarOpen[1](false);
            selectedNode[1](null);
            nodeDetail[1](null);
        }, []);

        var handleWikilinkClick = useCallback(function(noteName) { if (graphData[0]) { var found = graphData[0].nodes.find(function(n) { return n.name === noteName || n.id === noteName; }); if (found) handleNodeClick(found); } }, [graphData[0], handleNodeClick]);
        var renderedDetail = useMemo(function() { return nodeDetail[0] ? renderMarkdown(nodeDetail[0].content) : ''; }, [nodeDetail[0]]);
        var handleDetailClick = useCallback(function(e) { var wl = e.target.closest('.md-wikilink'); if (wl) { e.preventDefault(); handleWikilinkClick(wl.dataset.note); } }, [handleWikilinkClick]);

        useEffect(function() { injectStyles(); }, []);
        useEffect(function() { try { localStorage.setItem(STORAGE_KEY, isDark[0] ? 'dark' : 'light'); } catch (e) {} }, [isDark[0]]);

        if (loading[0] && !graphData[0]) return React.createElement('div', { style: { padding: 16, height: '100%', display: 'flex', alignItems: 'center', justifyContent: 'center', color: isDark[0] ? '#e0e0e0' : '#111' } }, 'Loading GalaxyBrain...');
        if (loadError[0]) return React.createElement('div', { style: { padding: 16, height: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', color: '#ff6b6b', textAlign: 'center' } }, React.createElement('h2', { style: { margin: '0 0 8px' } }, 'Error'), React.createElement('p', { style: { margin: 0 } }, loadError[0]), React.createElement('button', { onClick: function() { return window.location.reload(); }, style: { marginTop: 16, padding: '8px 16px', borderRadius: 6, border: '1px solid', background: isDark[0] ? 'rgba(255,255,255,.06)' : 'rgba(0,0,0,.06)', color: isDark[0] ? '#e0e0e0' : '#111', cursor: 'pointer' } }, 'Retry'));
        if (!graphData[0] || !graphData[0].nodes.length) return React.createElement('div', { style: { padding: 16, height: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', color: isDark[0] ? '#888' : '#666', textAlign: 'center' } }, React.createElement('h2', { style: { margin: '0 0 12px' } }, 'No Published Notes'), React.createElement('p', { style: { margin: '0 0 16px', maxWidth: 400 } }, 'Add { publish: true } to your note frontmatter in Obsidian to include notes in the graph.'), React.createElement('pre', { style: { background: isDark[0] ? 'rgba(0,0,0,.4)' : 'rgba(0,0,0,.06)', padding: 12, borderRadius: 6, fontSize: 12, overflow: 'auto' } }, '---\npublish: true\ntitle: "My Note"\ntags: [topic, subtopic]\ngraph:\n  shape: sphere\n  color: "#3498db"\n---\n# My Note\nContent here...'));

        var style = { padding: 16, height: '100%', color: isDark[0] ? '#e0e0e0' : '#111111', background: isDark[0] ? BG_DARK : BG_LIGHT, display: 'flex', flexDirection: 'column' };
        var headerStyle = { display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 14, flexShrink: 0 };
        var bgColor = isDark[0] ? BG_DARK : BG_LIGHT;
        var uiTextColor = isDark[0] ? '#e0e0e0' : '#111111';
        var uiBgColor = isDark[0] ? 'rgba(20,20,20,0.9)' : 'rgba(240,240,240,0.9)';
        var uiBorder = isDark[0] ? 'rgba(255,255,255,0.12)' : 'rgba(0,0,0,0.12)';

        return React.createElement('div', { style: style },
            React.createElement('div', { style: headerStyle },
                React.createElement('div', { style: { display: 'flex', alignItems: 'center', gap: 12 } },
                    React.createElement('h1', { style: { fontSize: 20, fontWeight: 700, margin: 0 } }, 'GalaxyBrain'),
                    React.createElement('span', { style: { fontSize: 12, color: isDark[0] ? 'rgba(255,255,255,.4)' : 'rgba(0,0,0,.4)' } }, graphData[0].nodes.length + ' nodes \u00b7 ' + graphData[0].links.length + ' links')
                ),
                React.createElement('div', { style: { display: 'flex', alignItems: 'center', gap: 8 } },
                    vaults[0].length > 1 && React.createElement('select', { value: currentVault[0], onChange: function(e) { return currentVault[1](e.target.value); }, style: { padding: '4px 10px', borderRadius: 6, border: '1px solid ' + (isDark[0] ? 'rgba(255,255,255,.15)' : 'rgba(0,0,0,.15)'), background: isDark[0] ? 'rgba(255,255,255,.06)' : 'rgba(0,0,0,.06)', color: isDark[0] ? '#e5e5e5' : '#111', fontSize: 12, fontWeight: 600, cursor: 'pointer', outline: 'none' } }, vaults[0].map(function(v) { return React.createElement('option', { key: v.name, value: v.name }, v.name + ' (' + v.published_count + ' published)'); })),
                    React.createElement('button', { onClick: function() { return isDark[1](function(d) { return !d; }); }, style: { width: 32, height: 32, borderRadius: 6, border: '1px solid ' + (isDark[0] ? 'rgba(255,255,255,.15)' : 'rgba(0,0,0,.15)'), background: isDark[0] ? 'rgba(255,255,255,.06)' : 'rgba(0,0,0,.06)', color: isDark[0] ? '#e5e5e5' : '#111', cursor: 'pointer', fontSize: 14, display: 'flex', alignItems: 'center', justifyContent: 'center' }, title: 'Toggle theme' }, isDark[0] ? '\u2600\ufe0f' : '\uD83C\uDF19'),
                    sidebarOpen[0] && React.createElement('button', { onClick: handleCloseSidebar, style: { padding: '6px 12px', borderRadius: 6, border: '1px solid rgba(168,230,25,.3)', background: 'rgba(168,230,25,.15)', color: '#a8e619', cursor: 'pointer', fontSize: 12, fontWeight: 600 } }, '\u2190 Close Detail')
                )
            ),
            React.createElement('div', { style: { flex: 1, minHeight: 0, display: 'flex', position: 'relative' } },
                // Graph area (flex: 1)
                React.createElement('div', { style: { flex: 1, minHeight: 0, position: 'relative', marginRight: sidebarOpen[0] ? 0 : 0 } },
                    React.createElement(Graph3D, { nodes: graphData[0].nodes, links: graphData[0].links, onNodeClick: handleNodeClick, highlightedTag: highlightedTag[0], isDark: isDark[0], libsReady: libsReady[0] })
                ),
                // Sidebar
                sidebarOpen[0] && nodeDetail[0] && React.createElement('div', {
                    style: {
                        width: sidebarWidth[0],
                        minWidth: 280,
                        maxWidth: 600,
                        background: uiBgColor,
                        borderLeft: '1px solid ' + uiBorder,
                        display: 'flex',
                        flexDirection: 'column',
                        boxShadow: '-8px 0 24px rgba(0,0,0,0.2)',
                        zIndex: 10,
                        overflow: 'hidden'
                    }
                },
                    React.createElement('div', {
                        style: {
                            position: 'absolute',
                            left: 0,
                            top: 0,
                            bottom: 0,
                            width: 8,
                            cursor: 'ew-resize',
                            background: 'transparent',
                            zIndex: 20
                        },
                        onMouseDown: handleResizeStart,
                        onMouseMove: function(e) { e.target.style.background = isResizing[0] ? 'rgba(96,165,250,0.2)' : 'transparent'; },
                        onMouseLeave: function(e) { if (!isResizing[0]) e.target.style.background = 'transparent'; }
                    }),
                    React.createElement('div', {
                        style: {
                            padding: '12px 16px',
                            borderBottom: '1px solid ' + uiBorder,
                            display: 'flex',
                            justifyContent: 'space-between',
                            alignItems: 'center',
                            flexShrink: 0
                        }
                    },
                        React.createElement('div', { style: { display: 'flex', alignItems: 'center', gap: 10 } },
                            React.createElement('span', { style: { fontSize: 14, fontWeight: 600, color: uiTextColor, maxWidth: sidebarWidth[0] - 100, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' } }, nodeDetail[0].title || nodeDetail[0].id),
                            nodeDetail[0].frontmatter.graph_pinned && React.createElement('span', { className: 'tag-badge', style: { fontSize: 10 } }, '\uD83D\uDCCC Pinned'),
                            nodeDetail[0].frontmatter.graph_collapsible && React.createElement('span', { className: 'tag-badge', style: { fontSize: 10 } }, '\uD83D\uDDC1 Collapsible')
                        ),
                        React.createElement('button', { onClick: handleCloseSidebar, style: { width: 24, height: 24, borderRadius: 4, border: '1px solid ' + uiBorder, background: 'transparent', color: uiTextColor, cursor: 'pointer', fontSize: 14, display: 'flex', alignItems: 'center', justifyContent: 'center' } }, '\u00d7')
                    ),
                    React.createElement('div', { className: 'note-detail', style: { flex: 1, overflow: 'auto', padding: 16 }, onClick: handleDetailClick },
                        React.createElement('div', { className: 'note-meta', style: { flexShrink: 0 } },
                            nodeDetail[0].tags.map(function(t) { return React.createElement('span', { key: t, className: 'tag-badge' }, '#' + t); }),
                            (nodeDetail[0].aliases || []).map(function(a) { return React.createElement('span', { key: a, className: 'tag-badge', style: { background: 'rgba(255,170,0,.15)', borderColor: 'rgba(255,170,0,.3)', color: '#ffaa00' } }, '@' + a); })
                        ),
                        React.createElement('div', { dangerouslySetInnerHTML: { __html: renderedDetail } })
                    )
                )
            )
        );
    }

    registry.register('galaxy-brain', GalaxyBrainPlugin);
    console.log('[galaxy-brain] Plugin registered');
})();