#include "ConsoleModule.h"
#include "MeshService.h"
#include "MeshModule.h"
#include "Telemetry/EnvironmentTelemetry.h"
#include "Telemetry/PowerTelemetry.h"
#include "NodeDB.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "Power.h"

ConsoleModule *consoleModule;
extern Power *power;

void ConsoleModule::sendText(NodeNum dest, ChannelIndex channel, const char *message, bool wantReplies)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    p->to = dest;
    p->channel = channel;
    p->want_ack = true;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    service->sendToMesh(
        p, RX_SRC_LOCAL,
        true); // send to mesh, cc to phone. Even if there's no phone connected, this stores the message to match ACKs
}


ProcessMessage ConsoleModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Console module: text msg from=0x%0x, id=0x%x, channel=%d,  msg=%.*s", mp.from, mp.id, mp.channel, p.payload.size, p.payload.bytes);
#endif
    // se il messaggio proviene da una delle chiavi autorizzate,
    // si prosegue

    const bool authorized =
        (config.security.admin_key[0].size == 32 &&
         memcmp(mp.public_key.bytes, config.security.admin_key[0].bytes, 32) == 0) ||
        (config.security.admin_key[1].size == 32 &&
         memcmp(mp.public_key.bytes, config.security.admin_key[1].bytes, 32) == 0) ||
        (config.security.admin_key[2].size == 32 &&
         memcmp(mp.public_key.bytes, config.security.admin_key[2].bytes, 32) == 0);

    if (!authorized) {
        LOG_INFO("message from no-authorized keys");
        return ProcessMessage::CONTINUE;
    }

    if (strncmp(reinterpret_cast<const char *>(p.payload.bytes),"+++",3)==0) {
        command_state = !command_state;
    }
    if (!command_state) return ProcessMessage::CONTINUE;

    if (p.payload.size>0 and p.payload.bytes[0]=='H') {
        sendText(mp.from,0, "Hello from firmware!", false);
    } else if (p.payload.size>0 and p.payload.bytes[0]=='N') {
        LOG_INFO("Nodes");
        // 0 = ourself
        uint16_t max_nodes=4;
        std::string route = "Nodes:\n";
        for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
            const auto &entry = nodeDatabase.nodes[i];
            if (entry.hops_away == 0) {
                route += vformat("%s: '%s' snr: %f\n", entry.user.short_name, entry.user.long_name, entry.snr);
                if (! --max_nodes) break;
            }
        }
        sendText(mp.from,0, route.c_str(), false);
    } else if (p.payload.size>0 and p.payload.bytes[0]=='T') {
        // telemetry
        meshtastic_Telemetry m = meshtastic_Telemetry_init_zero;
        m.which_variant = meshtastic_Telemetry_environment_metrics_tag;
        // check if module is loaded
        auto* environment_telemetry_module = (EnvironmentTelemetryModule*)MeshModule::getModule("EnvironmentTelemetry");
        if (!environment_telemetry_module) {
            sendText(mp.from, 0, "environment telemetry not enabled!", false);
            return ProcessMessage::CONTINUE;
        }
        if (environment_telemetry_module->extGetEnvironmentTelemetry(&m)) {
            std::string msg = "Environment:\n";
            if (m.variant.environment_metrics.has_temperature) {
                msg+=vformat(" T : %f\n", m.variant.environment_metrics.temperature);
            }
            if (m.variant.environment_metrics.has_relative_humidity) {
                msg+=vformat("RH: %f\n", m.variant.environment_metrics.relative_humidity);
            }
            if (m.variant.environment_metrics.has_barometric_pressure) {
                msg+=vformat(" P: %f\n", m.variant.environment_metrics.barometric_pressure);
            }
            sendText(mp.from,0, msg.c_str(), false);
        }
        auto* power_telemetry_module = (PowerTelemetryModule *) MeshModule::getModule("PowerTelemetry");
        if (!power_telemetry_module) {
            sendText(mp.from, 0, "power telemetry not enabled!", false);
            return ProcessMessage::CONTINUE;
        }
        m = meshtastic_Telemetry_init_zero;
        m.which_variant = meshtastic_Telemetry_power_metrics_tag;
        if (power_telemetry_module->extGetPowerTelemetry(&m)) {
            std::string msg = "Power:\n";
            if (m.variant.power_metrics.has_ch3_voltage) {
                msg+=vformat("V3 : %f\n", m.variant.power_metrics.ch3_voltage);
            }
            if (m.variant.power_metrics.has_ch3_current) {
                msg+=vformat("I3: %f\n", m.variant.power_metrics.ch3_current);
            }
            sendText(mp.from,0, msg.c_str(), false);
        }
    } else if (p.payload.size>0 and p.payload.bytes[0]=='C') {
        // nodi preferiti C? = lista, C+<id>, aggiunge, C-<id> toglie
        LOG_INFO("Favorites");
        if (p.payload.size==1) {
            // only C = list
            std::string msg = "Favorites:\n";
            for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
                const auto &entry = nodeDatabase.nodes[i];
                if (entry.is_favorite) {
                    msg += vformat("!%08x: '%s'\n", entry.num, entry.user.short_name);
                }
            }
            sendText(mp.from,0, msg.c_str(), false);
        } else if (p.payload.size==10) {
            // C+<hex id>
            // parse hex id
            auto new_favorite_id =  static_cast<uint32_t>(strtoul(reinterpret_cast<const char *>(p.payload.bytes+2), nullptr, 16));
            bool set = (p.payload.bytes[1] == '+');
            if (set || p.payload.bytes[1] == '-') {
                nodeDB->set_favorite(set, new_favorite_id);
                LOG_INFO("%s %d as favorite", set ? "Setting" : "Unsetting", new_favorite_id);
                auto msg = vformat("%d %s as favorite",new_favorite_id, set ? "set" : "unset" );
                sendText(mp.from, 0, msg.c_str(), false);
            }
        }
    } else if (p.payload.size>0 and p.payload.bytes[0]=='B') {
        // blacklist
        if (p.payload.size==1) {
            // list
            std::string msg = "Blacklist:\n";
            for (size_t i = 1; i < nodeDatabase.nodes.size(); i++) {
                const auto &entry = nodeDatabase.nodes[i];
                if (entry.is_ignored) {
                    msg += vformat("!%08x: '%s'\n", entry.num, entry.user.short_name);
                }
            }
            sendText(mp.from,0, msg.c_str(), false);
        } else if (p.payload.size==10) {
            auto new_bl_id =  static_cast<uint32_t>(strtoul(reinterpret_cast<const char *>(p.payload.bytes+2), nullptr, 16));
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(new_bl_id);
            if (node==nullptr) {
                sendText(mp.from, 0, "node not found in db", false);
                return ProcessMessage::CONTINUE;
            }
            bool set = (p.payload.bytes[1] == '+');
            if (set || p.payload.bytes[1] == '-') {
                node->is_ignored = set;
                nodeDB->saveToDisk(SEGMENT_NODEDATABASE);
                // salviamo il nodedb
                auto msg = vformat("%u %sblacklisted",new_bl_id, set ? "" : "not " );
                sendText(mp.from, 0, msg.c_str(), false);
            }
        }

    } else if (p.payload.size>0 and p.payload.bytes[0]=='V') {
        //voltage and battery stats
        if (!power) {
            sendText(mp.from, 0, "Power manager not ready", false);
            return ProcessMessage::CONTINUE;
        }

        uint16_t voltage = power->getLastVoltageRead();
        uint8_t battPercent = power->getLastBattPercentRead();

        //FIXME these params seem to be not evaluated correctly
        //eg. when the device is charging, it does not show as charging

        //bool charging = power->isBatteryCharging();
        //bool usbPowered = power->isUsbPowered();
        //bool batteryConnected = power->isBatteryConnect();

        std::string msg = "Battery:\n";
        msg += vformat("VLT: %u mV\n", voltage);
        msg += vformat("PRC: %u%%\n", battPercent);

        //msg += vformat("CHG: %s\n", charging ? "Y" : "N");
        //msg += vformat("USB PWR: %s\n", usbPowered ? "Y" : "N");
        //msg += vformat("BATT CONN: %s\n", batteryConnected ? "Y" : "N");

        sendText(mp.from, 0, msg.c_str(), false);
    }

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool ConsoleModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
