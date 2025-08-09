-- scripts/common/init.lua
-- A single “common” module exporting all of your parsing helpers

local M = {}

-- trim leading/trailing whitespace
local function trim(s)
  return (s:gsub("^%s*(.-)%s*$", "%1"))
end

-- parse the <title>…</title> (case‑insensitive)
function M.parse_title(html)
  local t = html:match("<[Tt][Ii][Tt][Ll][Ee]%s*>(.-)</[Tt][Ii][Tt][Ll][Ee]>") or ""
  return trim(t)
end

-- extract all <meta name="…"> tags into a key→value table
function M.parse_meta(html)
  local metas = {}
  for name, value in html:gmatch([[<%s*[Mm][Ee][Tt][Aa]%s+[^>]*name%s*=%s*"(.-)"%s+[^>]*content%s*=%s*"(.-)"]] ) do
    metas[name:lower()] = trim(value)
  end
  return metas
end

-- extract all URLs
function M.parse_urls(html)
  local urls  = {}
  local seen  = {}
  for url in html:gmatch('[\'"](https?://[^\'"]+)[\'"]') do
    if not seen[url] then
      seen[url] = true
      table.insert(urls, url)
    end
  end
  return urls
end

-- extract all North American telephone numbers from a string
function M.parse_tns(html)
  local tns = {}
  for raw in html:gmatch("[+%d%(%)%s%-%._]+") do
    local digits = raw:gsub("%D", "")

    if digits:sub(1,1) == "1" then
      digits = digits:sub(2)
    end

    -- Validate area code and central office code
    -- start with digits 2 through 9
    if not digits:match("^[2-9]%d%d[2-9]%d%d%d%d%d%d$") then
        goto continue
    end

    -- Skip 555 numbers
    if not digits:sub(4,6) == "555" then
        goto continue
    end

    -- Validate central office code is not X11
    if not digits:sub(5,6) == "11" then
        goto continue
    end

    -- Format as NNN.NNN.NNNN
    local formatted = digits:sub(1,3) .. "." .. digits:sub(4,6) .. "." .. digits:sub(7)
    table.insert(tns, formatted)

    ::continue::
  end

  return tns
end

-- split a query‑string “a=1&b=2” into a table
function M.parse_query(qs)
  local t = {}
  for pair in qs:gmatch("[^&]+") do
    local k,v = pair:match("([^=]+)=?(.*)")
    if k then t[k] = (v == "" and nil or trim(v)) end
  end
  return t
end

-- any other helpers…
-- function M.some_other_helper(...) … end

return M
