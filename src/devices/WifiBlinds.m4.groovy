/**
include(`app_constants.m4')
include(`header.m4')
define(`__name__', __device_id__)dnl
 */

metadata {
    definition(
            // include(`definition.m4')
    ) {
        attribute "ip", "string"

        capability "Switch Level"
        capability "Actuator"
        capability "Switch"
        capability "Configuration"
        capability "Sensor"
        capability "Refresh"
    }

    simulator {}

    tiles(scale: 2) {
        multiAttributeTile(name:"switch", type: "lighting", width: 6, height: 4, canChangeIcon: true) {
            tileAttribute ("device.switch", key: "PRIMARY_CONTROL") {
                attributeState "on", label:'down', action:"switch.off", icon:"st.Weather.weather15", backgroundColor:"#555555", nextState:"turningOff"
                attributeState "off", label:'up', action:"switch.on", icon:"st.Weather.weather14", backgroundColor:"#3bbcff", nextState:"turningOn"
                attributeState "turningOn", label:'lowering', action:"switch.off", icon:"st.Weather.weather11", backgroundColor:"#777777", nextState: "turningOff"
                attributeState "turningOff", label:'raising', action:"switch.on", icon:"st.Weather.weather11", backgroundColor:"#2287cd", nextState: "turningOn"
            }
            tileAttribute ("device.level", key: "SLIDER_CONTROL") {
                attributeState "level", action:"switch level.setLevel"
            }
        }
        standardTile("refresh", "device.switch", inactiveLabel: false, decoration: "flat", width: 2, height: 2) {
            state "default", label:"", action:"refresh.refresh", icon:"st.secondary.refresh"
        }

        valueTile("level", "device.level", inactiveLabel: false, decoration: "flat", width: 2, height: 2) {
            state "level", label:'${currentValue} %', unit:"%", backgroundColor:"#ffffff"
        }

        valueTile("ip", "device.hubMac", decoration: "flat", width: 2, height: 1) {
            state "default", label:'IP: ${currentValue}', width: 2, height: 1
        }

        main "switch"
        details(["switch", "refresh", "level", "ip", "levelSliderControl"])
    }
}

// Parse incoming device messages to generate events
def parse(String description) {
    log.debug "Parsing '${description}'"

    def keyVal = description.split(":", 2)
    if (keyVal[0] in ["level", "switch"]) {
        return createEvent(name: keyVal[0], value: keyVal[1])
    } else if (keyVal[0] == "updated") {
        log.trace "__human_name__ was updated"
        return null
    } else {
        log.warn "Unknown event in __human_name__ parse(): ${description}"
        return null
    }
}

def on() {
    return setLevel(100)
}

def off() {
    return setLevel(0)
}

def setLevel(level) {
    def oldLevel = device.currentValue("level") ?: 0

    log.info("__human_name__ ${device.deviceNetworkId} level set to: ${level} from ${oldLevel}")
    if (level == oldLevel || level > 100 || level < 0) return null

    if (oldLevel != 0 && level == 0) {
        sendEvent(name: "switch", value: "turningOff")
    } else if (oldLevel == 0 && level != 0) {
        sendEvent(name: "switch", value: "turningOn")
    }

    def netAddr = device.currentValue("ip")
    def dni = device.deviceNetworkId

    log.info("__human_name__ posting to ${netAddr} for ${device.deviceNetworkId}")
    return new physicalgraph.device.HubAction([
            method: "POST",
            path: "/set",
            headers: [
                    HOST: "${netAddr}:80",
                    "Content-Length": 0
            ],
            query: [device: dni, level: "${level}"]
    ], "${dni}")
}

def refresh() {
    log.info("__human_name__ ${device.deviceNetworkId} refreshing at ${device.currentValue("ip")}")
    return new physicalgraph.device.HubAction([
            method: "POST",
            path: "/subscribe",
            headers: [
                    HOST: "${netAddr}:80",
                    "Content-Length": 0
            ],
            query: [device: dni]
    ], "${dni}")
}
