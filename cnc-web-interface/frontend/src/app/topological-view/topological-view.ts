import {
    AfterViewInit,
    ChangeDetectionStrategy,
    ChangeDetectorRef,
    Component,
    ElementRef,
    Input,
    NgZone,
    OnDestroy,
    Output,
    ViewChild,
} from '@angular/core';
import {CommonModule} from '@angular/common';
import {Subject} from 'rxjs';
import * as d3 from 'd3';
import {CncNode, IfType, OperStatus, Topology, TopologyGraph} from '../grpc/cnc';

export type LayoutName = 'radial' | 'grid' | 'tree';

export interface EdgeTooltip {
    x: number;
    y: number;
    sourceNode: string;
    sourcePort: string;
    targetNode: string;
    targetPort: string;
    chassisId: string;
    managementIp: string;
}

interface PortDatum {
    id: string;
    label: string;
    lldpData?: any;
    x: number;
    y: number;
}

interface HostDatum {
    id: string;
    label: string;
    cncId: number;
    status: 'up' | 'down' | 'unknown';
    ports: PortDatum[];
    w: number;
    h: number;
    x: number;
    y: number;
}

interface EdgeDatum {
    id: string;
    srcPort: PortDatum;
    tgtPort: PortDatum;
    details?: any;
}

const HOST_PADDING = 14;
const PORT_W = 60;
const PORT_H = 22;
const PORT_GAP = 8;
const LABEL_H = 28;
const HOST_H_MIN = 80;
const CANVAS_PAD = 48;

const STATUS_COLOR: Record<string, string> = {
    up: '#22c55e',
    down: '#ef4444',
    unknown: '#94a3b8',
};

const LAYOUTS: LayoutName[] = ['radial', 'grid', 'tree'];
const LAYOUT_LABELS: Record<LayoutName, string> = {
    radial: 'Radial',
    grid: 'Grid',
    tree: 'Tree',
};

@Component({
    selector: 'app-topological-view',
    standalone: true,
    imports: [CommonModule],
    templateUrl: './topological-view.html',
    styleUrl: './topological-view.scss',
    changeDetection: ChangeDetectionStrategy.OnPush,
})
export class TopologicalView implements AfterViewInit, OnDestroy {
    @ViewChild('svgRef') svgRef!: ElementRef<SVGSVGElement>;
    @Input() set nodes(value: Topology) {
        this._nodes = value?.nodes ?? [];
        if (this._initialized) this._rebuild();
    }
    @Input() set graph(value: TopologyGraph) {
        this._graph = value;
        if (this._initialized) this._rebuild();
    }
    @Output() readonly nodeSelected = new Subject<number>();

    tooltip: EdgeTooltip | null = null;
    currentLayout: LayoutName = 'radial';
    readonly layouts = LAYOUTS;
    readonly layoutLabels = LAYOUT_LABELS;

    private _nodes: CncNode[] = [];
    private _graph: TopologyGraph = {nodes: [], edges: []};
    private _initialized = false;
    private _svg!: d3.Selection<SVGSVGElement, unknown, null, undefined>;
    private _g!: d3.Selection<SVGGElement, unknown, null, undefined>;
    private _resizeObserver?: ResizeObserver;
    private _destroyed = false;
    private _hosts: HostDatum[] = [];
    private _edges: EdgeDatum[] = [];
    private _zoom!: d3.ZoomBehavior<SVGSVGElement, unknown>;

    constructor(private zone: NgZone, private cdr: ChangeDetectorRef) {
    }

    ngAfterViewInit(): void {
        this.zone.runOutsideAngular(() => this._initSvg());
        this._initialized = true;
        if (this._nodes.length) this._rebuild();

        this._resizeObserver = new ResizeObserver(() => {
            if (!this._destroyed) this.zone.runOutsideAngular(() => this._fitViewBox());
        });
        this._resizeObserver.observe(this.svgRef.nativeElement);
    }

    ngOnDestroy(): void {
        this._destroyed = true;
        this._resizeObserver?.disconnect();
        this.nodeSelected.complete();
    }

    setLayout(name: LayoutName): void {
        this.currentLayout = name;
        this._rebuild();
    }

