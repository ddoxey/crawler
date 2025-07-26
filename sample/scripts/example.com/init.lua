local src = debug.getinfo(1, "S").source:sub(2)
local this_dir = src:match("(.*/)")
package.path = this_dir .. "../common/init.lua;" .. package.path
local common = require("common")  -- loads common/init.lua once

-- 4) (Optionally) load any other per-domain helpers
-- local fetch = require("fetch")
-- local parse = require("parse")

-- Define the single entry-point the C++ side will call
-- proc = process(html_content, url_string)
function process(content, url)
  if DEBUG then
    print(string.format("[init.lua] this function’s reference id = %s", tostring(process)))
    print("[init.lua] process() called, content length=" .. #content)  
  end

  -- call our common helper
  local title = common.parse_title(content)
  if DEBUG then
    print("[init.lua] common.parse_title → “" .. title .. "”")
  end

  -- If you had other steps, you could chain them:
  --   local data = parse(content, url)
  --   local enriched = other_step(data)
  local result = { title = title, url = url }

  -- return a table back to C++
  return result
end
