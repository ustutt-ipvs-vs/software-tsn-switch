import {
    CncNode,
    IfType,
    OperStatus,
    PtpInstanceType,
    PtpPortState,
    Topology,
} from '../grpc/cnc';

function macBytes(hex: string): Uint8Array {
    return new Uint8Array(hex.split(':').map(b => parseInt(b, 16)));
}

function makeGclEntry(index: number, gateStates: number, intervalNs: number) {
    return { index, gateStatesValue: gateStates, timeIntervalValue: intervalNs, operationName: 'SetGateStates' };
}

function makeGclConfig(cycleNs: number, entries: ReturnType<typeof makeGclEntry>[]) {
    return {
        operDataSet: true, adminDataSet: true,
        queueMaxSduTable: [
            { trafficClass: 0, queueMaxSdu: 1522, transmissionOverrun: 0n },
            { trafficClass: 1, queueMaxSdu: 1522, transmissionOverrun: 0n },
            { trafficClass: 2, queueMaxSdu: 1522, transmissionOverrun: 0n },
            { trafficClass: 3, queueMaxSdu: 4096, transmissionOverrun: 0n },
        ],
        gateEnabled: true,
        adminGateStates: 0xFF,
        adminCycleTime: { numerator: cycleNs, denominator: 1 },
        adminCycleTimeExtensionNs: 0,
        adminBaseTime: { seconds: 1710000000n, nanoseconds: 0 },
        adminControlList: entries,
        operGateStates: 0xFF,
        operCycleTime: { numerator: cycleNs, denominator: 1 },
        operCycleTimeExtensionNs: 0,
        operBaseTime: { seconds: 1710000000n, nanoseconds: 0 },
        operControlList: entries,
        configChange: false, configChangeTimeSeconds: 1710000000n,
        configChangeTimeNanoseconds: 0, configPending: false,
        configChangeError: 0n, tickGranularity: 8,
        currentTimeSeconds: 1710005000n, currentTimeNanoseconds: 0,
        clockId: 0, supportedListMax: 64, supportedIntervalMax: 999999999,
        supportedCycleMaxNumerator: 999999999, supportedCycleMaxDenominator: 1,
    };
}

const CYCLE_NS = 1_000_000;
const GCL_ENTRIES = [
    makeGclEntry(0, 0b10000000, 250_000),
    makeGclEntry(1, 0b01000000, 250_000),
    makeGclEntry(2, 0b00110000, 250_000),
    makeGclEntry(3, 0b00001111, 250_000),
];

function makePtpPerf15m(baseOffset: number) {
    return Array.from({ length: 8 }, (_, i) => ({
        periodComplete: true, pmTime: i, measurementValid: true,
        offsetFromTimeTransmitter: {
            avg: BigInt(baseOffset + i * 5), min: BigInt(baseOffset - 20 + i),
            max: BigInt(baseOffset + 80 + i * 3), stddev: 5n,
        },
        meanPathDelay: { avg: BigInt(1400 + i * 10), min: 1200n, max: 1800n, stddev: 50n },
    }));
}

function makePtpPortPerf15m(baseDelay: number) {
    return Array.from({ length: 8 }, (_, i) => ({
        pmTime: i,
        meanLinkDelay: {
            avg: BigInt(baseDelay + i * 10),
            min: BigInt(baseDelay - 200),
            max: BigInt(baseDelay + 400),
            stddev: 30n
        },
    }));
}

function makeLldpNeighbor(systemName: string, managementIp: string, chassisId: string, portId: string) {
    return { hasNeighbor: true, chassisId, portId, systemName, ttl: 120, managementIp };
}

function makeBridgeIface(ifindex: number, name: string, mac: string, operStatus: OperStatus = OperStatus.UP) {
    return {
        ifindex, name,
        lastLinkUpdateId: ifindex, lastQdiscUpdateId: ifindex,
        type: IfType.ETHERNET,
        adminEnabled: true, operStatus,
        hasPhysAddr: true, physAddress: macBytes(mac),
        mtu: 1500, numTxQueues: 8,
        numActiveTxQueues: operStatus === OperStatus.UP ? 8 : 0,
        speed: operStatus === OperStatus.UP ? 1000000000n : 0n,
        bridgePort: {
            bridgeName: 'br0', masterIndex: 1,
            trafficClassData: {
                mapDataSet: true,
                numTrafficClasses: 8,
                priorityMap: new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7])
            },
            gateParameterTable: makeGclConfig(CYCLE_NS, GCL_ENTRIES),
        },
    };
}

