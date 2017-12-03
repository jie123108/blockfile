--[[
author: jie123108@163.com
date: 20171202
]]
local cjson = require "cjson"
local lock = require "resty.lock"
require("resty.blockfile_init")
local _M = {}

local ngx_log = nil
if ngx == nil then
    ngx_log = print
    ngx = {
        INFO = "INFO",
        ERR = "ERR",
    }
else
    ngx_log = ngx.log
end

local function find_shared_obj(cpath, so_name)
    --ngx.log(ngx.INFO, "cpath:", cpath, ",so_name:", so_name)
    for k, v in string.gmatch(cpath, "[^;]+") do
        local so_path = string.match(k, "(.*/)")
        --ngx.log(ngx.INFO, "so_path:", so_path)

        if so_path then
            -- "so_path" could be nil. e.g, the dir path component is "."
            so_path = so_path .. so_name

            -- Don't get me wrong, the only way to know if a file exist is
            -- trying to open it.
            local f = io.open(so_path)
            if f ~= nil then
                io.close(f)
                return so_path
            end
        end
    end
end

local function get_blockfile()    
    local so_path = find_shared_obj(package.cpath, "libblockfile.so")
    if so_path == nil then
        ngx_log(ngx.ERR, "### can't find libblockfile.so in :" .. package.cpath)
        return nil
    end
   ngx_log(ngx.INFO, "load so_path:", so_path)
   return ffi.load(so_path)
end

_M.blockfile = get_blockfile()

function _M.base16_encode(bin, len)
--const char* base16_encode(const char *text, int length, char* base16);
    if len == nil or len == 0 then
        len = string.len(bin)
    end
    local hex = ffi.new("char[" .. len*2 .. "]")
    _M.blockfile.base16_encode(bin, len, hex)
    return ffi.string(hex, len*2)
end

function _M.md5sum(filename)
    local md5_hex = ffi.new("char[34]")
    local ok = tonumber(_M.blockfile.md5sum(filename, md5_hex))==0
    local md5 = nil
    if ok then
        md5 = ffi.string(md5_hex, 32)
    else 
        md5 = ffi.string(ffi.C.strerror(ffi.errno()))
    end
    return ok, md5
end

--int base16_decode(const char* base16, int length, char* bin);
function _M.base16_decode(base16)
    local len = string.len(base16)
    local bin = ffi.new("char[" .. math.floor(len/2) .. "]")
    if tonumber(_M.blockfile.base16_decode(base16, len, bin)) == 0 then
        return ffi.string(bin, math.floor(len/2))
    end
    return nil
end

function _M.system(cmd, bufsize)  
    if bufsize and bufsize > 0 then 
        local buf = ffi.new("char[" .. bufsize .. "]") 
        ffi.fill(buf, bufsize, 0)
        local psize = ffi.new("int[1]", bufsize)

        local ret = _M.blockfile.fm_system(cmd, buf, psize)
        if ret ~= 0 then
            local err = ffi.string(ffi.C.strerror(ret))
            ngx_log(ngx.ERR, "fm_system(", cmd, ") failed! errno:", ret, ",err:", err)
            return false, err
        end
        local output_len = tonumber(psize[0])
        local output = ffi.string(buf, output_len)
        ngx_log(ngx.INFO, "system(", cmd, "), output: ", output)
        return true,output
    else
        local ret = _M.blockfile.fm_system(cmd, nil, nil)
        if ret ~= 0 then
            local err = ffi.string(ffi.C.strerror(ret))
            ngx_log(ngx.ERR, "fm_system(", cmd, ") failed! errno:", ret, ",err:", err)
            return false, err
        end
        return true
    end
end

local mt = { __index = _M }
--[[
    opts: extinfo_type: extinfo struct type
          encode_extinfo: map to ffi struct 
          decode_extinfo: ffi struct to map
]]
function _M:open(filename, tmpfilename, opts)
    local xf = _M.blockfile.x_open(filename, tmpfilename)
    if xf == nil then
        local err = "open failed"
        if ffi.errno() ~= 0 then 
            err = ffi.string(ffi.C.strerror(ffi.errno()))
        end
        return nil, err
    end
    opts = opts or {}
    local obj = { xf = xf, filename = filename, tmpfilename=tmpfilename,is_open=true}
    obj.opts = opts
    return setmetatable(obj, mt)
end

local empty_md5 = "00000000000000000000000000000000"
-- ffi head to map
local function decode_head(head, opts)
    --if head ~= nil and type(head) == 'cdata' and ffi.string(head.magic, 3) == "XFL" then
    if type(head) ~= 'cdata' then
        ngx_log(ngx.ERR, "head type invalid! type:", type(head))
        return nil
    end
    if head ~= nil and ffi.string(head.magic, 3) == "BFI" then
        local filesize = tonumber(head.filesize)
        local block_size = tonumber(head.block_size)
        local block_cnt = tonumber(head.block_cnt)
        local block_processing = tonumber(head.block_processing)
        local block_processed = tonumber(head.block_processed)
        local version = tonumber(head.version)
        local create_time = tonumber(head.create_time)
        local extinfo = nil 
        if opts and opts.extinfo_type and opts.decode_extinfo then 
            extinfo = opts.decode_extinfo(ffi.cast(opts.extinfo_type .."*", head.extinfo))
        end
        local md5 = _M.base16_encode(ffi.string(head.md5, 16), 16)
        if md5 == empty_md5 then md5 = nil end
        local head_info = {
                filesize=filesize, block_size=block_size, block_cnt=block_cnt,
                block_processing=block_processing, block_processed=block_processed,
                version=version, create_time=create_time, md5=md5, 
                extinfo = extinfo, 
            }
        return head_info
    end -- end of if ffi.string(head.magic, 4) == "BFI"
    return nil