    private _initSvg(): void {
        this._svg = d3.select(this.svgRef.nativeElement)
            .attr('width', '100%')
            .attr('height', '100%');

        this._zoom = d3.zoom<SVGSVGElement, unknown>()
            .scaleExtent([0.15, 4])
            .on('zoom', (ev) => this._g.attr('transform', ev.transform));
        this._svg.call(this._zoom);
        this._svg.on('dblclick.zoom', null);

        this._svg.append('defs')
            .append('marker')
            .attr('id', 'topo-arrow')
            .attr('viewBox', '0 0 10 10')
            .attr('refX', 8).attr('refY', 5)
            .attr('markerWidth', 5).attr('markerHeight', 5)
            .attr('orient', 'auto-start-reverse')
            .append('path')
            .attr('d', 'M2 2L8 5L2 8')
            .attr('fill', 'none')
            .attr('stroke', '#94a3b8')
            .attr('stroke-width', 1.5)
            .attr('stroke-linecap', 'round')
            .attr('stroke-linejoin', 'round');

        this._g = this._svg.append('g').attr('class', 'scene');
    }

    private _buildModel(): { hosts: HostDatum[]; edges: EdgeDatum[] } {
        const hosts: HostDatum[] = [];
        const edges: EdgeDatum[] = [];
        const hostMap = new Map<string, HostDatum>();
        const portMap = new Map<string, PortDatum>();

        // 1. Process Managed Nodes
        for (const n of this._nodes) {
            const activePorts = (n.lldpAllData?.ports ?? []).filter((p: any) => p.neighbors.length > 0);
            const hostH = Math.max(HOST_H_MIN, LABEL_H + HOST_PADDING * 2 + PORT_H);
            const hostW = 240;

            const ports: PortDatum[] = activePorts.map((p: any, i: number) => {
                const totalW = activePorts.length * PORT_W + (activePorts.length - 1) * PORT_GAP;
                return {
                    id: `port-${n.id}-${p.name}`,
                    label: p.name,
                    lldpData: p.neighbors[0],
                    x: -totalW / 2 + i * (PORT_W + PORT_GAP) + PORT_W / 2,
                    y: hostH / 2 - HOST_PADDING - PORT_H / 2,
                };
            });

            const host: HostDatum = {
                id: `host-${n.id}`,
                label: n.hostName,
                cncId: n.id,
                status: this._nodeStatus(n),
                ports,
                w: hostW, h: hostH, x: 0, y: 0
            };

            hosts.push(host);
            hostMap.set(n.hostName.toLowerCase(), host);
            hostMap.set(n.ipAddress, host);
            ports.forEach(p => portMap.set(p.id, p));
        }

        // 2. Add Unmanaged Nodes
        for (const gNode of (this._graph?.nodes ?? [])) {
            const key = gNode.hostName.toLowerCase();
            if (hostMap.has(key)) continue;

            const host: HostDatum = {
                id: `unmanaged-${key}`,
                label: gNode.hostName || gNode.ipAddress,
                cncId: -1,
                status: 'unknown',
                ports: [],
                w: 200, h: HOST_H_MIN, x: 0, y: 0
            };
            hosts.push(host);
            hostMap.set(key, host);
        }

        // 3. Process Edges using Graph Data
        const seenEdges = new Set<string>();
        for (const gEdge of (this._graph?.edges ?? [])) {
            const srcH = hostMap.get(gEdge.localHostName.toLowerCase());
            const tgtH = hostMap.get(gEdge.remoteHostName.toLowerCase());
            if (!srcH || !tgtH) continue;

            const key = [srcH.id, tgtH.id].sort().join('|');
            if (seenEdges.has(key)) continue;
            seenEdges.add(key);

            const sPort = portMap.get(`port-${srcH.cncId}-${gEdge.localPort}`) ??
                { id: `center-${srcH.id}`, label: gEdge.localPort, x: 0, y: 0 };
            const tPort = portMap.get(`port-${tgtH.cncId}-${gEdge.remotePort}`) ??
                { id: `center-${tgtH.id}`, label: gEdge.remotePort, x: 0, y: 0 };

            edges.push({ id: `e-${key}`, srcPort: sPort, tgtPort: tPort, details: gEdge });
        }

        return { hosts, edges };
    }

