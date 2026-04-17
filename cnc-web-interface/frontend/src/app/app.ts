import {ChangeDetectorRef, Component, OnInit, ViewChild} from '@angular/core';
import {TabularView} from "./tabular-view/tabular-view";
import {MatToolbarModule} from "@angular/material/toolbar";
import {MatButtonModule} from "@angular/material/button";
import {MatIconModule} from "@angular/material/icon";
import {MatSidenavModule} from "@angular/material/sidenav";
import {TopologicalView} from "./topological-view/topological-view";
import {
    CncNode,
    GclConfig, IetfInterface,
    SetInterfaceScheduleRequest,
    SetNodeScheduleRequest,
    Topology, TopologyGraph
} from "./grpc/cnc";
import {CncService} from "./grpc/CncService";
import {MOCK_TOPOLOGY} from "./test-data/topology-data-mock";
import {InterfaceSaveEvent} from "./models/dataTypes";
import {forkJoin, from, interval, startWith, switchMap} from "rxjs";

@Component({
    selector: 'app-root',
    imports: [TabularView, MatToolbarModule, MatButtonModule, MatIconModule, MatSidenavModule, TopologicalView],
    templateUrl: './app.html',
    styleUrl: './app.scss'
})
export class App implements OnInit {
    @ViewChild(TabularView) tabularView!: TabularView;
    view: 'tabular' | 'topological' = 'topological';
    nodeData: Topology | undefined;
    topologyGraph: TopologyGraph | undefined;

    constructor(private cncService: CncService, private cdr: ChangeDetectorRef) {
    }

    ngOnInit() {
        // Update data every 5 minutes
        interval(360_000).pipe(
            startWith(0),
            switchMap(() => forkJoin({
                state: from(this.cncService.getNetworkState({})),
                graph: from(this.cncService.getTopologyGraph({}))
            }))
        ).subscribe(({ state, graph }) => {
            console.log('state: ', state)
            console.log('graph', graph)
            this.nodeData = state;
            this.topologyGraph = graph;
            this.cdr.detectChanges();
        });
    }

    onTopologyNodeSelected(nodeId: number) {
        this.view = 'tabular';
        this.cdr.detectChanges();
        // zero timeout so that change detection finally recognizes the change
        setTimeout(() => this.tabularView.scrollSelectedNodeIntoView(nodeId));
    }

    saveInterfaceData(saveEvent: InterfaceSaveEvent) {
        if (!this.nodeData) return;

        const idx = this.nodeData.nodes.findIndex(n => n.id === saveEvent.node.id);
        if (idx === -1) return;
        this.nodeData.nodes[idx] = saveEvent.node;

        const changedIntf: IetfInterface | undefined = saveEvent.node.interfaces.find(iface => iface.name === saveEvent.interfaceName);

        if (changedIntf?.bridgePort?.gateParameterTable) {
            const newAdminGcl = this.mapOperToAdminData(changedIntf.bridgePort.gateParameterTable);
            newAdminGcl.gateEnabled = true;
            newAdminGcl.operControlList.forEach((entry) => entry.operationName = 'sched:set-gate-states')
            const request: SetInterfaceScheduleRequest = {
                hostName: saveEvent.node.hostName,
                interfaceName: changedIntf.name,
                newAdminGcl: newAdminGcl,
            };
            console.log("Starting Interface save for schedule of interface:", changedIntf)
            console.log("Saved Admin GCL: ", newAdminGcl);
            this.cncService.setInterfaceSchedule(request).then(result => {
                console.log('Saved interface schedule: ', result);
            });
        }

        this.cdr.detectChanges();
    }
//todo set admin instead of oper data
    onNodeSave(node: CncNode) {
        if (!this.nodeData) return;

        const idx = this.nodeData.nodes.findIndex(n => n.id === node.id);
        if (idx === -1) return;
        this.nodeData.nodes[idx] = node;

        const request: SetNodeScheduleRequest = {
            hostName: node.hostName,
            interfaces: node.interfaces
        }
        this.cncService.setNodeSchedule(request).then(result => {
            console.log('Saved all schedules for node: ', result)
        })
    }

    mapOperToAdminData(gclConfig: GclConfig): GclConfig {
        return {
            ...gclConfig,
            adminCycleTime: gclConfig.operCycleTime,
            adminCycleTimeExtensionNs: gclConfig.operCycleTimeExtensionNs,
            adminControlList: [...(gclConfig.operControlList ?? [])],
            adminBaseTime: gclConfig.operBaseTime,
            adminGateStates: gclConfig.operGateStates,
        };
    }

    protected readonly MOCK_TOPOLOGY = MOCK_TOPOLOGY;
}
