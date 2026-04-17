import { Injectable } from '@angular/core';
import { GrpcWebFetchTransport } from '@protobuf-ts/grpcweb-transport';
import { CncServiceClient } from './cnc.client';
import {
    AdminGclResponse,
    type AllLldpDataResponse,
    AllPtpDataResponse,
    EmptyRequest, GclConfig, IetfInterface,
    InterfaceRequest, NodeRequest, OperGclResponse, type PtpNode, PtpPort,
    SetInterfaceScheduleRequest, SetNodeScheduleRequest,
    SetNodeScheduleResponse, Topology, TopologyGraph,
} from './cnc';

@Injectable({ providedIn: 'root' })
export class CncService {

    private client: CncServiceClient;

    constructor() {
        const transport = new GrpcWebFetchTransport({
            baseUrl: 'http://localhost:8080'
        });
        this.client = new CncServiceClient(transport);
    }

    async getNetworkState(nodeRequest: EmptyRequest): Promise<Topology> {
        const { response } = await this.client.getNetworkState(nodeRequest);
        return response;
    }

    async setNodeSchedule(nodeScheduleRequest: SetNodeScheduleRequest): Promise<SetNodeScheduleResponse> {
        const { response } = await this.client.setNodeSchedule(nodeScheduleRequest);
        return response;
    }

    async setInterfaceSchedule(interfaceScheduleRequest: SetInterfaceScheduleRequest): Promise<IetfInterface> {
        const { response } = await this.client.setInterfaceSchedule(interfaceScheduleRequest);
        return response;
    }

    async getAllPtpData(emptyRequest: EmptyRequest): Promise<AllPtpDataResponse> {
        const { response } = await this.client.getAllPtpData(emptyRequest);
        return response;
    }

    async getAllLldpData(emptyRequest: EmptyRequest): Promise<AllLldpDataResponse> {
        const { response } = await this.client.getAllLldpData(emptyRequest);
        return response;
    }

    async getNodePtpData(nodeRequest: NodeRequest): Promise<PtpNode> {
        const { response } = await this.client.getNodePtpData(nodeRequest);
        return response;
    }

    async getInterfacePtptData(interfaceRequest: InterfaceRequest): Promise<PtpPort> {
        const {response} = await this.client.getInterfacePtpData(interfaceRequest);
        return response;
    }

    async getInterfaceGcl(interfaceRequest: InterfaceRequest): Promise<GclConfig> {
        const {response} = await this.client.getInterfaceGcl(interfaceRequest);
        return response;
    }

    async getInterfaceAdminGcl(interfaceRequest: InterfaceRequest): Promise<AdminGclResponse> {
        const {response} = await this.client.getInterfaceAdminGcl(interfaceRequest);
        return response;
    }

    async getInterfaceOperGcl(interfaceRequest: InterfaceRequest): Promise<OperGclResponse> {
        const {response} = await this.client.getInterfaceOperGcl(interfaceRequest);
        return response;
    }

    async getTopologyGraph(emptyRequest: EmptyRequest): Promise<TopologyGraph> {
        const { response } = await this.client.getTopologyGraph(emptyRequest);
        return response;
    }
}