    private _applyLayout(hosts: HostDatum[]): void {
        if (!hosts.length) return;

        switch (this.currentLayout) {

            case 'radial': {
                const r = Math.max(220, hosts.length * 85);
                hosts.forEach((h, i) => {
                    const angle = (i / hosts.length) * 2 * Math.PI - Math.PI / 2;
                    h.x = r * Math.cos(angle);
                    h.y = r * Math.sin(angle);
                });
                break;
            }

            case 'grid': {
                const cols = Math.ceil(Math.sqrt(hosts.length));
                const spX = Math.max(200, Math.max(...hosts.map(h => h.w)) + 60);
                const spY = Math.max(160, Math.max(...hosts.map(h => h.h)) + 80);
                hosts.forEach((h, i) => {
                    h.x = (i % cols) * spX - ((cols - 1) * spX) / 2;
                    h.y = Math.floor(i / cols) * spY - (Math.ceil(hosts.length / cols) - 1) * spY / 2;
                });
                break;
            }

            case 'tree': {
                const adj = new Map<string, Set<string>>();
                for (const h of hosts) adj.set(h.id, new Set());
                for (const e of this._edges) {
                    const srcH = hosts.find(h => h.ports.some(p => p.id === e.srcPort.id));
                    const tgtH = hosts.find(h => h.ports.some(p => p.id === e.tgtPort.id));
                    if (srcH && tgtH && srcH.id !== tgtH.id) {
                        adj.get(srcH.id)!.add(tgtH.id);
                        adj.get(tgtH.id)!.add(srcH.id);
                    }
                }
                const depth = new Map<string, number>();
                const queue = [hosts[0].id];
                depth.set(hosts[0].id, 0);
                while (queue.length) {
                    const cur = queue.shift()!;
                    for (const nb of adj.get(cur) ?? []) {
                        if (!depth.has(nb)) {
                            depth.set(nb, depth.get(cur)! + 1);
                            queue.push(nb);
                        }
                    }
                }
                const maxD = Math.max(0, ...depth.values());
                hosts.forEach(h => {
                    if (!depth.has(h.id)) depth.set(h.id, maxD + 1);
                });

                const byDepth = new Map<number, HostDatum[]>();
                hosts.forEach(h => {
                    const d = depth.get(h.id)!;
                    if (!byDepth.has(d)) byDepth.set(d, []);
                    byDepth.get(d)!.push(h);
                });

                const spY = Math.max(160, Math.max(...hosts.map(h => h.h)) + 90);
                byDepth.forEach((row, d) => {
                    const spX = Math.max(200, Math.max(...row.map(h => h.w)) + 60);
                    const totalW = (row.length - 1) * spX;
                    row.forEach((h, i) => {
                        h.x = i * spX - totalW / 2;
                        h.y = d * spY;
                    });
                });
                break;
            }
        }

        for (const h of hosts) {
            for (const p of h.ports) {
                p.x = h.x + p.x;
                p.y = h.y + p.y;
            }
        }

        for (const e of this._edges) {
            if (e.srcPort.id.startsWith('center-')) {
                const hostId = e.srcPort.id.replace('center-', '');
                const h = hosts.find(host => host.id === hostId);
                if (h) { e.srcPort.x = h.x; e.srcPort.y = h.y; }
            }
            if (e.tgtPort.id.startsWith('center-')) {
                const hostId = e.tgtPort.id.replace('center-', '');
                const h = hosts.find(host => host.id === hostId);
                if (h) { e.tgtPort.x = h.x; e.tgtPort.y = h.y; }
            }
        }
    }

    private _rebuild(): void {
        if (!this._g) return;
        this.zone.runOutsideAngular(() => {
            this._g.selectAll('*').remove();
            const {hosts, edges} = this._buildModel();
            this._hosts = hosts;
            this._edges = edges;
            this._applyLayout(hosts);
            this._draw(hosts, edges);
            this._fitViewBox();
        });
    }

