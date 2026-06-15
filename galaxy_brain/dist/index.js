// GalaxyBrain Plugin for Hermes Dashboard - Working 3d-force-graph Imperative API
(function() {
    'use strict';

    async function loadScript(url) {
        return new Promise((resolve, reject) => {
            const script = document.createElement('script');
            script.src = url;
            script.onload = resolve;
            script.onerror = reject;
            document.head.appendChild(script);
        });
    }

    async function loadLibs() {
        console.log('[galaxy-brain] Loading 3D libraries...');
        await loadScript('/api/plugins/galaxy-brain/static/libs/three.min.js');
        console.log('[galaxy-brain] Three.js loaded');
        await loadScript('/api/plugins/galaxy-brain/static/libs/3d-force-graph.umd.min.js');
        console.log('[galaxy-brain] 3d-force-graph loaded');

        const FG = window.ForceGraph3D || (window.ForceGraph3D && window.ForceGraph3D.default);
        if (!FG || typeof FG !== 'function') {
            console.error('[galaxy-brain] ForceGraph3D not a function:', typeof FG);
            throw new Error('ForceGraph3D not loaded');
        }

        console.log('[galaxy-brain] window.THREE:', !!window.THREE);
        console.log('[galaxy-brain] window.ForceGraph3D:', !!FG);
        console.log('[galaxy-brain] ForceGraph3D type:', typeof FG);
        return FG;
    }

    let ForceGraph3D = null;
    loadLibs().then(f => { ForceGraph3D = f; }).catch(e => console.error('[galaxy-brain] Library load failed:', e));

    const React = window.React;
    const useState = React.useState;
    const useEffect = React.useEffect;
    const useRef = React.useRef;
    const useMemo = React.useMemo;
    const useCallback = React.useCallback;

    // Parse URL params for highlight/focus
    const urlParams = new URLSearchParams(window.location.search);
    const highlightTag = urlParams.get('highlight');
    const focusNodeId = urlParams.get('focus');

    function Graph3D({ nodes, links, width, height, onNodeClick, onNodeRightClick, selectedNodeId, isDark, highlightTag, focusNodeId }) {
        const containerRef = useRef(null);
        const fgRef = useRef(null);
        const initializedRef = useRef(false);
        var isDarkRef = useRef(isDark);
        isDarkRef.current = isDark;

        // Refs for highlight state (so nodeThreeObject callback can read current values)
        const highlightTagRef = useRef(highlightTag);
        highlightTagRef.current = highlightTag;
        const highlightedNodeIdsRef = useRef(new Set());
        const pulsingLightsRef = useRef(new Map());

        if (!nodes || !links || !ForceGraph3D) {
            return React.createElement('div', { ref: containerRef, style: { width: '100%', height: '400px', display: 'flex', alignItems: 'center', justifyContent: 'center', background: isDark ? '#0d0d0d' : '#f5f5f5', borderRadius: '8px', color: '#888' } }, 'Loading 3D graph...');
        }

        const graphData = useMemo(() => ({ nodes, links }), [nodes, links]);
        const validNodes = useMemo(() => new Set(nodes.map(n => n.id)), [nodes]);

        const filteredLinks = useMemo(() => links.filter(l => validNodes.has(l.source) && validNodes.has(l.target)), [links, validNodes]);

        if (filteredLinks.length !== links.length) {
            console.warn('[galaxy-brain] Filtered out', links.length - filteredLinks.length, 'invalid links');
        }

        // Compute highlighted node IDs (tag + its connected notes)
        const highlightedNodeIds = useMemo(() => {
            if (!highlightTag || !nodes || !links) return new Set();
            const s = new Set();
            s.add(highlightTag);
            links.forEach(l => {
                const src = typeof l.source === 'object' ? l.source.id : l.source;
                const tgt = typeof l.target === 'object' ? l.target.id : l.target;
                if (src === highlightTag) s.add(tgt);
                if (tgt === highlightTag) s.add(src);
            });
            return s;
        }, [highlightTag, nodes, links]);

        // Keep refs in sync
        useEffect(() => { highlightedNodeIdsRef.current = highlightedNodeIds; }, [highlightedNodeIds]);

        function defNodeThreeObject(fg) {
            fg.nodeThreeObject((node) => {
                const THREE = window.THREE;
                if (!THREE) return null;
                const size = Math.max(5, (node.val || 5));

                // Highlight state - use refs for current values
                const currentHighlightTag = highlightTagRef.current;
                const currentHighlightedIds = highlightedNodeIdsRef.current;
                const isHighlightedTag = node.id === currentHighlightTag;
                const isConnected = currentHighlightTag !== null && currentHighlightedIds.has(node.id);
                const isDimmed = currentHighlightTag !== null && !isConnected && !isHighlightedTag;

                if (node.type === 'ghost') {
                    const canvas = document.createElement('canvas');
                    canvas.width = 64; canvas.height = 64;
                    const ctx = canvas.getContext('2d');
                    ctx.font = 'bold 40px Arial';
                    ctx.fillStyle = (node.color && node.color.match(/^#[0-9a-fA-F]{6}$/)) ? node.color : '#888888';
                    ctx.textAlign = 'center';
                    ctx.textBaseline = 'middle';
                    ctx.fillText('+', 32, 36);
                    const texture = new THREE.CanvasTexture(canvas);
                    const material = new THREE.SpriteMaterial({ map: texture, transparent: true, opacity: 0.8, depthTest: false });
                    const sprite = new THREE.Sprite(material);
                    sprite.scale.set(size * 1.5, size * 1.5, 1);
                    sprite.renderOrder = -1;
                    return sprite;
                }

                let geometry;
                switch (node.type) {
                    case 'file': geometry = new THREE.SphereGeometry(size, 16, 16); break;
                    case 'tag': geometry = new THREE.OctahedronGeometry(size, 0); break;
                    default: geometry = new THREE.SphereGeometry(size, 16, 16);
                }

                var isDark = isDarkRef.current !== undefined ? isDarkRef.current : true;
                const matColor = new THREE.Color(node.color || '#3498db');
                const material = new THREE.MeshStandardMaterial({
                    color: matColor,
                    metalness: 0.1,
                    roughness: 0.7,
                    emissive: isDark ? new THREE.Color(0x111111) : new THREE.Color(0x000000),
                    emissiveIntensity: 0.2
                });
                const mesh = new THREE.Mesh(geometry, material);
                mesh.castShadow = true;
                mesh.receiveShadow = true;

                // Scale up highlighted / connected nodes
                if (isHighlightedTag || isConnected) {
                    mesh.scale.multiplyScalar(1.35);
                }

                // Dim non-highlighted nodes when a tag is active
                if (isDimmed) {
                    mesh.traverse((child) => {
                        if (child.isMesh) {
                            const mat = child.material;
                            if (mat) { mat.transparent = true; mat.opacity = 0.15; }
                        }
                    });
                }

                // Emissive glow on the highlighted tag itself
                if (isHighlightedTag) {
                    mesh.traverse((child) => {
                        if (child.isMesh) {
                            const mat = child.material;
                            if (mat) {
                                mat.emissive = new THREE.Color(node.color);
                                mat.emissiveIntensity = 0.7;
                            }
                        }
                    });
                    // PointLight that will be pulsed by the RAF loop
                    const light = new THREE.PointLight(node.color, 4, 60);
                    pulsingLightsRef.current.set(node.id, light);
                    const group = new THREE.Group();
                    group.add(mesh);
                    group.add(light);
                    return group;
                } else {
                    pulsingLightsRef.current.delete(node.id);
                    return mesh;
                }
            });
        }

        function defLinkThreeObject(fg) {
            fg.linkThreeObject((link) => {
                const THREE = window.THREE;
                if (!THREE) return null;
                const color = (link.color && link.color.match(/^#[0-9a-fA-F]{6}$/)) ? link.color : '#ffffff';
                const material = new THREE.LineBasicMaterial({ color: new THREE.Color(color), linewidth: 2, transparent: true, opacity: 0.6 });
                return new THREE.Line(new THREE.BufferGeometry(), material);
            });
        }

        // Refresh node visuals when highlightTag changes
        useEffect(() => {
            if (fgRef.current && initializedRef.current) {
                defNodeThreeObject(fgRef.current);
                // Force graph to refresh node objects
                fgRef.current.graphData(fgRef.current.graphData());
            }
        }, [highlightTag]);

        useEffect(() => {
            if (!containerRef.current || !ForceGraph3D || initializedRef.current) return;

            const el = containerRef.current;
            console.log('[galaxy-brain] Initializing ForceGraph3D (imperative) on element:', el, 'dimensions:', { width, height });

            try {
                const fg = ForceGraph3D()
                    .graphData({ nodes, links: filteredLinks })
                    .width(width)
                    .height(height)
                    .nodeId('id')
                    .nodeLabel('label')
                    .nodeVal('valence')
                    .nodeColor('color')
                    .nodeAutoColorBy(undefined)
                    .linkSource('source')
                        .linkTarget('target')
                        .linkColor(function() { return '#ffffff'; })
                        .linkWidth(1)
                        .enableNavigationControls(true)
                        .controlType('orbit')
                        .backgroundColor(isDark ? '#0d0d0d' : '#f5f5f5');

                defNodeThreeObject(fg);
                defLinkThreeObject(fg);

                fg.onNodeClick(onNodeClick);
                if (onNodeRightClick) fg.onNodeRightClick(onNodeRightClick);
                fg.onNodeHover(n => el.style.cursor = n ? 'pointer' : 'default');

                fg(el);
                fgRef.current = fg;
                initializedRef.current = true;
                console.log('[galaxy-brain] ForceGraph3D initialized successfully');

                // RAF loop for pulsing lights
                let rafId;
                const animateLights = () => {
                    const t = Date.now() / 1000;
                    pulsingLightsRef.current.forEach((light) => {
                        light.intensity = 3 + 2 * Math.sin(t * 3);
                    });
                    rafId = requestAnimationFrame(animateLights);
                };
                animateLights();

                // Auto-focus on focusNodeId prop after sim settles
                if (focusNodeId) {
                    setTimeout(() => {
                        if (!fgRef.current) return;
                        const found = nodes.find((n) => n.id === focusNodeId);
                        if (found && found.x != null) {
                            const dist = 80;
                            const mag = Math.hypot(found.x, found.y ?? 0, found.z ?? 0) || 1;
                            const ratio = 1 + dist / mag;
                            fgRef.current.cameraPosition(
                                { x: found.x * ratio, y: (found.y ?? 0) * ratio, z: (found.z ?? 0) * ratio },
                                { x: found.x, y: found.y ?? 0, z: found.z ?? 0 },
                                1500,
                            );
                        }
                    }, 4000);
                }

                setTimeout(() => { try { fg.zoomToFit(); } catch (e) {} }, 100);
            } catch (e) {
                console.error('[galaxy-brain] Initialization error:', e);
                initializedRef.current = false;
            }

            return function() {
                console.log('[galaxy-brain] Cleaning up ForceGraph3D');
                cancelAnimationFrame(rafId);
                try { fgRef.current && fgRef.current._destructor && fgRef.current._destructor(); } catch (e) {}
                fgRef.current = null;
                initializedRef.current = false;
            };
        }, []);

        const bgColor = useMemo(function() { return (isDarkRef.current ? '#0d0d0d' : '#f5f5f5'); }, [isDark]);

        useEffect(() => {
            if (fgRef.current) {
                try { fgRef.current.backgroundColor(bgColor); } catch (e) {}
            }
        }, [bgColor]);

        useEffect(() => {
            if (fgRef.current) {
                try { fgRef.current.graphData({ nodes, links: filteredLinks }); } catch (e) {}
            }
        }, [nodes, filteredLinks]);

        useEffect(() => {
            if (fgRef.current) {
                try { fgRef.current.width(width).height(height); } catch (e) {}
            }
        }, [width, height]);

        return React.createElement('div', {
            ref: containerRef,
            style: { width: '100%', height: '100%', minHeight: '400px', background: isDark ? '#0d0d0d' : '#f5f5f5', borderRadius: '8px', overflow: 'hidden' }
        });
    }

    function GalaxyBrainPlugin() {
        const [vaults, setVaults] = useState([]);
        const [selectedVault, setSelectedVault] = useState(null);
        const [graphData, setGraphData] = useState({ nodes: [], links: [] });
        const [loading, setLoading] = useState(false);
        const [nodeDetail, setNodeDetail] = useState(null);
        const [collapsedNodes, setCollapsedNodes] = useState(new Set());
        const [containerSize, setContainerSize] = useState({ width: 800, height: 500 });
        const [isDark, setIsDark] = useState(() => document.documentElement.dataset.theme === 'dark');

        const graphContainerRef = useRef(null);

        useEffect(() => {
            const observer = new ResizeObserver(function(entries) {
                for (let i = 0; i < entries.length; i++) {
                    const entry = entries[i];
                    const width = entry.contentRect.width;
                    const height = entry.contentRect.height;
                    setContainerSize({ width: Math.max(width, 100), height: Math.max(height, 100) });
                }
            });
            if (graphContainerRef.current && graphContainerRef.current.parentElement) {
                observer.observe(graphContainerRef.current.parentElement);
            }
            return () => observer.disconnect();
        }, []);

        useEffect(() => {
            const updateTheme = () => {
                setIsDark(document.documentElement.dataset.theme === 'dark');
            };
            updateTheme();
            const observer = new MutationObserver(updateTheme);
            observer.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
            return () => observer.disconnect();
        }, []);

        useEffect(() => {
            async function fetchVaults() {
                try {
                    const res = await fetch('/api/plugins/galaxy-brain/vaults', { credentials: 'include' });
                    if (res.ok) {
                        const data = await res.json();
                        setVaults(data.vaults || []);
                        if (data.vaults && data.vaults.length && !selectedVault) setSelectedVault(data.vaults[0]);
                    }
                } catch (e) { console.error('[galaxy-brain] Failed to fetch vaults:', e); }
            }
            fetchVaults();
        }, [selectedVault]);

        useEffect(() => {
            if (!selectedVault) return;
            async function fetchGraph() {
                setLoading(true);
                try {
                    const res = await fetch('/api/plugins/galaxy-brain/graph?vault=' + encodeURIComponent(selectedVault), { credentials: 'include' });
                    if (res.ok) {
                        const data = await res.json();
                        console.log('[galaxy-brain] Graph data received:', data.nodes ? data.nodes.length : 0, 'nodes,', data.links ? data.links.length : 0, 'links');
                        if (data.nodes) console.log('[galaxy-brain] Node IDs:', data.nodes.map(n => n.id));
                        if (data.links) console.log('[galaxy-brain] Link sources:', data.links.map(l => l.source));
                        if (data.links) console.log('[galaxy-brain] Link targets:', data.links.map(l => l.target));
                        if (data.nodes) console.log('[galaxy-brain] Colors:', data.nodes.map(n => n.color));
                        setGraphData({ nodes: data.nodes || [], links: data.links || [] });
                    }
                } catch (e) { console.error('[galaxy-brain] Failed to fetch graph:', e); }
                finally { setLoading(false); }
            }
            fetchGraph();
        }, [selectedVault]);

        const visibleNodes = useMemo(() => {
            if (collapsedNodes.size === 0) {
                console.log('[galaxy-brain] No collapsedNodes, returning all nodes:', graphData.nodes ? graphData.nodes.length : 0);
                return graphData.nodes || [];
            }
            const expanded = graphData.nodes ? graphData.nodes.filter(n => !collapsedNodes.has(n.id)) : [];
            console.log('[galaxy-brain] visibleNodes:', expanded.length, '/', (graphData.nodes ? graphData.nodes.length : 0), 'expanded:', expanded.length);
            return expanded;
        }, [graphData.nodes, collapsedNodes]);

        const handleNodeClick = (node) => {
            if (!node) return;
            console.log('[galaxy-brain] Node clicked:', node.id, node.type);
            if (node.type === 'file') {
                fetch('/api/plugins/galaxy-brain/notes/' + encodeURIComponent(node.id) + '?vault=' + encodeURIComponent(selectedVault), { credentials: 'include' })
                    .then(r => r.json())
                    .then(data => {
                        console.log('[galaxy-brain] Node detail loaded:', data.title);
                        setNodeDetail(data);
                    })
                    .catch(e => console.error('[galaxy-brain] Failed to load node detail:', e));
            } else if (node.type === 'tag') {
                const newCollapsed = new Set(collapsedNodes);
                if (newCollapsed.has(node.id)) {
                    newCollapsed.delete(node.id);
                } else {
                    newCollapsed.add(node.id);
                }
                setCollapsedNodes(newCollapsed);
            }
        };

        // Shift+Right-click → focus camera on node (avoids orbit controls conflict)
        const handleNodeRightClick = useCallback((node, event) => {
            if (!node || !fgRef.current || !event.shiftKey) return;
            event.preventDefault();
            event.stopPropagation();
            const n = node;
            if (n.x == null) return;
            const dist = 60;
            const mag = Math.hypot(n.x, n.y ?? 0, n.z ?? 0) || 1;
            const ratio = 1 + dist / mag;
            fgRef.current.cameraPosition(
                { x: n.x * ratio, y: (n.y ?? 0) * ratio, z: (n.z ?? 0) * ratio },
                { x: n.x, y: n.y ?? 0, z: n.z ?? 0 },
                1200,
            );
        }, []);

        if (!ForceGraph3D) {
            return React.createElement('div', { style: { padding: '20px', color: '#888', textAlign: 'center' } }, 'Loading 3D libraries...');
        }

        return React.createElement('div', { style: { display: 'flex', flexDirection: 'column', height: '100%', background: 'transparent', fontFamily: 'system-ui, -apple-system, sans-serif' } },
            React.createElement('div', { style: { display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', marginBottom: '16px', flexWrap: 'wrap', gap: '12px' } },
                React.createElement('div', null,
                    React.createElement('h2', { style: { margin: '0 0 4px', fontSize: '20px', color: '#fff' } }, 'GalaxyBrain'),
                    React.createElement('div', { style: { color: '#888', fontSize: '13px' } }, (graphData.nodes ? graphData.nodes.length : 0), ' nodes \u00b7 ', (graphData.links ? graphData.links.length : 0), ' links'),
                    React.createElement('div', { style: { display: 'flex', gap: '16px', marginTop: '8px', flexWrap: 'wrap' } },
                        React.createElement('span', { style: { display: 'flex', alignItems: 'center', gap: '6px', color: '#3498db', fontSize: '12px' } }, React.createElement('span', { style: { width: '10px', height: '10px', borderRadius: '50%', background: '#3498db', display: 'inline-block' } }), 'Notes'),
                        React.createElement('span', { style: { display: 'flex', alignItems: 'center', gap: '6px', color: '#e74c3c', fontSize: '12px' } }, React.createElement('span', { style: { width: '10px', height: '10px', borderRadius: '50%', background: '#e74c3c', display: 'inline-block' } }), 'Tags'),
                        React.createElement('span', { style: { display: 'flex', alignItems: 'center', gap: '6px', color: '#888', fontSize: '12px' } }, React.createElement('span', { style: { width: '10px', height: '10px', borderRadius: '50%', background: '#888', display: 'inline-block' } }), 'Ghost (unlinked)')
                    )
                ),
                React.createElement('div', { style: { display: 'flex', gap: '8px', alignItems: 'center' } },
                    React.createElement('select', {
                        value: selectedVault,
                        onChange: e => setSelectedVault(e.target.value),
                        style: { padding: '6px 12px', borderRadius: '4px', background: '#2a2a2a', color: '#fff', border: '1px solid #444', minWidth: '200px' }
                    }, vaults.map(v => React.createElement('option', { key: v, value: v }, v)))
                )
            ),

            React.createElement('div', { ref: graphContainerRef, style: { flex: 1, display: 'flex', flexDirection: 'column', minHeight: '400px', background: 'transparent', position: 'relative' } },
                React.createElement('div', { style: { width: '100%', height: '100%', minHeight: '400px', background: 'transparent', position: 'relative' } },
                    React.createElement(Graph3D, {
                        nodes: visibleNodes,
                        links: graphData.links || [],
                        width: containerSize.width,
                        height: containerSize.height,
                        onNodeClick: handleNodeClick,
                        onNodeRightClick: handleNodeRightClick,
                        selectedNodeId: nodeDetail && nodeDetail.id,
                        isDark: isDark,
                        highlightTag: highlightTag,
                        focusNodeId: focusNodeId
                    })
                )
            ),

            nodeDetail && React.createElement('div', { style: { marginTop: '16px', padding: '16px', background: '#1e1e1e', borderRadius: '8px', border: '1px solid #333', color: '#e0e0e0' } },
                React.createElement('div', { style: { display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' } },
                    React.createElement('h3', { style: { margin: 0, color: nodeDetail.type === 'file' ? '#3498db' : '#e74c3c' } }, nodeDetail.title || nodeDetail.id),
                    React.createElement('button', { onClick: function() { setNodeDetail(null); }, style: { background: 'none', border: 'none', color: '#888', fontSize: '18px', cursor: 'pointer', padding: '4px' } }, '\u00d7')
                ),
                React.createElement('p', { style: { margin: 0, whiteSpace: 'pre-wrap', fontFamily: 'monospace', fontSize: '13px', lineHeight: '1.5' } }, nodeDetail.content || nodeDetail.body || '(no content)')
            )
        );
    }

    function registerPlugin() {
        if (window.HermesDashboard && window.HermesDashboard.registerSidebarPlugin) {
            window.HermesDashboard.registerSidebarPlugin({
                id: 'galaxy-brain',
                name: 'GalaxyBrain',
                icon: 'network',
                component: GalaxyBrainPlugin,
                weight: 10
            });
            console.log('[galaxy-brain] Plugin registered via HermesDashboard API');
        } else {
            setTimeout(registerPlugin, 100);
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', registerPlugin);
    } else {
        registerPlugin();
    }
})();