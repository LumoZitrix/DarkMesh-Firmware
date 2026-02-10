#include "HunterModule.h"
#include "MeshService.h"
#include "configuration.h"
#include <Arduino.h>

/*
Generic Thread Module allows for the execution of custom code at a set interval.
*/
HunterModule *hunterModule;


HunterModule::HunterModule()
    : ProtobufModule("hunter", meshtastic_PortNum_TRACEROUTE_APP, &meshtastic_RouteDiscovery_msg), OSThread("Hunter")
{
    ourPortNum = meshtastic_PortNum_TRACEROUTE_APP;
    isPromiscuous = true; // We need to update the route even if it is not destined to us
}


int32_t HunterModule::runOnce()
{

    bool enabled = true;
    if (!enabled)
        return disable();

    if (firstTime) {
        // do something the first time we run
        firstTime = false;
        LOG_INFO("first time HunterThread running");
        return 10000; // parte dopo 10 secondi
    }

    LOG_INFO("hunter executing on node_index %d", node_id);
    // effettua il traceroute sul nodo node_id
    if (node_id<nodeDatabase.nodes.size()) {
        const auto &entry = nodeDatabase.nodes[node_id];
        LOG_INFO("hunter: %s: '%s'", entry.user.short_name, entry.user.long_name);
        HunterModule::launch(entry.num);
        node_id++;
    } else node_id=1;

    return (my_interval);
}

void HunterModule::launch(NodeNum node) {

    meshtastic_RouteDiscovery req = meshtastic_RouteDiscovery_init_zero;
    LOG_INFO("Creating RouteDiscovery protobuf...");

    meshtastic_MeshPacket *p = router->allocForSending();
    if (p) {
        p->to = node;
        p->decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        p->decoded.want_response = true;

        // Use reliable delivery for traceroute requests (which will be copied to traceroute responses by setReplyTo)
        p->want_ack = true;

        p->decoded.payload.size =
            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_RouteDiscovery_msg, &req);

        LOG_INFO("Packet allocated successfully: to=0x%08x, portnum=%d, want_response=%d, payload_size=%d", p->to,
                 p->decoded.portnum, p->decoded.want_response, p->decoded.payload.size);

        if (service) {
            service->sendToMesh(p, RX_SRC_USER);
            LOG_INFO("sendToMesh called successfully for trace route to node 0x%08x", node);
        } else {
            LOG_ERROR("MeshService is NULL!");
        }
    } else {
        LOG_ERROR("Failed to allocate TraceRoute packet from router");
    }

}

bool HunterModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RouteDiscovery *r)
{

    LOG_INFO("HunterModule handleReceivedProtobuf");
    // questo lo passiamo alla console, se attiva, cosi' lo trasmette come statistica
    return false; // let it be handled by RoutingModule
}
