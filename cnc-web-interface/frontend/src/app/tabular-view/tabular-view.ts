import {Component, ElementRef, Input, Output, QueryList, ViewChildren} from '@angular/core';
import {MatCardModule} from "@angular/material/card";
import {MatButtonModule} from "@angular/material/button";
import {MatTabsModule} from "@angular/material/tabs";
import {MatTableModule} from "@angular/material/table";
import {CommonModule} from "@angular/common";
import {MatListModule} from "@angular/material/list";
import {FormsModule} from "@angular/forms";
import {LldpNeighborComponent} from "./lldp/lldp-neighbor.component";
import {PtpComponent} from "./ptp/ptp.component";
import {GclSchedComponent} from "./gcl-sched/gcl-sched.component";
import {Subject} from "rxjs";
import {CncNode, IetfInterface, LldpNode, Topology} from "../grpc/cnc";
import {MatIcon} from "@angular/material/icon";
import {InterfaceSaveEvent} from "../models/dataTypes";
import {MatTooltip} from "@angular/material/tooltip";

@Component({
    selector: 'app-tabular-view',
    standalone: true,
    imports: [
        CommonModule, MatCardModule, MatButtonModule, MatTabsModule,
        MatListModule, MatTableModule, FormsModule,
        GclSchedComponent, LldpNeighborComponent, PtpComponent, MatIcon, MatTooltip,
    ],
    templateUrl: './tabular-view.html',
    styleUrl: './tabular-view.scss',
})
export class TabularView {
    @Input() set topology(value: Topology | undefined) {
        this._topology = value;
        this.interfaceEditingKey = null;
        this.interfaceSnapshot = null;
        this.nodeEditingKey = null;
        this.nodeSnapshots.clear();
    }
    @Output() interfaceSave = new Subject<InterfaceSaveEvent>();
    @Output() nodeSave = new Subject<CncNode>();
    @ViewChildren('nodeAnchor') nodeAnchors!: QueryList<ElementRef<HTMLDivElement>>;
    get topology(): Topology | undefined { return this._topology; }
    private _topology: Topology | undefined;
    interfaceEditingKey: string | null = null;
    private interfaceSnapshot: IetfInterface | null = null;
    nodeEditingKey: number | null = null;
    private nodeSnapshots = new Map<number, CncNode>();

    // Interface edit
    enterInterfaceEditMode(nodeId: number, intf: IetfInterface) {
        this.interfaceEditingKey = this.editKey(nodeId, intf.name);
        this.interfaceSnapshot = structuredClone(intf);
    }

    cancelInterfaceEdit(node: CncNode, intfName: string) {
        if (this.interfaceSnapshot) {
            const idx = node.interfaces.findIndex(i => i.name === intfName);
            if (idx !== -1) node.interfaces[idx] = this.interfaceSnapshot;
        }
        this.interfaceEditingKey = null;
        this.interfaceSnapshot = null;
    }

    saveInterface(node: CncNode, intfName: string) {
        if (!this.isCycleTimeValidByName(node, intfName)) return;
        this.interfaceEditingKey = null;
        this.interfaceSnapshot = null;
        this.interfaceSave.next({ node, interfaceName: intfName });
    }

    isEditing(nodeId: number, interfaceName: string): boolean {
        return this.interfaceEditingKey === this.editKey(nodeId, interfaceName);
    }

    canSaveInterface(node: CncNode, intfName: string): boolean {
        return this.isCycleTimeValidByName(node, intfName);
    }

    // Node edit
    enterNodeEditMode(node: CncNode) {
        this.nodeEditingKey = node.id;
        this.nodeSnapshots.set(node.id, structuredClone(node));
    }

    cancelNodeEdit(node: CncNode) {
        const snapshot = this.nodeSnapshots.get(node.id);
        if (snapshot && this._topology) {
            const idx = this._topology.nodes.findIndex(n => n.id === node.id);
            if (idx !== -1) this._topology.nodes[idx] = snapshot;
        }
        this.nodeEditingKey = null;
        this.nodeSnapshots.delete(node.id);
    }

    saveNode(node: CncNode) {
        if (!this.canSaveNode(node)) return;

        const snapshot = this.nodeSnapshots.get(node.id);
        const nodeWithChangedInterfacesOnly: CncNode = {
            ...node,
            interfaces: snapshot ? this.getChangedInterfaces(node, snapshot) : node.interfaces
        };

        this.nodeEditingKey = null;
        this.nodeSnapshots.delete(node.id);
        this.nodeSave.next(nodeWithChangedInterfacesOnly);
    }

    private getChangedInterfaces(current: CncNode, snapshot: CncNode): IetfInterface[] {
        return current.interfaces.filter(intf => {
            const original = snapshot.interfaces.find(i => i.name === intf.name);
            return !original || JSON.stringify(intf) !== JSON.stringify(original);
        });
    }

    isNodeEditing(nodeId: number): boolean {
        return this.nodeEditingKey === nodeId;
    }

    canSaveNode(node: CncNode): boolean {
        return node.interfaces.every(intf => this.isCycleTimeValid(intf));
    }

    scrollSelectedNodeIntoView(nodeId: number) {
        const anchor = this.nodeAnchors.find(
            el => el.nativeElement.getAttribute('data-node-id') === String(nodeId)
        );
        anchor?.nativeElement.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }

    getCycleTimeNs(intf: IetfInterface): number {
        const num = intf.bridgePort?.gateParameterTable?.operCycleTime?.numerator ?? 0;
        const den = intf.bridgePort?.gateParameterTable?.operCycleTime?.denominator ?? 0;
        if (den === 0 && num === 0) return 0;
        return Math.round((num / den)  * 1_000_000_000);
    }

    getIntervalSum(intf: IetfInterface): number {
        return intf.bridgePort?.gateParameterTable?.operControlList
            ?.reduce((sum, entry) => sum + entry.timeIntervalValue, 0) ?? 0;
    }

    isCycleTimeValid(intf: IetfInterface): boolean {
        if (this.disableLoopbackInf(intf.name)) return true;
        const tolerance = 1;
        return Math.abs(this.getIntervalSum(intf) - this.getCycleTimeNs(intf)) <= tolerance;
    }

    private isCycleTimeValidByName(node: CncNode, intfName: string): boolean {
        const intf = node.interfaces.find(i => i.name === intfName);
        return intf ? this.isCycleTimeValid(intf) : false;
    }

    private editKey(nodeId: number, interfaceName: string): string {
        return `${nodeId}:${interfaceName}`;
    }

    disableLoopbackInf(interfaceName: string) {
        return interfaceName === 'lo';
    }

    interfaceLldpData(allLldpData: LldpNode, intfName: string) {
        return allLldpData.ports.find((port) => port.name === intfName);
    }
}