    private _draw(hosts: HostDatum[], edges: EdgeDatum[]): void {

        this._g.append('g').attr('class', 'edges')
            .selectAll<SVGPathElement, EdgeDatum>('path')
            .data(edges, d => d.id)
            .join('path')
            .attr('fill', 'none')
            .attr('stroke', '#94a3b8')
            .attr('stroke-width', 1.5)
            .attr('marker-end', 'url(#topo-arrow)')
            .attr('d', d => this._edgePath(d))
            .style('cursor', 'pointer')
            .on('mouseover', (ev, d) => this._showEdgeTooltip(ev, d, hosts))
            .on('mouseout', () => this._hideTooltip());

        const hostSel = this._g.append('g').attr('class', 'hosts')
            .selectAll<SVGGElement, HostDatum>('g.host-node')
            .data(hosts, d => d.id)
            .join('g')
            .attr('class', 'host-node')
            .attr('transform', d => `translate(${d.x - d.w / 2},${d.y - d.h / 2})`)
            .style('cursor', 'pointer')
            .on('click', (_, d) => {
                if (d.cncId === -1) return;
                this.zone.run(() => this.nodeSelected.next(d.cncId));
            })

        hostSel.append('rect')
            .attr('class', 'host-card')
            .attr('width', d => d.w)
            .attr('height', d => d.h)
            .attr('rx', 8)
            .attr('fill', 'var(--surface-card, #ffffff)')
            .attr('stroke', 'var(--surface-border, #e2e8f0)')
            .attr('stroke-width', 1);

        hostSel.append('rect')
            .attr('width', 4)
            .attr('height', d => d.h)
            .attr('rx', 4)
            .attr('fill', d => STATUS_COLOR[d.status]);

        hostSel.filter(d => d.cncId === -1)
            .select('rect.host-card')
            .attr('stroke-dasharray', '4 3')
            .attr('fill', 'var(--surface-ground, #f8fafc)');

        hostSel.append('text')
            .attr('x', d => d.w / 2)
            .attr('y', LABEL_H / 2)
            .attr('text-anchor', 'middle')
            .attr('dominant-baseline', 'middle')
            .attr('fill', 'var(--text-color, #1e293b)')
            .attr('font-size', '12')
            .attr('font-weight', '600')
            .attr('font-family', 'Roboto, "Helvetica Neue", sans-serif')
            .text(d => d.label);

        hostSel.append('line')
            .attr('x1', 8).attr('x2', d => d.w - 8)
            .attr('y1', LABEL_H).attr('y2', LABEL_H)
            .attr('stroke', 'var(--surface-border, #e2e8f0)')
            .attr('stroke-width', 1);

        hostSel.each(function (host) {
            const portCount = host.ports.length;
            if (!portCount) return;

            const totalW = portCount * PORT_W + (portCount - 1) * PORT_GAP;
            const startX = (host.w - totalW) / 2;
            const py = LABEL_H + HOST_PADDING;

            const portG = d3.select(this).append('g').attr('class', 'ports');

            host.ports.forEach((port, i) => {
                const px = startX + i * (PORT_W + PORT_GAP);

                const pg = portG.append('g')
                    .attr('transform', `translate(${px},${py})`);

                pg.node()!.addEventListener('mouseover', (ev) => {
                    pg.dispatch('port-hover', {detail: {event: ev, port, host}, bubbles: false} as any);
                });

                pg.append('rect')
                    .attr('width', PORT_W)
                    .attr('height', PORT_H)
                    .attr('rx', 4)
                    .attr('fill', '#3b82f6')
                    .attr('stroke', 'rgba(59,130,246,0.3)')
                    .attr('stroke-width', 1);

                pg.append('text')
                    .attr('x', PORT_W / 2)
                    .attr('y', PORT_H / 2)
                    .attr('text-anchor', 'middle')
                    .attr('dominant-baseline', 'middle')
                    .attr('fill', '#ffffff')
                    .attr('font-size', '9')
                    .attr('font-weight', '500')
                    .attr('font-family', 'Roboto, "Helvetica Neue", sans-serif')
                    .text(port.label.length > 9 ? port.label.slice(0, 8) + '…' : port.label);
            });
        });

        this._g.selectAll<SVGGElement, HostDatum>('g.host-node')
            .each((host) => {
                const portGs = this._g.selectAll<SVGGElement, HostDatum>('g.host-node')
                    .filter(d => d.id === host.id)
                    .selectAll<SVGGElement, unknown>('g.ports > g');

                portGs.on('mouseover', (ev) => {
                    const el = ev.currentTarget as SVGGElement;
                    const idx = Array.from(el.parentElement!.children).indexOf(el);
                    const port = host.ports[idx];
                    if (port) this._showPortTooltip(ev, port, host);
                });
                portGs.on('mouseout', () => this._hideTooltip());
            });
    }

