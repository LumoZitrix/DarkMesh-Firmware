#include "HunterModule.h"
#include "MeshService.h"
#include "configuration.h"
#include <Arduino.h>

#include "ConsoleModule.h"
#include "Default.h"
#include "TraceRouteModule.h"
#include "RTC.h"

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

    if (!enabled) return 2000;

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

    return (trace_interval);
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
            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload), &meshtastic_RouteDiscovery_msg, &req);

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

bool HunterModule::handleReceivedProtobuf(const meshtastic_MeshPacket &p, meshtastic_RouteDiscovery *r)
{

    LOG_INFO("HunterModule handleReceivedProtobuf");
    const meshtastic_Data &incoming = p.decoded;

    LOG_INFO("xxx req %d", incoming.request_id);
    if (!incoming.request_id) {
        printRoute(r, p.from, p.to, true);
        return false;
    } else {
        printRoute(r, p.to, p.from, false);
    }
    return false;

    // deviamo il pacchetto verso il nodo che controlla l'hunter ( si trova in ConsoleModule)
    NodeNum controlling_node = consoleModule->controlling_node;
    LOG_INFO("xxx controlling node 0x%08x", controlling_node);
    if (controlling_node==0) return false;

    meshtastic_MeshPacket *np = router->allocForSending();
    if (np==nullptr){return  false;}
    // memcpy(&np, &p, sizeof(meshtastic_MeshPacket));

    np->id = p.id;
    np->from = p.from;

    np->to = 3663388348; //controlling_node;
    np->decoded.request_id = p.decoded.request_id;

    np->decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
    meshtastic_RouteDiscovery nr = *r;

    // andrebbe aggiunta la destinazione intermedia, cioè questo nodo, alla lista
    if (nr.route_back_count< ROUTE_SIZE) {
        nr.route_back[nr.route_back_count] = myNodeInfo.my_node_num;
        nr.route_back_count++;
    }
    if (nr.snr_back_count< ROUTE_SIZE) {
        nr.snr_back[nr.snr_back_count] = p.rx_snr;
        nr.snr_back_count++;
    }
    printRoute(&nr, p.to, p.from, false);

    // ri-codifico il pacchetto da trasmettere

    np->decoded.payload.size =
        pb_encode_to_bytes(np->decoded.payload.bytes, sizeof(np->decoded.payload), &meshtastic_RouteDiscovery_msg, &nr);


    LOG_INFO("Steering: to=0x%08x, portnum=%d, want_response=%d, payload_size=%d", np->to,
             np->decoded.portnum, np->decoded.want_response, np->decoded.payload.size);

    if (service) {
        service->sendToMesh(np, RX_SRC_LOCAL);
        LOG_INFO("sendToMesh called successfully for trace route response to node 0x%08x", controlling_node);
    } else {
        LOG_ERROR("MeshService is NULL!");
    }

    return false; // let it be handled by RoutingModule
}

void HunterModule::printRoute(meshtastic_RouteDiscovery *r, uint32_t origin, uint32_t dest, bool isTowardsDestination)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    std::string route = "Hunter: Route traced:\n";
    route += vformat("0x%x --> ", origin);
    for (uint8_t i = 0; i < r->route_count; i++) {
        if (i < r->snr_towards_count && r->snr_towards[i] != INT8_MIN)
            route += vformat("0x%x (%.2fdB) --> ", r->route[i], (float)r->snr_towards[i] / 4);
        else
            route += vformat("0x%x (?dB) --> ", r->route[i]);
    }
    // If we are the destination, or it has already reached the destination, print it
    if (dest == nodeDB->getNodeNum() || !isTowardsDestination) {
        if (r->snr_towards_count > 0 && r->snr_towards[r->snr_towards_count - 1] != INT8_MIN)
            route += vformat("0x%x (%.2fdB)", dest, (float)r->snr_towards[r->snr_towards_count - 1] / 4);

        else
            route += vformat("0x%x (?dB)", dest);
    } else
        route += "...";

    // If there's a route back (or we are the destination as then the route is complete), print it
    if (r->route_back_count > 0 || origin == nodeDB->getNodeNum()) {
        route += "\n";
        if (r->snr_towards_count > 0 && origin == nodeDB->getNodeNum())
            route += vformat("(%.2fdB) 0x%x <-- ", (float)r->snr_back[r->snr_back_count - 1] / 4, origin);
        else
            route += "...";

        for (int8_t i = r->route_back_count - 1; i >= 0; i--) {
            if (i < r->snr_back_count && r->snr_back[i] != INT8_MIN)
                route += vformat("(%.2fdB) 0x%x <-- ", (float)r->snr_back[i] / 4, r->route_back[i]);
            else
                route += vformat("(?dB) 0x%x <-- ", r->route_back[i]);
        }
        route += vformat("0x%x", dest);
    }
    LOG_INFO(route.c_str());
#endif
}
