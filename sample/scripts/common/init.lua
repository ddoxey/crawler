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
