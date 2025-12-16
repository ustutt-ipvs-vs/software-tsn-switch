#pragma once

#include <string>
#include "common/include/CncTypes.h"

class FipsImporter {
public:
    // Load GCL configuration from a FIPS file
    static ietfInterface_t importConfig(
        const std::string& jsonFilePath,
        const std::string& linkName,
        const std::string& targetIp,
        const std::string& targetInterface
    );
};