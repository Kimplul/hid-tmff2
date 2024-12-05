-- Create a new dissector
tmff = Proto("tmff", "TM Force Feedback Protocol")

-- Define fields for the protocol
local f = tmff.fields
f.action = ProtoField.string("tmff.action", "Action")
f.value_1 = ProtoField.uint32("tmff.value_1", "Value 1", base.DEC)
f.value_2 = ProtoField.uint32("tmff.value_2", "Value 2", base.DEC)
f.value_full = ProtoField.uint32("tmff.value_full", "Value full", base.DEC)
f.decoded = ProtoField.bool("tmff.decoded", "decoded")
local hid_data = Field.new("usbhid.data")

local id = {}
local action = {}
local decoded = false

-- Packettype [0]
local ConfigPacket = 0x0a
local FFBPacket = 0x60

-- Configparam 0a + [3]
local Dampers = 0x17
local Linearity = 0x2f
local Mode = 0x2a -- Comfort, Sport, Performance, Extreme
local Color = 0x24

-- FFB Effect 60 + [3]
local PlayStop = 89

-- FFB effect 60 + 
local Open = 0x0105
local Close = 0x0104
local DaFrInSp = 64

local function decodeConfigPacket(data)
end

local function decodeFfbPacket(data)
    firstbyte = data(0, 1):uint()
    
    id = data(2, 1):uint()
    secondbyte = data(1, 1):uint()
    if (secondbyte == 0x01) then
        if (id == 0x04) then
            action = "Open (04)"
            decoded = true
            return
        elseif (id == 0x05) then
            action = "Open (05)"
            decoded = true
            print(action)
            return
        else
            action = "Unknown Open/Close"
            return
        end
    elseif (secondbyte == 0x00) then
        fourthbyte = data(3, 1):uint()
        if (fourthbyte == 0x89) then
            fifthbyte = data(4, 1):uint()
            if (fifthbyte == 01) then
                action = "Play"
                decoded = true
                return
            elseif (fifthbyte == 41) then
                action = "Play with count"
                decoded = true
                return
            elseif (fifthbyte == 00) then
                action = "Stop"
                decoded = true
                return
            else
                action = "Unknown playback"
                decoded = true
                return
            end
        end
    end
end

-- Create a table to map packet types to decoding functions
local packetTypeHandlers = {
    [ConfigPacket] = decodeConfigPacket,
    [FFBPacket] = decodeFfbPacket
    -- Add more mappings as needed
}

local function decodePacket(packet)
    -- Extract packet type and action
    local packetType = packet(0, 1):uint() 
    local handler = packetTypeHandlers[packetType]

    if handler then
        -- Call the handler function with the action and data
        handler(packet)
    else
        print("Unknown packet type:", packetType)
    end
end

function checkAction(buf)
    local action = buf(0, 1):uint(base.HEX)
    local act_mod = buf(1, 1):uint(base.HEX)
    local te = 0x0a
    if action == te then
        act_type = buf(3, 1):uint(base.HEX)
        if act_type == 0x17 then
            return "Dampers"
        elseif act_type == 0x2f then
            return "Linearity"
        elseif act_type == 0x2a then
            return "Mode"
        elseif act_type == 0x24 then
            -- 68 fe = custom color / 68 fb = color cycle
            return "Color"
        else
            return "Unknown setup"
        end
    elseif action == 0x60 then
        if act_mod == 0x02 then
            return "Gain"
        elseif act_mod == 0x01 then
            if buf(2, 1):uint(base.HEX) == 0x05 then
                return "Open"
            elseif buf(2, 1):uint(base.HEX) == 0x04 then
                return "Close"
            elseif buf(3, 1):uint(base.HEX) == 0x64 then
                return "Damper+Friction+Inertia+Spring"
            end
        else
            return "FFB Setup?"
        end
    else
        return "Wa!"
    end
end

function ModeName(buf)
    if buf(5, 2):uint(base.HEX) == 0x0001 then
        return "Comfort"
    elseif buf(5, 2):uint(base.HEX) == 0x0101 then
        return "Sport"
    elseif buf(5, 2):uint(base.HEX) == 0x0201 then
        return "Performance"
    elseif buf(5, 2):uint(base.HEX) == 0x0301 then
        return "Extreme"
    end
end

function preFilter(buf)
    if buf(0, 1):uint(base.HEX) == 0x0a or buf(0, 1):uint(base.HEX) == 0x60 then
        return true
    else
        return false
    end
end

count = 0
function tmff.dissector(buffer, pinfo, tree)
    local tete = hid_data()
    if not tete then
        return
    end
    local length = tete.range:len()
    if length <= 63 or not preFilter(tete.range) then
        return
    end
    pinfo.cols.protocol = tmff.name
    action = "hu?"
    decoded = false
    decodePacket(tete.range)

    -- Create a subtree for the protocol
    local subtree = tree:add(tmff, buffer(), "TMFF")
    local setValue = tete.range(5, 4):le_uint()
    -- Parse fields
    -- local action = checkAction(tete.range)
    subtree:add(f.action, action)
    subtree:add(f.value_1, tete.range(5, 2):le_uint())
    subtree:add(f.value_2, tete.range(7, 2):le_uint())
    subtree:add(f.value_full, tete.range(5, 4):le_int())
    subtree:add(f.decoded, decoded)

    if action == "Mode" then
        pinfo.cols.info = action .. ": " .. ModeName(tete.range)
    elseif action == "Gain" then
        pinfo.cols.info = action .. ": " .. math.floor(tete.range(2, 1):uint() / 2.53)
    elseif action == "Open" then
        pinfo.cols.info = action
    else
        pinfo.cols.info = action .. ": " .. tete.range(5, 4):le_uint() .. " " .. tete.range(7, 2):le_uint() .. " " ..
                              tete.range(5, 2):le_uint()
    end
end

register_postdissector(tmff)
