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

local function blockfile_open(filename, tmpfilename)
    return blockfile:open(filename, tmpfilename, blockfile_opts)
end

local function getfilename(dir)
    return dir .. "/" .. "blockfile_test.txt"
end

local function test_open(filename, xf_expect, err_expect)
    local xf, err = blockfile_open(filename)
    assert(xf == xf_expect)
    assert(err == err_expect)
    return xf
end

local function test_close(xf, chk_md5_sum, completed_expect, head_expect)
    local completed, head = xf:close(chk_md5_sum)
    print("completed:", completed)
    print("head:", head)
    assert(completed == completed_expect)
    if head_expect ~= nil then
        assert(head == head_expect)
    end
end

local function test_block_set_processed(xf, block_idx, ok_expect, err_expect)
    local ok, err = xf:block_set_processed(block_idx)
    --print("block_set_processed:", ok, err)
    assert(ok == ok_expect)
    assert(err == err_expect)
    print "TEST: test_block_set_processed ok"
end

local function test_block_is_processed(xf, block_idx, processed_expect, err_expect)
    local processed, err = xf:block_is_processed(block_idx)
    --print("test_block_is_processed:", ok, err)
    assert(processed == processed_expect)
    assert(err == err_expect)
    print "TEST: test_block_is_processed ok"
end

local function test_head_is_inited(xf, inited_expect, err_expect)
    local inited, err = xf:head_is_inited()    
    assert(inited == inited_expect)
    assert(err == err_expect)
    print "TEST: head_is_inited ok"
end

local function test_head_init(xf, filesize, block_size, create_time, extinfo, ok_expect, err_expect)
    local ok, err = xf:head_init(filesize, block_size, create_time, extinfo)
    assert(ok == ok_expect)
    assert(err == err_expect)
    print "TEST: test_head_init ok"
end

local function test_block_write(xf, block_idx, buf, size, writed_expect, err_expect)
    local writed, err = xf:block_write(block_idx, 0, buf, size)
    assert(writed == writed_expect)
    assert(err == err_expect)
    print "TEST: test_block_write ok"
end

local function test_block_read(xf, block_idx, buf, size, buf_expect, readed_expect, err_expect)
    local readed, err = xf:block_read(block_idx, buf, size)
    assert(readed == readed_expect)
    assert(err == err_expect)
    if readed > 0 then
        assert(ffi.string(buf, readed) == buf_expect)
    end
    print "TEST: test_block_read ok"
end

local function test_processed_ok(xf, processed_ok_expect, err_expect)
    local processed_ok, err = xf:processed_ok()
    assert(processed_ok == processed_ok_expect)
    assert(err == err_expect)
    print "TEST: test_processed_ok ok"
end


local function test_OpenClose(dir)
    local filename = getfilename(dir)
    blockfile.system("rm -f " .. filename .. "*", 32)
    local xf, err = blockfile_open(filename)
    assert(xf ~= nil)
    test_close(xf,nil, 0, nil)

    --close后进行其它操作.
    test_close(xf, nil, -1, "closed")
    test_block_set_processed(xf, 1, false, "closed")
    test_block_is_processed(xf, 1, nil, "closed")
    test_head_is_inited(xf, nil, "closed")
    test_head_init(xf, 0,0,0, {expires=321}, false, "closed")
    test_block_write(xf, 0,0,0, -1, "closed")
    test_block_read(xf, 0, 0, 0, 0, -1, "closed")
    test_processed_ok(xf, false, "closed")

    --打开失败。
    blockfile.system("echo 'test' > /tmp/blockfile_test.mt", 32)
    blockfile.system("echo 'test' > /tmp/blockfile_test.dat", 32)
    local xf, err = blockfile_open("/tmp/blockfile_test")
    assert(xf == nil)
    assert(err == "open failed")
end

