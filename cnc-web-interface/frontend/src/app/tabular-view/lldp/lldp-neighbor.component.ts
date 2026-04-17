import {Component, Input} from '@angular/core';
import {CommonModule} from "@angular/common";
import {LldpNode, LldpPort} from "../../grpc/cnc";

@Component({
    selector: 'app-lldp-neighbor',
    standalone: true,
    imports: [CommonModule],
    templateUrl: './lldp-neighbor.component.html',
    styleUrl: './lldp-neighbor.component.scss',
})
export class LldpNeighborComponent {
    @Input() lldpNode: LldpNode | undefined;
    @Input() lldpPort: LldpPort | undefined;

}