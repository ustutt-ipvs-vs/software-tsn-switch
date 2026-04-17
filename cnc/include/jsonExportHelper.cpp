struct CncEvent {
    Delay time;
    int queue;
    bool open;
    bool operator<(const CncEvent& o) const {
        return time < o.time;
    }
};

[[nodiscard]] auto TSNConfiguration::dump_to_cnc_json() const -> nlohmann::json {
    nlohmann::json root;
    root["nodes"] = nlohmann::json::array();

    // Group data by source/target devices and queues
    // Map: SourceID -> (TargetID -> List of PeriodicGates)
    std::map<DeviceId, std::map<DeviceId, std::map<int, PeriodicGate>>> link_configs;

    for (const auto& [port_key, gate] : gcl_config) {
        auto [link, pcp] = port_key;
        link_configs[link.source][link.target][pcp] = gate;
    }

    // Iterate over the grouped data to build JSON
    for (const auto& [source_id, targets_map] : link_configs) {
        nlohmann::json node_json;
        // Get name of the source device
        node_json["hostName"] = topology_->at(source_id).name;
        node_json["interfaces"] = nlohmann::json::array();

        // Iterate over all interfaces (targets) for this source
        for (const auto& [target_id, queues_map] : targets_map) {
            nlohmann::json iface_json;

            // return name of neighbor device (CNC uses this for LLDP lookup)
            iface_json["neighbor"] = topology_->at(target_id).name;

            // --- GCL CALCULATION ---

            std::vector<CncEvent> events;
            Delay max_cycle_time = 0;

            // Generate events for all queues
            for (const auto& [pcp, gate] : queues_map) {
                bool state = (gate.initial == OPEN);
                Delay current_time = gate.offset;

                // Calculate cycle time (max duration of all queues)
                Delay queue_end_time = gate.offset;
                for (auto d : gate.durations) queue_end_time += d;
                if (queue_end_time > max_cycle_time) max_cycle_time = queue_end_time;

                for (auto duration : gate.durations) {
                    if (duration > 0) {
                        current_time += duration;
                        state = !state;
                        events.push_back({current_time, pcp, state});
                    }
                }
            }
            std::sort(events.begin(), events.end());

            // Build mask entries
            nlohmann::json entries = nlohmann::json::array();

            // Construct initial mask
            uint8_t current_mask = 0;
            for (const auto& [pcp, gate] : queues_map) {
                if (gate.initial == OPEN) current_mask |= (1 << pcp);
            }

            Delay last_time = 0;
            uint32_t entry_index = 0;  // Index counter for YANG/Netconf list

            for (const auto& ev : events) {
                Delay duration = ev.time - last_time;
                if (duration > 0) {
                    entries.push_back({{"index", entry_index++},  // Add index
                                       {"timeIntervalValue", duration},
                                       {"gateStatesValue", current_mask},
                                       {"operationName", "sched:set-gate-states"}});
                }
                // Update mask for next interval
                if (ev.open)
                    current_mask |= (1 << ev.queue);
                else
                    current_mask &= ~(1 << ev.queue);

                last_time = ev.time;
            }

            // Last entry to complete the cycle
            if (last_time < max_cycle_time) {
                entries.push_back({{"index", entry_index++},  // Add index
                                   {"timeIntervalValue", max_cycle_time - last_time},
                                   {"gateStatesValue", current_mask},
                                   {"operationName", "sched:set-gate-states"}});
            }

            // Complete interface JSON with missing admin fields
            iface_json["gcl"] = {// Defaulting to 255 (all open) if GCL is inactive.
                                 {"adminGateStates", 255},

                                 {"cycleTime", max_cycle_time},

                                 // Setting a safe default of 500ns for now.
                                 {"cycleTimeExtension", 500},

                                 {"entries", entries}};

            node_json["interfaces"].push_back(iface_json);
        }
        root["nodes"].push_back(node_json);
    }

    return root;
}