#include "config/Config.h"
#include "config/VehicleRules.h"
#include "common/Logger.h"

bool Config::validateCross(const BaseConfig& cfg) {
    const char* reason = vehicle_rules::validate(cfg);
    if (reason) {
        logger.log("[Config] Rejected: %s\n", reason);
        return false;
    }
    return true;
}
