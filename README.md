Name
====
Block file read and write operations toolkit.


This tool class, used to block read and write files, is typically used for block uploads (server-side writes) or block downloads(local writes).

Inside the code base there is an openresty wrapper class, but the dynamic library for this library can also be used in other languages.

* About implementation:
	* When a file is written, there are two temporary files: a data file (.dat) and an index file (.mt).
	* .dat data file, support for writing in blocks. Block size is fixed.
	* .mt index file, with a 1024-byte file header. File header records the data file size, block size, creation time, data file md5, and some extended information. Index file file body part is a bitmap, Bitmap records which blocks have been written, and which blocks have not been written.
    

# Usage for openresty

### To load this library

compile the model

```
cd /path/to/blockfile/clibs/ && make
```

you need to specify this library's path in ngx_lua's lua_package_path directive. For example:

```nginx
http {
   lua_package_path '/path/to/blockfile/libs/?.lua;;';
   lua_package_cpath '/path/to/blockfile/clibs/?.so;;';
}
```

you use require to load the library into a local Lua variable:
```lua
local blockfile = require("resty.blockfile")
```

### run with resty
```
cd /path/to/blockfile
resty -I libs -I clibs test/usage.lua
resty -I libs -I clibs test/blockfile_test.lua
```

### Methods
[blockfile/test/usage.lua](test/usage.lua)

```
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
```


# Licence

This module is licensed under the 2-clause BSD license.

Copyright (c) 2017, Xiaojie Liu <jie123108@163.com>

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR AN
Y DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUD
ING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