end

function _M:close(chk_md5_sum)
    local dlcompleted = 0
    local ret = nil
    if self.is_open then
        local xf = self.xf
        chk_md5_sum = chk_md5_sum or 0
        local phead = ffi.new("blockfile_head_t[1]")    
        dlcompleted = tonumber(_M.blockfile.x_close(xf, phead, chk_md5_sum))
        local head = phead[0]
        local head_info = decode_head(head, self.opts)
        if head_info then
            self.head = head_info
        end

        self.is_open = false
        self.xf = nil
        ret = self.head
    else 
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
        dlcompleted = -1
        ret = "closed"
    end
    return dlcompleted, ret
end

function _M:set_make_dest_dir(make_dest_dir)
    local err = nil
    if self.is_open then
        local xf = self.xf
        local i_make_dest_dir = 0
        if make_dest_dir then
            i_make_dest_dir = 1
        end
        _M.blockfile.x_set_make_dest_dir(xf, i_make_dest_dir)
        return true
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end
    return false, err
end

function _M:block_is_processed(block_index)
    local processed = nil
    local err = nil
    if self.is_open then
        local xf = self.xf
        processed = _M.blockfile.x_block_is_processed(xf, block_index)>0
        
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end
    return processed, err
end

function _M:block_set_processed(block_index)
    local ok = false
    local err = nil
    if self.is_open then
        local xf = self.xf
        ok = _M.blockfile.x_block_set_processed(xf, block_index)==0
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end
    return ok, err
end

function _M:head_is_inited()
    local inited = nil
    local err = nil
    if self.is_open then
        local xf = self.xf
        inited = _M.blockfile.x_head_is_inited(xf) > 0
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end
    return inited, err
end

function _M:get_file_head()
    local err = nil
    if self.is_open then
        local xf = self.xf
        local xhead = _M.blockfile.x_get_file_head(xf)
        if xhead then
            local head = decode_head(xhead, self.opts)
            return head
        end
        return nil
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end
    return nil, err
end

function _M:head_init(filesize, block_size, create_time, extinfo)
    local ok = false
    local err = nil
    if self.is_open then
        local xf = self.xf
        local extinfo_cdata = nil 
        if extinfo and self.opts and self.opts.encode_extinfo then 
            extinfo_cdata = ffi.cast("char*",self.opts.encode_extinfo(extinfo))
        end
        ok = _M.blockfile.x_head_init(xf, filesize, block_size, create_time, extinfo_cdata)==0
        if ok then
            local head = self:get_file_head()
            if head then
                self.head = head
            end
        else
            err = ffi.string(ffi.C.strerror(ffi.errno()))
        end
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end
    return ok, err
end

function _M.reinit_head(filename, tmpfilename, create_time, extinfo)
    local xf, err = _M:open(filename, tmpfilename)
    if xf == nil then
        return false, err
    end
    if not xf:head_is_inited() then
        xf:close()
        return false, "not-inited"
    end
    local head = xf:get_file_head()
    if head == nil then
        xf:close()
        ngx.log(ngx.ERR, "file [", tmpfilename, "] inited, but file_head is nil")
        return false, "not-inited"
    end

    local extinfo_cdata = nil 
    if extinfo and self.opts and self.opts.encode_extinfo then 
        extinfo_cdata = ffi.cast("char*",self.opts.encode_extinfo(extinfo))
    end
    local ok, err = xf:head_init(head.filesize, head.block_size, create_time, extinfo_cdata)
    ngx.log(ngx.INFO, "file [", tmpfilename, "] reinit_head(create_time:", create_time, 
                ") ok:", ok, ", err:", tostring(err))
    xf:close()
    return ok, err
end

function _M.x_read_head(metafilename, opts)
    local phead = ffi.new("blockfile_head_t[1]")
    local ret = tonumber(_M.blockfile.x_read_head(metafilename, phead))
    if ret == 0 then
        local head = phead[0]
        local head_info = decode_head(head, opts)
        if head_info then
            return true, head_info
        else
            return false
        end
    else
        return false
    end
end


function _M:block_write(block_index, writed, buf, size)
    local writed_this = -1
    local err = nil

    if self.is_open then
        local xf = self.xf
        writed_this = _M.blockfile.x_block_write(xf, block_index, writed, buf, size)
        if writed_this == -1 then
            err = ffi.string(ffi.C.strerror(ffi.errno()))
        end
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end

    return writed_this, err
end

function _M:block_read(block_index, buf, size)
    local readed = -1
    local err = nil

    if self.is_open then
        local xf = self.xf
        readed = _M.blockfile.x_block_read(xf, block_index, buf, size)
        if readed == -1 then
            err = ffi.string(ffi.C.strerror(ffi.errno()))
        end
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end

    return readed, err
end

function _M:processed_ok()
    local processed_ok = false
    local err = nil

    if self.is_open then
        local xf = self.xf
        processed_ok = _M.blockfile.x_processed_ok(xf)>0
    else
        err = "closed"
        ngx_log(ngx.ERR, " file '", (self.filename or 'nil'), "' closed!")
    end
    
    return processed_ok, err
end

return _M