const CORE_SW_01: CncNode = {
    id: 1, hostName: 'core-sw-01', ipAddress: '10.0.0.1',
    interfaces: [
        makeBridgeIface(2, 'eth0', '00:11:22:33:44:02'),
        makeBridgeIface(3, 'eth1', '00:11:22:33:44:03'),
        makeBridgeIface(4, 'eth2', '00:11:22:33:44:04'),
    ],
    ptpAllData: {
        defaultDs: { numPorts: 3, priority1: 128, clockIdentity: '00:11:22:FF:FE:33:44:01', currentTime: { seconds: 1710005000n, nanoseconds: 0 }, instanceType: PtpInstanceType.PTP_INSTANCE_BC },
        currentDs: { stepsRemoved: 0, offsetFromTimeTransmitter: 12n, meanDelay: 980n },
        parentDs: { parentClockIdentity: '00:11:22:FF:FE:00:00:01', parentPortNumber: 1, grandParentClockIdentity: '00:00:00:FF:FE:00:00:00' },
        performanceRecords15M: makePtpPerf15m(30),
        performanceRecords24H: makePtpPerf15m(28),
        ports: [
            { portIndex: 1, underlyingInterface: 'eth0', portDs: { portClockIdentity: '00:11:22:FF:FE:33:44:01', portPortNumber: 1, portState: PtpPortState.TIME_RECEIVER, meanLinkDelay: 980n }, portPerformanceRecords15M: makePtpPortPerf15m(980), portPerformanceRecords24H: [] },
        ],
    },
    lldpAllData: {
        messageTxInterval: 30, messageTxHoldMultiplier: 4, messageFastTx: 1,
        localSystemData: { chassisIdSubtype: 'macAddress', chassisId: '00:11:22:33:44:01', systemName: 'core-sw-01', systemDescription: 'TSN Core', systemCapabilitiesSupported: 'bridge', systemCapabilitiesEnabled: 'bridge' },
        ports: [
            { name: 'eth0', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-01', '10.0.0.2', '00:11:22:33:55:01', 'eth0')] },
            { name: 'eth1', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-02', '10.0.0.3', '00:11:22:33:55:02', 'eth1')] },
            { name: 'eth2', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-02', '10.0.0.3', '00:11:22:33:55:02', 'eth2')] },
        ],
    },
};

const ACC_SW_01: CncNode = {
    id: 2, hostName: 'acc-sw-01', ipAddress: '10.0.0.2',
    interfaces: [
        makeBridgeIface(2, 'eth0', '00:11:22:33:55:11'),
        makeBridgeIface(3, 'eth1', '00:11:22:33:55:12', OperStatus.UP),
    ],
    ptpAllData: {
        defaultDs: { numPorts: 2, priority1: 128, clockIdentity: '00:11:22:FF:FE:33:55:01', currentTime: { seconds: 1710005000n, nanoseconds: 12 }, instanceType: PtpInstanceType.PTP_INSTANCE_BC },
        currentDs: { stepsRemoved: 1, offsetFromTimeTransmitter: 42n, meanDelay: 1050n },
        parentDs: { parentClockIdentity: '00:11:22:FF:FE:33:44:01', parentPortNumber: 2, grandParentClockIdentity: '00:11:22:FF:FE:00:00:01' },
        performanceRecords15M: makePtpPerf15m(42),
        performanceRecords24H: makePtpPerf15m(38),
        ports: [],
    },
    lldpAllData: {
        messageTxInterval: 30, messageTxHoldMultiplier: 4, messageFastTx: 1,
        localSystemData: { chassisIdSubtype: 'macAddress', chassisId: '00:11:22:33:55:01', systemName: 'acc-sw-01', systemDescription: 'Access 01', systemCapabilitiesSupported: 'bridge', systemCapabilitiesEnabled: 'bridge' },
        ports: [
            { name: 'eth0', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('core-sw-01', '10.0.0.1', '00:11:22:33:44:01', 'eth0')] },
            { name: 'eth1', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-02', '10.0.0.3', '00:11:22:33:55:02', 'eth2')] },
        ],
    },
};

const ACC_SW_02: CncNode = {
    id: 3, hostName: 'acc-sw-02', ipAddress: '10.0.0.3',
    interfaces: [
        makeBridgeIface(2, 'eth0', '00:11:22:33:55:21'),
        makeBridgeIface(3, 'eth1', '00:11:22:33:55:22', OperStatus.UP),
        makeBridgeIface(4, 'eth2', '00:11:22:33:55:23', OperStatus.UP),
    ],
    ptpAllData: {
        defaultDs: { numPorts: 3, priority1: 128, clockIdentity: '00:11:22:FF:FE:33:55:02', currentTime: { seconds: 1710005000n, nanoseconds: 18 }, instanceType: PtpInstanceType.PTP_INSTANCE_BC },
        currentDs: { stepsRemoved: 1, offsetFromTimeTransmitter: 38n, meanDelay: 995n },
        parentDs: { parentClockIdentity: '00:11:22:FF:FE:33:44:01', parentPortNumber: 2, grandParentClockIdentity: '00:11:22:FF:FE:00:00:01' },
        performanceRecords15M: makePtpPerf15m(38),
        performanceRecords24H: makePtpPerf15m(35),
        ports: [],
    },
    lldpAllData: {
        messageTxInterval: 30, messageTxHoldMultiplier: 4, messageFastTx: 1,
        localSystemData: { chassisIdSubtype: 'macAddress', chassisId: '00:11:22:33:55:02', systemName: 'acc-sw-02', systemDescription: 'Access 02', systemCapabilitiesSupported: 'bridge', systemCapabilitiesEnabled: 'bridge' },
        ports: [
            { name: 'eth0', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-03', '10.0.0.4', '00:11:22:33:55:03', 'eth0')] },
            { name: 'eth1', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('core-sw-01', '10.0.0.1', '00:11:22:33:44:01', 'eth1')] },
            { name: 'eth2', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('core-sw-01', '10.0.0.1', '00:11:22:33:44:01', 'eth2')] },
        ],
    },
};

const ACC_SW_03: CncNode = {
    id: 4, hostName: 'acc-sw-03', ipAddress: '10.0.0.4',
    interfaces: [makeBridgeIface(2, 'eth0', '00:11:22:33:55:31')],
    ptpAllData: {
        defaultDs: { numPorts: 1, priority1: 128, clockIdentity: '00:11:22:FF:FE:33:55:03', currentTime: { seconds: 1710005000n, nanoseconds: 55 }, instanceType: PtpInstanceType.PTP_INSTANCE_OC },
        currentDs: { stepsRemoved: 2, offsetFromTimeTransmitter: 65n, meanDelay: 1100n },
        parentDs: { parentClockIdentity: '00:11:22:FF:FE:33:55:02', parentPortNumber: 1, grandParentClockIdentity: '00:11:22:FF:FE:33:44:01' },
        performanceRecords15M: makePtpPerf15m(65),
        performanceRecords24H: makePtpPerf15m(60),
        ports: [],
    },
    lldpAllData: {
        messageTxInterval: 30, messageTxHoldMultiplier: 4, messageFastTx: 1,
        localSystemData: { chassisIdSubtype: 'macAddress', chassisId: '00:11:22:33:55:03', systemName: 'acc-sw-03', systemDescription: 'Access 03', systemCapabilitiesSupported: 'bridge', systemCapabilitiesEnabled: 'bridge' },
        ports: [
            { name: 'eth0', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-02', '10.0.0.3', '00:11:22:33:55:02', 'eth0')] },
        ],
    },
};

const ACC_SW_04: CncNode = {
    id: 5, hostName: 'acc-sw-04', ipAddress: '10.0.0.5',
    interfaces: [
        makeBridgeIface(2, 'eth0', '00:11:22:33:55:41'),
        makeBridgeIface(3, 'eth1', '00:11:22:33:55:42'),
        makeBridgeIface(4, 'eth2', '00:11:22:33:55:43'),
    ],
    ptpAllData: {
        defaultDs: { numPorts: 3, priority1: 128, clockIdentity: '00:11:22:FF:FE:33:55:04', currentTime: { seconds: 1710005000n, nanoseconds: 22 }, instanceType: PtpInstanceType.PTP_INSTANCE_BC },
        currentDs: { stepsRemoved: 1, offsetFromTimeTransmitter: 55n, meanDelay: 1030n },
        parentDs: { parentClockIdentity: '00:11:22:FF:FE:33:44:01', parentPortNumber: 4, grandParentClockIdentity: '00:11:22:FF:FE:00:00:01' },
        performanceRecords15M: makePtpPerf15m(55),
        performanceRecords24H: makePtpPerf15m(50),
        ports: [],
    },
    lldpAllData: {
        messageTxInterval: 30, messageTxHoldMultiplier: 4, messageFastTx: 1,
        localSystemData: { chassisIdSubtype: 'macAddress', chassisId: '00:11:22:33:55:04', systemName: 'acc-sw-04', systemDescription: 'Access 04 Mesh', systemCapabilitiesSupported: 'bridge', systemCapabilitiesEnabled: 'bridge' },
        ports: [
            { name: 'eth0', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-02', '10.0.0.3', '00:11:22:33:55:02', 'eth1')] },
            { name: 'eth1', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('edge-01', '10.0.1.1', '00:11:22:33:66:01', 'eth0')] },
            { name: 'eth2', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('core-sw-01', '10.0.0.1', '00:11:22:33:44:01', 'eth2')] },
        ],
    },
};

const EDGE_01: CncNode = {
    id: 6, hostName: 'edge-01', ipAddress: '10.0.1.1',
    interfaces: [
        { ifindex: 1, name: 'eth0', lastLinkUpdateId: 1, lastQdiscUpdateId: 1, type: IfType.ETHERNET, adminEnabled: true, operStatus: OperStatus.UP, hasPhysAddr: true, physAddress: macBytes('00:11:22:33:66:01'), mtu: 1500, numTxQueues: 4, numActiveTxQueues: 4, speed: 1000000000n }
    ],
    ptpAllData: {
        defaultDs: { numPorts: 1, priority1: 128, clockIdentity: '00:11:22:FF:FE:33:66:01', currentTime: { seconds: 1710005000n, nanoseconds: 78 }, instanceType: PtpInstanceType.PTP_INSTANCE_OC },
        currentDs: { stepsRemoved: 2, offsetFromTimeTransmitter: 88n, meanDelay: 1150n },
        parentDs: { parentClockIdentity: '00:11:22:FF:FE:33:55:04', parentPortNumber: 1, grandParentClockIdentity: '00:11:22:FF:FE:33:44:01' },
        performanceRecords15M: makePtpPerf15m(88),
        performanceRecords24H: makePtpPerf15m(82),
        ports: [],
    },
    lldpAllData: {
        messageTxInterval: 30, messageTxHoldMultiplier: 4, messageFastTx: 1,
        localSystemData: { chassisIdSubtype: 'macAddress', chassisId: '00:11:22:33:66:01', systemName: 'edge-01', systemDescription: 'Edge Node 01', systemCapabilitiesSupported: 'station', systemCapabilitiesEnabled: 'station' },
        ports: [
            { name: 'eth0', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txAndRx', neighbors: [makeLldpNeighbor('acc-sw-04', '10.0.0.5', '00:11:22:33:55:04', 'eth1')] },
        ],
    },
};

const EDGE_02: CncNode = {
    id: 7, hostName: 'edge-02', ipAddress: '10.0.1.2',
    interfaces: [
        { ifindex: 1, name: 'eth0', lastLinkUpdateId: 1, lastQdiscUpdateId: 1, type: IfType.ETHERNET, adminEnabled: true, operStatus: OperStatus.DOWN, hasPhysAddr: true, physAddress: macBytes('00:11:22:33:66:02'), mtu: 1500, numTxQueues: 4, numActiveTxQueues: 0, speed: 0n }
    ],
    ptpAllData: {
        defaultDs: { numPorts: 1, priority1: 128, clockIdentity: '00:11:22:FF:FE:33:66:02', currentTime: { seconds: 1710005000n, nanoseconds: 0 }, instanceType: PtpInstanceType.PTP_INSTANCE_OC },
        currentDs: { stepsRemoved: 0, offsetFromTimeTransmitter: 0n, meanDelay: 0n },
        parentDs: { parentClockIdentity: '', parentPortNumber: 0, grandParentClockIdentity: '' },
        performanceRecords15M: [],
        performanceRecords24H: [],
        ports: [],
    },
    lldpAllData: {
        messageTxInterval: 30, messageTxHoldMultiplier: 4, messageFastTx: 1,
        localSystemData: { chassisIdSubtype: 'macAddress', chassisId: '00:11:22:33:66:02', systemName: 'edge-02', systemDescription: 'Edge Node 02 (Faulty)', systemCapabilitiesSupported: 'station', systemCapabilitiesEnabled: 'station' },
        ports: [
            { name: 'eth0', destMacAddress: '01:80:C2:00:00:0E', adminStatus: 'txOnly', neighbors: [] },
        ],
    },
};

export const MOCK_TOPOLOGY: Topology = {
    nodes: [CORE_SW_01, ACC_SW_01, ACC_SW_02, ACC_SW_03, ACC_SW_04, EDGE_01, EDGE_02],
};