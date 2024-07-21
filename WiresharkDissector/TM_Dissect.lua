-- Create a new dissector
tmff = Proto("tmff", "TM Force Feedback Protocol")

-- Define fields for the protocol
local f = tmff.fields
f.action = ProtoField.string("tmff.action", "Action")
f.value_1 = ProtoField.uint32("tmff.value_1", "Value 1", base.DEC)
f.value_2 = ProtoField.uint32("tmff.value_2", "Value 2", base.DEC)
f.value_full = ProtoField.uint32("tmff.value_full", "Value full", base.DEC)

-- f.type = ProtoField.uint8("tmff.type", "Type", base.HEX)
-- f.value = ProtoField.uint32("tmff.value", "Value", base.HEX)

function checkAction(buf)
    local action = buf(64, 1):uint(base.HEX)
    local act_mod = buf(65, 1):uint(base.HEX)
    local te = 0x0a
    if action == te then
        act_type = buf(67, 1):uint(base.HEX)
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
            if buf(66,1):uint(base.HEX) == 0x05 then
                return "Open"
            end
        else
            return "FFB Setup?"
        end
    else
        return "Wa!"
    end
end

function ModeName(buf)
    if buf(69, 2):uint(base.HEX)
    == 0x0001 then
        return "Comfort"
    elseif buf(69, 2):uint(base.HEX)
    == 0x0101 then
        return "Sport"
    elseif buf(69, 2):uint(base.HEX)
    == 0x0201 then
        return "Performance"
    elseif buf(69, 2):uint(base.HEX)
    == 0x0301 then
        return "Extreme"
    end
end

function preFilter(buf)
    if buf(64, 1):uint(base.HEX)
    == 0x0a
    or buf(64, 1):uint(base.HEX)
    == 0x60 then
        return true
    else
        return false
    end
end

count = 0
function tmff.dissector(buffer, pinfo, tree)
    length = buffer:len()
    if length == 0 or not preFilter(buffer) then return end
    pinfo.cols.protocol = tmff.name

    -- Create a subtree for the protocol
    local subtree = tree:add(tmff, buffer(), "TMFF")

    local setValue = buffer(69, 4):le_uint()


    -- Parse fields
    local action = checkAction(buffer)
    subtree:add(f.action, action)
    subtree:add(f.value_1, buffer(69, 2):le_uint())
    subtree:add(f.value_2, buffer(71, 2):le_uint())
    subtree:add(f.value_full, buffer(69, 4):le_int())
    -- subtree:add(f.type, buffer(2, 1))
    -- subtree:add(f.value, buffer(3))

    -- Update protocol column in Wireshark
    -- 1065353216 = 100%
    -- 1008981770 = 1%
    -- 1056964608 = 50%
    -- 8388608 20-10
    -- 5033165 30-20
    -- 3355443 40-30
    -- 3355443 50-40
    -- 1677722 60-50
    -- 1677721 70-60
    -- 1677722 80-70
    -- 1677721 90-80
    -- 1677722 100-90
    -- pinfo.cols.info = "Value: " .. buffer(72, 1):uint() .. " " .. buffer(71, 1):uint().. " " .. buffer(70, 1):uint().. " " .. buffer(69, 1):uint()
    if action == "Mode" then
        pinfo.cols.info = action .. ": " .. ModeName(buffer)
    elseif action == "Gain" then
        pinfo.cols.info = action .. ": " .. math.floor(buffer(66, 1):uint() / 2.53) 
    elseif action == "Open" then
        pinfo.cols.info = action
    else
        pinfo.cols.info = action .. ": " .. buffer(69, 4):le_int()  .. " " .. buffer(71, 2):le_int() - 15394 .. " " .. buffer(69, 2):le_int()
    end
    -- pinfo.cols."HID Data" = "Ha"
    -- if (count == 0) then
    --     print(pinfo.cols)
    --     count = count + 1
    -- end
end

register_postdissector(tmff)
