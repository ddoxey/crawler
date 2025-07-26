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

-- extract all North American telephone numbers from a string
function M.parse_tns(html)
  local tns = {}
  for raw in html:gmatch("[+%d%(%)%s%-%._]+") do
    local digits = raw:gsub("%D", "")

    -- Step 1: Normalize to 10 digits
    if #digits == 11 and digits:sub(1,1) == "1" then
      digits = digits:sub(2)
    elseif #digits ~= 10 then
      goto continue  -- reject
    end

    -- Step 2: Validate area code (first digit not 0 or 1)
    if digits:sub(1,1) == "0" or digits:sub(1,1) == "1" then
      goto continue
    end

    -- Step 3: Format as NNN.NNN.NNNN
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
