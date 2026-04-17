import {Component, Input} from '@angular/core';
import {CommonModule} from "@angular/common";
import {MatTableModule} from "@angular/material/table";
import {MatTabsModule} from "@angular/material/tabs";
import {MatIconModule} from "@angular/material/icon";
import {FormsModule} from "@angular/forms";
import {MatButtonToggle, MatButtonToggleGroup} from "@angular/material/button-toggle";
import {MatDividerModule} from "@angular/material/divider";
import {PtpInstanceType, PtpNode, PtpPortState} from "../../grpc/cnc";

@Component({
    selector: 'app-ptp',
    standalone: true,
    imports: [
        CommonModule,
        MatTableModule,
        MatTabsModule,
        MatIconModule,
        FormsModule,
        MatButtonToggleGroup,
        MatButtonToggle,
        MatDividerModule,
    ],
    templateUrl: './ptp.component.html',
    styleUrl: './ptp.component.scss',
})
export class PtpComponent {
    @Input() ptpNode!: PtpNode | undefined;

    nodePerfWindow: '15m' | '24h' = '15m';
    portPerfWindow: '15m' | '24h' = '15m';
    nodePerColumns = ['period', 'valid', 'offsetAvg', 'offsetMin', 'offsetMax'];
    portPerfColumns = ['period', 'valid', 'linkDelayAvg', 'linkDelayMin', 'linkDelayMax', 'linkDelayStddev'];

    get node(): PtpNode {
        return this.ptpNode ?? {
            currentDs: undefined,
            parentDs: undefined,
            defaultDs: undefined,
            ports: [],
            performanceRecords15M: [],
            performanceRecords24H: [],
        } as unknown as PtpNode;
    }

    ptpInstanceTypeLabel(type: PtpInstanceType | undefined): string {
        const map: Record<PtpInstanceType, string> = {
            [PtpInstanceType.PTP_INSTANCE_OC]: 'Ordinary Clock',
            [PtpInstanceType.PTP_INSTANCE_BC]: 'Boundary Clock',
            [PtpInstanceType.PTP_INSTANCE_P2P_TC]: 'P2P Transparent Clock',
            [PtpInstanceType.PTP_INSTANCE_E2E_TC]: 'E2E Transparent Clock',
            [PtpInstanceType.PTP_INSTANCE_INVALID]: 'Invalid',
            [PtpInstanceType.PTP_INSTANCE_TYPE_UNSPECIFIED]: 'Unspecified'
        };
        return map[type!] ?? 'Unknown';
    }

    ptpPortStateLabel(state: PtpPortState | undefined): string {
        const map: Record<PtpPortState, string> = {
            [PtpPortState.INITIALIZING]: 'Initializing',
            [PtpPortState.FAULTY]: 'Faulty',
            [PtpPortState.DISABLED]: 'Disabled',
            [PtpPortState.LISTENING]: 'Listening',
            [PtpPortState.PRE_TIME_TRANSMITTER]: 'Pre-Time Transmitter',
            [PtpPortState.TIME_TRANSMITTER]: 'Time Transmitter',
            [PtpPortState.PASSIVE]: 'Passive',
            [PtpPortState.UNCALIBRATED]: 'Uncalibrated',
            [PtpPortState.TIME_RECEIVER]: 'Time Receiver',
            [PtpPortState.UNSPECIFIED]: 'Unspecified',
        };
        return map[state!] ?? 'Unknown';
    }

    ptpPortStateClass(state: PtpPortState | undefined): string {
        if (state === PtpPortState.TIME_RECEIVER || state === PtpPortState.TIME_TRANSMITTER) return 'state-active';
        if (state === PtpPortState.FAULTY || state === PtpPortState.DISABLED) return 'state-error';
        return 'state-passive';
    }

    offsetSyncClass(offsetNs: bigint | number | undefined): string {
        const abs = Math.abs(Number(offsetNs));
        if (abs < 100) return 'sync-good';
        if (abs < 1000) return 'sync-warn';
        return 'sync-bad';
    }
}