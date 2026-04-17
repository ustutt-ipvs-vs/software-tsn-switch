import {CncNode} from "../grpc/cnc";

export interface InterfaceSaveEvent {
    node: CncNode;
    interfaceName: string;
}