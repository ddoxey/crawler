-- scripts/common/init.lua
-- A single “common” module exporting all of your parsing helpers

local M = {}

-- trim leading/trailing whitespace
local function trim(s)
  return (s:gsub("^%s*(.-)%s*$", "%1"))
end

local function unescape_html(s)
  s = s:gsub("&amp;", "&"):gsub("&quot;", '"'):gsub("&#39;", "'")
  s = s:gsub("&lt;", "<"):gsub("&gt;", ">")
  return s
end

-- Strip JS block and line comments, preserving quotes and template literals.
-- Keeps line breaks for // comments to avoid breaking line-sensitive logic.
local function strip_js_comments(src)
  local out = {}
  local i, n = 1, #src
  local state = "code"  -- code | squote | dquote | template

  while i <= n do
    local c  = src:sub(i, i)
    local c2 = (i < n) and src:sub(i+1, i+1) or ""

    if state == "code" then
      if c == "'" then
        state = "squote"; out[#out+1] = c; i = i + 1
      elseif c == '"' then
        state = "dquote"; out[#out+1] = c; i = i + 1
      elseif c == "`" then
        state = "template"; out[#out+1] = c; i = i + 1
      elseif c == "/" and c2 == "/" then
        -- line comment: skip to newline, keep one newline
        i = i + 2
        while i <= n and src:sub(i,i) ~= "\n" do i = i + 1 end
        out[#out+1] = "\n"
        if i <= n then i = i + 1 end
      elseif c == "/" and c2 == "*" then
        -- block comment: skip until */
        i = i + 2
        while i <= n-1 and not (src:sub(i,i) == "*" and src:sub(i+1,i+1) == "/") do
          i = i + 1
        end
        i = (i <= n-1) and (i + 2) or (n + 1)
        out[#out+1] = " "  -- keep spacing
      else
        out[#out+1] = c; i = i + 1
      end

    elseif state == "squote" then
      if c == "\\" and i < n then
        out[#out+1] = src:sub(i, i+1); i = i + 2
      elseif c == "'" then
        state = "code"; out[#out+1] = c; i = i + 1
      else
        out[#out+1] = c; i = i + 1
      end

    elseif state == "dquote" then
      if c == "\\" and i < n then
        out[#out+1] = src:sub(i, i+1); i = i + 2
      elseif c == '"' then
        state = "code"; out[#out+1] = c; i = i + 1
      else
        out[#out+1] = c; i = i + 1
      end

    elseif state == "template" then
      if c == "\\" and i < n then
        out[#out+1] = src:sub(i, i+1); i = i + 2
      elseif c == "`" then
        state = "code"; out[#out+1] = c; i = i + 1
      else
        out[#out+1] = c; i = i + 1
      end
    end
  end

  return table.concat(out)
end

-- Parse attributes of a single tag into a table (lowercased keys)
local function parse_attrs(tag)
  local attrs = {}

  -- quoted: key='...'  or key="..."
  for k, q, v in tag:gmatch("([%w:-]+)%s*=%s*(['\"])(.-)%2") do
    attrs[k:lower()] = v
  end
  -- unquoted: key=value (value ends at whitespace, '>', or '/')
  for k, v in tag:gmatch("([%w:-]+)%s*=%s*([^%s'\">/]+)") do
    local lk = k:lower()
    if attrs[lk] == nil then attrs[lk] = v end
  end
  return attrs
end

-- Return { delay = <int>, url = "<string>" } or nil
local function parse_meta_refresh(meta_tag)
  local attrs = parse_attrs(meta_tag)
  local hev   = attrs["http-equiv"]
  if not (hev and hev:lower() == "refresh") then
    return nil
  end

  local content = attrs["content"]
  if not content then return nil end
  content = trim(content)

  -- delay is optional; default 0
  local delay = tonumber(content:match("^%s*(%d+)")) or 0

  -- url=… can be single-quoted, double-quoted, or unquoted
  local url =
         content:match("[Uu][Rr][Ll]%s*=%s*'(.-)'")
      or content:match('[Uu][Rr][Ll]%s*=%s*"(.-)"')
      or content:match("[Uu][Rr][Ll]%s*=%s*([^%s>]+)")

  if not url then return nil end

  url = trim(unescape_html(url))
  return { delay = delay, url = url }
end

function M.detect_client_redirect(html)
  -- 1) META REFRESH
  for tag in html:gmatch("<%s*[Mm][Ee][Tt][Aa][^>]*>") do
    local mr = parse_meta_refresh(tag)
    if mr then
      return {
        url   = mr.url,
        delay = mr.delay,
        type  = "meta",
        base  = M.extract_base(html),  -- may be nil
      }
    end
  end

  -- 2) JS location assignment patterns
  html = strip_js_comments(html)

  -- Minimal helpers
  local function esc(s)  -- escape Lua pattern magic
    return (s:gsub("([%%%^%$%(%)%.%[%]%*%+%-%?])", "%%%1"))
  end
  -- Make a case-insensitive Lua pattern from a plain string
  local function ci(s)
    return (s:gsub("%a", function(c)
      return "[" .. c:lower() .. c:upper() .. "]"
    end))
  end
  -- Your identifier set (order == precedence). Put *.href first if you want it to win.
  local idents = {
    "window.location.href", "location.href", "document.location.href", "top.location.href",
    "window.location", "location", "document.location", "top.location",
  }
  local methods = {
    "assign",
    "replace",
  }
  local captures = {
    [["([^"]+)"]],  -- double-quoted value
    [['([^']+)']],  -- single-quoted value
  }

  local patterns = {}
  for _, ident in ipairs(idents) do
    local ip = ci(esc(ident))
    -- Property assignment: *.location = "..."
    --                      *.location.href = "..."
    for _, cap in ipairs(captures) do
      table.insert(patterns, ip .. "%s*=%s*" .. cap)
    end

    -- Method calls only apply to *.location (not *.location.href)
    -- i.e. *.location.assign("...")  /  *.location.replace("...")
    if not ip:match("%%.href$") then
      for _, m in ipairs(methods) do
        for _, cap in ipairs(captures) do
          table.insert(patterns,
                       ip .. "%s*%.%s*" .. "%s*" .. m .. "%s*%(%s*" .. cap .. "%s*%)")
        end
      end
    end
  end

  for _, p in ipairs(patterns) do
    local u = html:match(p)
    if u then
      return {
        url   = trim(unescape_html(u)),
        delay = 0,
        type  = "js",
        base  = M.extract_base(html),
      }
    end
  end

  return nil
end

-- convenience
function M.build_client_redirect_result(html)
  return M.detect_client_redirect(html)
end


-------

-- parse the <title>…</title> (case‑insensitive)
function M.parse_title(html)
  local t = html:match("<[Tt][Ii][Tt][Ll][Ee]%s*>(.-)</[Tt][Ii][Tt][Ll][Ee]>") or ""
  return trim(t)
end

-- Grab <base href="..."> if present
function M.extract_base(html)
  local href =
    html:match("<%s*[Bb][Aa][Ss][Ee][^>]-[Hh][Rr][Ee][Ff]%s*=%s*['\"]([^'\"]+)['\"]")
    or html:match("<%s*[Bb][Aa][Ss][Ee][^>]-[Hh][Rr][Ee][Ff]%s*=%s*([^%s>]+)")
  return href and trim(unescape_html(href)) or nil
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
