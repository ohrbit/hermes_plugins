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
                console.log('[galaxy-brain] No collapsed parents, showing all', nodes.length, 'nodes');
                return nodes;
            }
            // Build adjacency: parent -> children (file nodes connected via 'tag' or 'link' type)
            var childrenOf = {};
            links.forEach(function(l) {
                var src = l.source, tgt = l.target;
                // If source is a collapsible parent and target is a file, target is child of source
                var srcNode = nodes.find(function(n) { return n.id === src; });
                var tgtNode = nodes.find(function(n) { return n.id === tgt; });
                if (srcNode && srcNode.collapsible && tgtNode && tgtNode.type === 'file') {
                    childrenOf[src] = childrenOf[src] || new Set();
                    childrenOf[src].add(tgt);
                }
                if (tgtNode && tgtNode.collapsible && srcNode && srcNode.type === 'file') {
                    childrenOf[tgt] = childrenOf[tgt] || new Set();
                    childrenOf[tgt].add(src);
                }
            });
            // Collect all hidden children of collapsed parents
            var hidden = new Set();
            collapsedNodes[0].forEach(function(parentId) {
                var kids = childrenOf[parentId];
                if (kids) kids.forEach(function(k) { return hidden.add(k); });
            });
            var result = nodes.filter(function(n) { return !hidden.has(n.id); });
            console.log('[galaxy-brain] visibleNodes:', result.length, '/', nodes.length, 'hidden:', hidden.size, 'collapsed parents:', collapsedNodes[0].size);
            return result;
        }, [nodes, links, collapsedNodes[0], highlightedTag]);

        var visibleLinks = useMemo(function() {
            if (!collapsedNodes[0] || collapsedNodes[0].size === 0) return links;
            var hidden = new Set();
            var childrenOf = {};
            links.forEach(function(l) {
                var src = l.source, tgt = l.target;
                var srcNode = nodes.find(function(n) { return n.id === src; });
                var tgtNode = nodes.find(function(n) { return n.id === tgt; });
                if (srcNode && srcNode.collapsible && tgtNode && tgtNode.type === 'file') {
                    childrenOf[src] = childrenOf[src] || new Set();
                    childrenOf[src].add(tgt);
                }
                if (tgtNode && tgtNode.collapsible && srcNode && srcNode.type === 'file') {
                    childrenOf[tgt] = childrenOf[tgt] || new Set();
                    childrenOf[tgt].add(src);
                }
            });
            collapsedNodes[0].forEach(function(parentId) {
                var kids = childrenOf[parentId];
                if (kids) kids.forEach(function(k) { return hidden.add(k); });
            });
            var result = links.filter(function(l) { return !hidden.has(l.source) && !hidden.has(l.target); });
            console.log('[galaxy-brain] visibleLinks:', result.length, '/', links.length);
            return result;
        }, [nodes, links, collapsedNodes[0], highlightedTag]);

        var handleLocalNodeClick = useCallback(function(node) {
            if (!node || !node.collapsible) return;
            var newCollapsed = new Set(collapsedNodes[0]);
            if (newCollapsed.has(node.id)) newCollapsed.delete(node.id); // expand: remove from collapsed
            else newCollapsed.add(node.id); // collapse: add to collapsed
            collapsedNodes[1](newCollapsed);
            showBuildCta[1](false);
        }, [collapsedNodes[0]]);

        var handleCalloutClose = useCallback(function() { return showCallout[1](false); }, []);
        var handleCtaClose = useCallback(function() { return showBuildCta[1](false); }, []);

        if (!libsReady) {
            return React.createElement('div', { style: { position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: uiTextColor, background: bgColor, borderRadius: 8 } }, 'Loading 3D libraries...');
        }

        var ForceGraph3D = window.ForceGraph3D;
        var THREE = window.THREE;
        if (!ForceGraph3D || !THREE) {
            return React.createElement('div', { style: { position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#ff6b6b', background: bgColor, borderRadius: 8 } }, '3D libraries not fully loaded (ForceGraph3D:' + !!ForceGraph3D + ' THREE:' + !!THREE + ')');
        }

        var graphData = useMemo(function() { return { nodes: visibleNodes, links: visibleLinks }; }, [visibleNodes, visibleLinks]);
        var nodeColor = useCallback(function(n) { return n.color; }, []);
        var nodeLabel = useCallback(function(n) { return n.name; }, []);
        
        // Custom Three.js object per node based on shape property
        var nodeThreeObject = useCallback(function(node) {
            var THREE = window.THREE;
            var shape = node.shape || 'sphere';
            var size = Math.max(2, (node.val || 10) * 0.5);
            var geometry;
            switch (shape) {
                case 'box': geometry = new THREE.BoxGeometry(size, size, size); break;
                case 'cylinder': geometry = new THREE.CylinderGeometry(size * 0.6, size * 0.6, size * 1.5, 8); break;
                case 'cone': geometry = new THREE.ConeGeometry(size * 0.8, size * 1.5, 8); break;
                case 'torus': geometry = new THREE.TorusGeometry(size * 0.7, size * 0.3, 8, 16); break;
                case 'torusknot': geometry = new THREE.TorusKnotGeometry(size * 0.6, size * 0.2, 64, 8, 2, 3); break;
                case 'dodecahedron': geometry = new THREE.DodecahedronGeometry(size); break;
                case 'octahedron': geometry = new THREE.OctahedronGeometry(size); break;
                case 'sphere':
                default: geometry = new THREE.SphereGeometry(size, 16, 16); break;
            }
            var material = new THREE.MeshBasicMaterial({ color: node.color, transparent: true, opacity: 0.9 });
            var mesh = new THREE.Mesh(geometry, material);
            mesh.userData = mesh.userData || {};
            mesh.userData.galaxyBrainNode = node;
            return mesh;
        }, []);
        
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
                nodeThreeObject: nodeThreeObject,
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
            // Code blocks with language label
            '.code-block{background:rgba(120,80,180,0.15);border:1px solid rgba(180,140,220,0.25);border-radius:8px;padding:14px 16px;margin:12px 0;overflow-x:auto;font-size:13px;line-height:1.6;font-family:\'SF Mono\',\'Fira Code\',\'JetBrains Mono\',monospace;position:relative}' +
            '.code-block code{color:#e8e0f0;background:none;padding:0}' +
            '.code-block::before{content:attr(data-lang);position:absolute;top:8px;right:12px;font-size:10px;font-weight:600;color:rgba(180,140,220,0.7);text-transform:uppercase;letter-spacing:0.5px;background:rgba(120,80,180,0.2);padding:2px 8px;border-radius:4px}' +
            // Headers
            '.md-h1{font-size:24px;font-weight:700;margin:20px 0 10px;color:#fff;line-height:1.3}' +
            '.md-h2{font-size:20px;font-weight:600;margin:18px 0 8px;color:#f0e8ff;line-height:1.3;border-bottom:1px solid rgba(180,140,220,0.15);padding-bottom:6px}' +
            '.md-h3{font-size:16px;font-weight:600;margin:16px 0 6px;color:#e0d8f0;line-height:1.4}' +
            // Checkboxes
            '.md-check{display:flex;align-items:flex-start;gap:10px;margin:6px 0;font-size:13px;color:#e5e0f0}' +
            '.md-check-box{width:18px;height:18px;border:2px solid rgba(180,140,220,0.4);border-radius:4px;display:inline-flex;align-items:center;justify-content:center;font-size:12px;flex-shrink:0;transition:all 0.15s}' +
            '.md-check-box.checked{background:#a8e619;border-color:#a8e619;color:#1a1a1a}' +
            // Images
            '.md-img-embed{max-width:100%;margin:12px 0;text-align:center}' +
            '.md-img-embed img{max-width:100%;border-radius:8px;box-shadow:0 4px 20px rgba(0,0,0,0.4)}' +
            // Wikilinks
            '.md-wikilink{color:#8ab4f8;cursor:pointer;text-decoration:none;border-bottom:1px dashed rgba(138,180,248,0.4);padding-bottom:1px;transition:all 0.15s}' +
            '.md-wikilink:hover{color:#b4d0f8;border-bottom-color:#8ab4f8;text-shadow:0 0 8px rgba(138,180,248,0.3)}' +
            // Inline code
            '.md-inline-code{background:rgba(120,80,180,0.2);border:1px solid rgba(180,140,220,0.15);padding:2px 6px;border-radius:4px;font-size:12.5px;font-family:\'SF Mono\',\'Fira Code\',\'JetBrains Mono\',monospace;color:#d8c8f0}' +
            // Paragraphs
            '.md-p{margin:6px 0;color:#dcd0f0;line-height:1.7}' +
            // Detail panel
            '.note-detail{background:none;padding:0;max-height:none;overflow:visible}' +
            '.note-meta{display:flex;gap:8px;margin-bottom:16px;flex-wrap:wrap;align-items:center}' +
            '.tag-badge{background:rgba(138,180,248,0.15);border:1px solid rgba(138,180,248,0.3);border-radius:999px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#8ab4f8;letter-spacing:0.2px}' +
            '.ghost-badge{background:rgba(255,170,0,.15);border:1px solid rgba(255,170,0,.3);border-radius:999px;padding:4px 12px;font-size:11.5px;font-weight:500;color:#ffaa00;letter-spacing:0.2px}' +
            // Strong/em
            '.md-p strong, .md-h1 strong, .md-h2 strong, .md-h3 strong{color:#fff;font-weight:700}' +
            '.md-p em{color:#d8c8f0;font-style:italic}' +
            // Blockquotes
            'blockquote{margin:12px 0;padding:10px 16px;border-left:3px solid rgba(138,180,248,0.5);background:rgba(120,80,180,0.08);border-radius:0 8px 8px 0;color:#d0c8e0;font-style:italic}' +
            // Lists
            'ul, ol{margin:8px 0 8px 24px;line-height:1.7;color:#dcd0f0}' +
            'li{margin:4px 0}' +
            // Horizontal rule
            'hr{border:none;border-top:1px solid rgba(180,140,220,0.15);margin:20px 0}' +
            // Table
            'table{width:100%;border-collapse:collapse;margin:12px 0;font-size:12.5px}' +
            'th, td{border:1px solid rgba(180,140,220,0.15);padding:8px 12px;text-align:left}' +
            'th{background:rgba(120,80,180,0.15);font-weight:600;color:#e8e0f0}' +
            'td{color:#dcd0f0}' +
            'tr:nth-child(even) td{background:rgba(120,80,180,0.05)}';
        document.head.appendChild(s);
    }

    function GalaxyBrainPlugin() {
        var vaults = useState([]);
        var currentVault = useState(null);
        var graphData = useState(null);
        var loadError = useState(null);
        var selectedNode = useState(null);
        var nodeDetail = useState(null);
        var view = useState('graph'); // 'graph' | 'note'
        var isDark = useState(function() {
            try { return (localStorage.getItem(STORAGE_KEY) || 'dark') !== 'light'; }
            catch (e) { return true; }
        });
        var highlightedTag = useState(null);
        var loading = useState(true);
        var libsReady = useState(false);
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
                // Load Three.js first, then force-graph-3d (which may need THREE)
                return loadScript('/api/plugins/galaxy-brain/static/libs/three.min.js', 'THREE')
                    .then(function() { console.log('[galaxy-brain] THREE loaded:', typeof window.THREE); })
                    .then(function() {
                        return loadScript('/api/plugins/galaxy-brain/static/libs/react-force-graph-3d.umd.min.js', 'ForceGraph3D');
                    })
                    .then(function() { console.log('[galaxy-brain] window.ForceGraph3D:', typeof window.ForceGraph3D, 'THREE:', typeof window.THREE); if (!cancelled) libsReady[1](true); })
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
                view[1]('note');
                fetchJSON(BASE + '/node/' + encodeURIComponent(node.id) + '?vault=' + encodeURIComponent(currentVault[0])).then(nodeDetail[1]).catch(function() { return nodeDetail[1](null); });
            } else if (node.type === 'tag') {
                var tagName = node.name.replace(/^#/, '');
                highlightedTag[1](tagName);
                setTimeout(function() { return highlightedTag[1](null); }, 3000);
            }
        }, [currentVault[0]]);

        var handleBackToGraph = useCallback(function() {
            view[1]('graph');
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
        var contentStyle = view[0] === 'note' ? { flex: 1, overflow: 'auto', minHeight: 0 } : { flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' };
        var bgColor = isDark[0] ? BG_DARK : BG_LIGHT;
        var uiTextColor = isDark[0] ? '#e0e0e0' : '#111111';

        return React.createElement('div', { style: style },
            React.createElement('div', { style: headerStyle },
                React.createElement('div', { style: { display: 'flex', alignItems: 'center', gap: 12 } },
                    React.createElement('h1', { style: { fontSize: 20, fontWeight: 700, margin: 0 } }, 'GalaxyBrain'),
                    React.createElement('span', { style: { fontSize: 12, color: isDark[0] ? 'rgba(255,255,255,.4)' : 'rgba(0,0,0,.4)' } }, graphData[0].nodes.length + ' nodes \u00b7 ' + graphData[0].links.length + ' links')
                ),
                React.createElement('div', { style: { display: 'flex', alignItems: 'center', gap: 8 } },
                    vaults[0].length > 1 && React.createElement('select', { value: currentVault[0], onChange: function(e) { return currentVault[1](e.target.value); }, style: { padding: '4px 10px', borderRadius: 6, border: '1px solid ' + (isDark[0] ? 'rgba(255,255,255,.15)' : 'rgba(0,0,0,.15)'), background: isDark[0] ? 'rgba(255,255,255,.06)' : 'rgba(0,0,0,.06)', color: isDark[0] ? '#e5e5e5' : '#111', fontSize: 12, fontWeight: 600, cursor: 'pointer', outline: 'none' } }, vaults[0].map(function(v) { return React.createElement('option', { key: v.name, value: v.name }, v.name + ' (' + v.published_count + ' published)'); })),
                    React.createElement('button', { onClick: function() { return isDark[1](function(d) { return !d; }); }, style: { width: 32, height: 32, borderRadius: 6, border: '1px solid ' + (isDark[0] ? 'rgba(255,255,255,.15)' : 'rgba(0,0,0,.15)'), background: isDark[0] ? 'rgba(255,255,255,.06)' : 'rgba(0,0,0,.06)', color: isDark[0] ? '#e5e5e5' : '#111', cursor: 'pointer', fontSize: 14, display: 'flex', alignItems: 'center', justifyContent: 'center' }, title: 'Toggle theme' }, isDark[0] ? '\u2600\ufe0f' : '\uD83C\uDF19'),
                    view[0] === 'note' && React.createElement('button', { onClick: handleBackToGraph, style: { padding: '6px 12px', borderRadius: 6, border: '1px solid rgba(168,230,25,.3)', background: 'rgba(168,230,25,.15)', color: '#a8e619', cursor: 'pointer', fontSize: 12, fontWeight: 600 } }, '\u2190 Back to Graph')
                )
            ),
            React.createElement('div', { style: contentStyle },
                view[0] === 'graph' ? React.createElement(Graph3D, { nodes: graphData[0].nodes, links: graphData[0].links, onNodeClick: handleNodeClick, highlightedTag: highlightedTag[0], isDark: isDark[0], libsReady: libsReady[0] }) : (nodeDetail[0] ? React.createElement('div', { className: 'note-detail', style: { maxHeight: 'calc(100vh - 200px)', overflow: 'auto' }, onClick: handleDetailClick }, React.createElement('div', { className: 'note-meta' }, React.createElement('strong', null, nodeDetail[0].title || nodeDetail[0].id), nodeDetail[0].tags.map(function(t) { return React.createElement('span', { key: t, className: 'tag-badge' }, '#' + t); }), nodeDetail[0].frontmatter.graph_pinned && React.createElement('span', { className: 'tag-badge' }, '\uD83D\uDCCC Pinned'), nodeDetail[0].frontmatter.graph_collapsible && React.createElement('span', { className: 'tag-badge' }, '\uD83D\uDDC1 Collapsible'), (nodeDetail[0].aliases || []).map(function(a) { return React.createElement('span', { key: a, className: 'tag-badge', style: { background: 'rgba(255,170,0,.15)', borderColor: 'rgba(255,170,0,.3)', color: '#ffaa00' } }, '@' + a); })) , React.createElement('div', { dangerouslySetInnerHTML: { __html: renderedDetail } })) : React.createElement('div', { style: { padding: 16, color: uiTextColor, textAlign: 'center' } }, 'Loading note...'))
            )
        );
    }

    registry.register('galaxy-brain', GalaxyBrainPlugin);
    console.log('[galaxy-brain] Plugin registered');
})();