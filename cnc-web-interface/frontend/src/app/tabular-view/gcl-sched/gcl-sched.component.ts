import {Component, Input} from '@angular/core';
import {MatTableModule} from "@angular/material/table";
import {MatFormField, MatInput} from "@angular/material/input";
import {FormsModule} from "@angular/forms";
import {MatIcon} from "@angular/material/icon";
import {MatButtonModule} from "@angular/material/button";
import {CommonModule} from "@angular/common";
import {GclEntry, IetfInterface} from "../../grpc/cnc";

@Component({
    selector: 'app-gcl-schedule',
    standalone: true,
    imports: [
        CommonModule,
        MatTableModule,
        MatFormField,
        MatInput,
        FormsModule,
        MatIcon,
        MatButtonModule,
    ],
    templateUrl: './gcl-sched.component.html',
    styleUrl: './gcl-sched.component.scss',
})
export class GclSchedComponent {
    @Input() intf!: IetfInterface;
    @Input() editMode = false;

    getCycleTimeNs(): number {
        const num = this.intf.bridgePort?.gateParameterTable?.operCycleTime?.numerator ?? 0;
        const den = this.intf.bridgePort?.gateParameterTable?.operCycleTime?.denominator ?? 0;
        if (den === 0) return 0;
        return Math.round((num / den) * 1_000_000_000);
    }

    getIntervalSum(): number {
        return this.intf.bridgePort?.gateParameterTable?.operControlList
            ?.reduce((sum, entry) => sum + entry.timeIntervalValue, 0) ?? 0;
    }

    isCycleTimeValid(): boolean {
        const tolerance = 1; // 1 ns tolerance
        return Math.abs(this.getIntervalSum() - this.getCycleTimeNs()) <= tolerance;
    }

    isBitSet(value: number, bit: number): boolean {
        return (value & (1 << bit)) !== 0;
    }

    toggleBit(entry: GclEntry, bit: number): void {
        entry.gateStatesValue ^= (1 << bit);
    }

    removeEntry(index: number): void {
        const list = this.intf.bridgePort!.gateParameterTable!.operControlList;
        list.splice(index, 1);
        this.intf.bridgePort!.gateParameterTable!.operControlList = [...list];
    }

    addEntry(): void {
        const list = this.intf.bridgePort!.gateParameterTable!.operControlList;
        this.intf.bridgePort!.gateParameterTable!.operControlList = [...list, {
            index: list.length,
            operationName: 'sched:set-gate-states',
            gateStatesValue: 0xFF,
            timeIntervalValue: 0
        }];
    }

    isLoopbackInterface(name: string) {
        return name === 'lo';
    }
}