local function check_head(head, filesize, block_size, create_time, block_cnt, extinfo)
    assert(head ~= nil)
    assert(head.filesize == filesize)
    assert(head.block_size == block_size)
    assert(head.create_time == create_time)
    assert(head.block_cnt == block_cnt)
    if extinfo ~= nil then 
        assert(head.extinfo ~= nil)
        assert(extinfo.expires == head.extinfo.expires)
        assert(extinfo.last_modified == head.extinfo.last_modified)
        assert(extinfo.etag == head.extinfo.etag)
    end
    print "TEST: check_head ok"
end

local function test_HeadInit(dir)
    local filename = getfilename(dir)
    blockfile.system("rm -f " .. filename .. "*", 32)
    local xf, err = blockfile_open(filename)
    assert(xf ~= nil)
    local filesize = 1024*1024*5+333553
    local block_size = 1024*64
    local create_time = 33
    local expires = 3600
    local last_modified = 44
    local block_cnt = math.ceil(filesize/block_size)
    local extinfo = {expires=54321, last_modified=98765, etag="abcdefhijk"}
    test_head_init(xf, filesize, block_size, create_time, extinfo, true, nil)
    test_head_is_inited(xf, true, nil)
    assert(xf.head ~= nil)
    check_head(xf.head, filesize, block_size, create_time, block_cnt, extinfo)
  
    test_close(xf,nil, 0, nil)

    -- open已经打开的.
    local xf, err = blockfile_open(filename)
    check_head(xf:get_file_head(), filesize, block_size, create_time, block_cnt, extinfo)
    --重复head_init
    create_time = create_time + 100
    expires = 600
    local extinfo = {expires=1234567, last_modified=345678, etag="ujujujikkkk"}
    test_head_init(xf, filesize, block_size, create_time, extinfo, true, nil)
    check_head(xf:get_file_head(), filesize, block_size, create_time, block_cnt, extinfo)
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

local function test_BlockDownload(dir)
    local filename = getfilename(dir)
    blockfile.system("rm -f " .. filename .. "*", 32)
    local xf, err = blockfile_open(filename)
    assert(xf ~= nil)
    local filesize = 30
    local block_size = 8
    local create_time = 1
    local expires = 2
    local last_modified = 3
    local block_cnt = math.ceil(filesize/block_size)
    test_head_init(xf, filesize, block_size, create_time, {}, true, nil)

    for block_idx=0,block_cnt-1 do 
        local block_real_size = get_block_real_size(block_size, block_idx, filesize)
        local c = string.char(65 + block_idx)
        local chars = {}
        for i=1,block_real_size do 
            table.insert(chars, c)
        end
        local strs = table.concat(chars)
        test_block_write(xf, block_idx, strs, block_real_size, block_real_size, nil)
        test_block_set_processed(xf, block_idx, true, nil)

        test_block_is_processed(xf, block_idx, true, nil)
        local buf = ffi.new("char[" .. block_real_size .. "]")     
        test_block_read(xf, block_idx, buf, block_real_size, strs, block_real_size, nil)
    end

    test_processed_ok(xf, true, nil)
end

local function test_extinfo()
    local extinfo_raw = {expires=33, last_modified=42, etag="abcdef"}
    local extinfo = ffi.new("extinfo_t", extinfo_raw)
    local extinfo_table = decode_extinfo(extinfo)
    assert(extinfo_raw.expires == extinfo_table.expires)
    assert(extinfo_raw.last_modified == extinfo_table.last_modified)
    assert(extinfo_raw.etag == extinfo_table.etag)

    extinfo_table.etag = "9999999"
    local extinfo_cdata = encode_extinfo(extinfo_table)
    assert(extinfo_cdata.expires == extinfo_table.expires)
    assert(extinfo_cdata.last_modified == extinfo_table.last_modified)
    assert(ffi.string(extinfo_cdata.etag) == extinfo_table.etag)
    print("TEST: test_extinfo ok")
end

local function test_Main(dir)
    test_OpenClose(dir)
    test_HeadInit(dir)
    test_BlockDownload(dir)
    test_extinfo()
end

test_Main("/tmp")


