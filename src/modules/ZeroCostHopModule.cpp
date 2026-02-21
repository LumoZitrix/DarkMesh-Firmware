
#include "ZeroCostHopModule.h"

#include "meshUtils.h"
#include "NodeDB.h"

ZeroCostHopModule *zeroCostHopModule;

const char* tag = "Zero Cost Hop -";

ProcessMessage ZeroCostHopModule::handleReceived(const meshtastic_MeshPacket &mp) {

    if (!isInfrastructureRole(config.device.role)) {
        return ProcessMessage::CONTINUE;
    }

    const uint8_t hopsUsed = mp.hop_start - mp.hop_limit;

    if (hopsUsed != 0) {
        LOG_INFO("%s node id %lu hops used: %u not suitable for this module, skipping",tag, mp.from, hopsUsed);
        return ProcessMessage::CONTINUE;
    }

    LOG_INFO("%s node id %lu hops used: %u , proceeding..",tag, mp.from, hopsUsed);

    meshtastic_User scratch = {};
    if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_User_msg, &scratch)) {

        if (!isInfrastructureRole(scratch.role)) {
            LOG_INFO("%s decoded node %s it's not an infrastructure role, skipping",tag, scratch.id);
            return ProcessMessage::CONTINUE;
        }

        LOG_INFO("%s decode success for node id %s",tag, scratch.id);

        for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
            const auto &entry = nodeDatabase.nodes[i];

            if (entry.num == mp.from && !entry.is_favorite) {
                LOG_INFO("%s node match found for num %s setting favourite!",tag, scratch.id);
                nodeDB->set_favorite(true, entry.num);
                break;
            }
        }

    } else  {
        LOG_ERROR("%s could not decode NodeInfo!", tag);
    }

    return ProcessMessage::CONTINUE;
}

bool ZeroCostHopModule::isInfrastructureRole(const meshtastic_Config_DeviceConfig_Role role) {
    return role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
           role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE ||
           role == meshtastic_Config_DeviceConfig_Role_CLIENT_BASE;
}