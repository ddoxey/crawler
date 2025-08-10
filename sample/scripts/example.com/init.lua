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
    if title then  
      print("[init.lua] common.parse_title → “" .. title .. "”")
    else
      print("[init.lua] common.parse_title → none")
    end
  end

  local tns = common.parse_tns(content)
  if DEBUG then
    if tns then
      print("[init.lua] common.parse_tns → “" .. table.concat(tns, ";") .. "”")
    else
      print("[init.lua] common.parse_tns → none")
    end
  end

  local urls = common.parse_urls(content)
  if DEBUG then
    if urls then  
      print("[init.lua] common.parse_urls → “" .. table.concat(urls, ";") .. "”")
    else
      print("[init.lua] common.parse_urls → none")
    end
  end

  local client_redirect = common.build_client_redirect_result(content)
  if DEBUG then
    if client_redirect then
      print(("[init.lua] client_redirect → type=%s delay=%d url=%s base=%s")
            :format(client_redirect.type or "?",
                    client_redirect.delay or 0,
                    client_redirect.url or "(nil)",
                    client_redirect.base or "(nil)"))
    else
      print("[init.lua] client_redirect → none")
    end
  end

  -- If you had other steps, you could chain them:
  --   local data = parse(content, url)
  --   local enriched = other_step(data)
  local result = {
    client_redirect = client_redirect,
    title = title,
    tns = tns,
    urls = urls,
    url = url,
  }

  -- return a table back to C++
  return result
end