    private _edgePath(e: EdgeDatum): string {
        const x1 = e.srcPort.x, y1 = e.srcPort.y;
        const x2 = e.tgtPort.x, y2 = e.tgtPort.y;
        const dx = x2 - x1, dy = y2 - y1;
        const mx = (x1 + x2) / 2 - dy * 0.15;
        const my = (y1 + y2) / 2 + dx * 0.15;
        return `M${x1},${y1} Q${mx},${my} ${x2},${y2}`;
    }

    private _fitViewBox(): void {
        if (!this._hosts.length) return;
        let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
        for (const h of this._hosts) {
            minX = Math.min(minX, h.x - h.w / 2);
            minY = Math.min(minY, h.y - h.h / 2);
            maxX = Math.max(maxX, h.x + h.w / 2);
            maxY = Math.max(maxY, h.y + h.h / 2);
        }
        const vx = minX - CANVAS_PAD;
        const vy = minY - CANVAS_PAD;
        const vw = (maxX - minX) + CANVAS_PAD * 2;
        const vh = (maxY - minY) + CANVAS_PAD * 2;
        this._svg.attr('viewBox', `${vx} ${vy} ${vw} ${vh}`);
    }

    private _showPortTooltip(event: MouseEvent, port: PortDatum, host: HostDatum): void {
        const lldp = port.lldpData;
        if (!lldp) return;
        const rect = this.svgRef.nativeElement.getBoundingClientRect();
        this.zone.run(() => {
            this.tooltip = {
                x: event.clientX - rect.left,
                y: event.clientY - rect.top,
                sourceNode: host.label,
                sourcePort: port.label,
                targetNode: lldp.systemName ?? '',
                targetPort: lldp.portId ?? '',
                chassisId: lldp.chassisId ?? '',
                managementIp: lldp.managementIp ?? '',
            };
            this.cdr.detectChanges();
        });
    }

    private _showEdgeTooltip(event: MouseEvent, edge: EdgeDatum, hosts: HostDatum[]): void {
        const srcH = hosts.find(h => h.ports.some(p => p.id === edge.srcPort.id));
        const tgtH = hosts.find(h => h.ports.some(p => p.id === edge.tgtPort.id));
        const rect = this.svgRef.nativeElement.getBoundingClientRect();
        this.zone.run(() => {
            this.tooltip = {
                x: event.clientX - rect.left,
                y: event.clientY - rect.top,
                sourceNode: srcH?.label ?? '',
                sourcePort: edge.srcPort.label,
                targetNode: tgtH?.label ?? '',
                targetPort: edge.tgtPort.label,
                chassisId: edge.details?.chassisId ?? '',
                managementIp: edge.details?.managementIp ?? '',
            };
            this.cdr.detectChanges();
        });
    }

    private _hideTooltip(): void {
        this.zone.run(() => {
            this.tooltip = null;
            this.cdr.detectChanges();
        });
    }

    private _nodeStatus(node: CncNode): 'up' | 'down' | 'unknown' {
        let anyUp = false, anyDown = false;
        for (const iface of node.interfaces) {
            if (iface.type === IfType.LOOPBACK) continue;
            if (iface.operStatus === OperStatus.UP) anyUp = true;
            if (iface.operStatus === OperStatus.DOWN) anyDown = true;
        }
        if (anyUp) return 'up';
        if (anyDown) return 'down';
        return 'unknown';
    }
}