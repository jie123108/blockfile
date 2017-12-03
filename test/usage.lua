local blockfile = require("resty.blockfile")
local cjson = require "cjson"


ffi.cdef[[
typedef struct {
    int32_t expires;
    int32_t last_modified;
    char etag[64]; 
}extinfo_t;
]]

function encode_extinfo(extinfo_table)
    if extinfo_table == nil then 
        return nil 
    end
    local expires = extinfo_table.expires or 0
    local last_modified = extinfo_table.last_modified or 0
    local etag = extinfo_table.etag
    return ffi.new("extinfo_t", extinfo_table)
end

function decode_extinfo(extinfo_cdata)
    if extinfo_cdata == nil then 
        return nil 
    end
    if type(extinfo_cdata) ~= 'cdata' then
        ngx_log(ngx.ERR, "extinfo type invalid! type:", type(extinfo_cdata))
        return nil
    end
    local info = {  expires=tonumber(extinfo_cdata.expires), 
                    last_modified=tonumber(extinfo_cdata.last_modified),
                    etag = ffi.string(extinfo_cdata.etag)}
    return info
end

local blockfile_opts = {extinfo_type="extinfo_t", encode_extinfo=encode_extinfo, decode_extinfo=decode_extinfo}


function initrandom()
    math.randomseed(ngx.now())
end
initrandom()


function random_sample(str, len)
    local t= {}
    for i=1, len do
        local idx = math.random(#str)
        table.insert(t, string.sub(str, idx,idx))
    end
    return table.concat(t)
end

function random_block(size)
    return random_sample("abcdefhijklmnopqrstuvwxyz0123456789", size)
end


local function getfilename(dir)
    local filename = dir .. "/" .. "blockfile_" .. random_block(16) .. ".txt"

    return filename
end

function get_block_real_size(block_size, block_idx, filesize)
    local block_cnt = math.ceil(filesize/block_size)
    local real_block_size = block_size
    if block_idx == block_cnt-1 then        
        local tmpsize = filesize % block_size
        if tmpsize > 0 then
            real_block_size = tmpsize;
        end        
    end
   
    return real_block_size
end

local dir = "/tmp"

local filename = getfilename(dir)
-- for test only
blockfile.system("rm -f " .. filename .. "*", 32)

ngx.say("open test file:", filename)
-- blockfile open
local xf, err = blockfile:open(filename, nil, blockfile_opts)

local filesize = 1024+20
local block_size = 64
local create_time = ngx.time()
local expires = create_time + 3600*24*7
local last_modified = create_time
local block_cnt = math.ceil(filesize/block_size)
local extinfo = {expires=expires, last_modified=last_modified, etag="abcdefhijk"}
-- head init
local ok, err = xf:head_init(filesize, block_size, create_time, extinfo)
if not ok then 
    ngx.log(ngx.ERR, "head init failed! err:", tostring(err))
    ngx.exit(0)
end

local head, err = xf:get_file_head()
assert(err == nil)
ngx.say("head: ", cjson.encode(head))
-- check head init status
local inited, err = xf:head_is_inited()
ngx.say("inited: ", tostring(inited))

for block_idx=0,block_cnt-1 do 
    local block_real_size = get_block_real_size(block_size, block_idx, filesize)
    local buf = random_block(block_real_size)
    -- write a block
    local writed, err = xf:block_write(block_idx, 0, buf, block_real_size)
    assert(writed == block_real_size)
    ngx.say(string.format("write block [%d] writed: %d", block_idx, writed))
    -- set block status to: processed
    local ok, err = xf:block_set_processed(block_idx)
    assert(err == nil)
    ngx.say(string.format("set block [%d] to processed", block_idx))

    -- check block status
    local processed, err = xf:block_is_processed(block_idx)
    assert(processed == true)
    ngx.say(string.format("check block [%d] processed: %s", block_idx, processed))

    -- read a block
    local buf_read = ffi.new("char[" .. block_real_size .. "]")     
    local readed, err = xf:block_read(block_idx, buf_read, block_real_size)
    assert(readed == block_real_size)
    ngx.say(string.format("read block [%d] readed: %d", block_idx, readed))
end

-- check all block is processed
local processed_ok, err = xf:processed_ok()
assert(processed_ok == true)
ngx.say("processed ok:", processed_ok)

-- close blockfile
completed, ret = xf:close(1)
ngx.say("completed: ", completed)


