# LuaAsio

Simple transparent non-blocking, high concurrency I/O for LuaJIT. Only 500+ lines.

There are no callbacks, Asynchronous happens when you perform a non-blocking operation.

Lightweight, low resource usage, available for embedded devices.

Based on ```Boost.Asio (Header-only) ``` Library.

Tested on windows<!-- , ubuntu, openwrt.  -->

# Usage

Server side:
```Lua
local asio = require 'asio'

function connection_th(con)
    -- Issues a synchronous, but non-blocking I/O operation.
    local data = con:read(5)
    -- Still non-blocking
    con:write(data .. '-pong')
    con:close()
end

local s = asio.server('127.0.0.1', 1234, function(con)
    -- light threads running asynchronously at various I/O operation.
    asio.spawn_light_thread(connection_th, con)
end)

-- This event loop is blocked during code execution
asio.run()
```

Client side:
```Lua
local asio = require 'asio'

local ping_send = function(text)
    local con = asio.connect('localhost', '1234')
    con:write(text)
    con:read(10)
    con:close()
end

asio.spawn_light_thread(ping_send, 'ping1')
asio.spawn_light_thread(ping_send, 'ping2')

asio.run()
```

# Light Thread & non-blocking

Your code needs to execute in Light Thread, actually Light Threads are Lua coroutine that all running in one thread, so you don't have to worry about context switching overhead and race conditions.

When goes to a non-blocking operation, the current Light Thread will wait for completion (block), and then it switches to the other available Light Thread to continue execution, or handle new connection.

If you want to use multithreading, Client side can be simply achieved by multiple Lua State (use like torch/threads); Server side not supported yet, but easy to implement by Lua State pool. Although because non-blocking, there is high concurrency even in a single thread.
<!-- Server side has **threads** parameters in **asio.server** function. -->

# Example: Real Case

## Full duplex Transparent Proxy for Router
Big effect, so simple!

Client/Router:
```Lua
local asio = require 'asio'

function forward(from_con, to_con)
    while true do
        local data = from_con:read_some()
        if not data then break end
        --you can require "resty.aes" add aes_256_cfb:en/decrypt(data) here
        local ok = to_con:write(data)
        if not ok then break end
    end
end

function connection_th(downstream)
    local dest_addr = downstream:get_original_dst()
    local upstream = asio.connect('serverhost', 5678)
    if not upstream then return end
    local ok = upstream:write(dest_addr)
    if not ok then return end
    asio.spawn_light_thread(forward, downstream, upstream)
    asio.spawn_light_thread(forward, upstream, downstream)
end

local s = asio.server('127.0.0.1', 1234, function(downstream)
    asio.spawn_light_thread(connection_th, downstream)
end)

asio.run()
```

Server:
```Lua
local asio = require 'asio'

function connection_th(downstream)
    local dest_addr = downstream:read(128)
    local upstream = asio.connect(dest_addr)
    if not upstream then return end
    asio.spawn_light_thread(forward, downstream, upstream)
    asio.spawn_light_thread(forward, upstream, downstream)
end

local s = asio.server('127.0.0.1', 5678, function(downstream)
    asio.spawn_light_thread(connection_th, downstream)
end)

asio.run()
```



# Building

## Windows

```
./build.bat
```

## Linux

```
./build.sh
```

# Unit Test

```
./test/luajit ./test/test.lua
```

# Reference

**holder = asio.server(ip, point, accept_handler)**

Listening port starts accepting connections.

`accept_handler(conn)` is your callback function when new connection is established. If you want to perform non-blocking operations on `conn`, you need call `spawn_light_thread` first.

<!-- If **threads** greater than 1, will create a thread pool and randomly assign Light Threads to one of them. There is no inter-thread communication method, so your need other lua moudle to communication between each Light Thread.  -->

Server are automatically closed when the return value `holder` are garbage collected.

----

**conn, err_msg = asio.connect(host, port, resolve_v6=false)**

Connect to the host port. This is a non-blocking operation.

Resolve host name is **not** a non-blocking operation yet, So **use IP address**.

If there are no errors, return `conn`(module); otherwise, returns `nil`, `err_msg`(lua str).

----

**conn, err_msg = asio.connect(sockaddr_storage)**

Same as `connect(host, port)`. `sockaddr_storage` is return value of `conn:get_original_dst()`.

----

**asio.sleep(sec)**

Suspends the execution of the current light thread until the duration have elapse. This is a non-blocking operation.

----

**data, err_msg = conn:read(size)**

Read binary data of a specified size. This is a non-blocking operation.

If there are no errors, return `data`(lua str); otherwise, returns `nil`, `err_msg`(lua str).

----

**data, err_msg = conn:read_some()**

Read binary data until one or more bytes. This is a non-blocking operation.

If there are no errors, return `data`(lua str); otherwise, returns `nil`, `err_msg`(lua str).

----

**ok, err_msg = conn:write(data)**

Write the data(lua str) to connection. This is a non-blocking operation.

If there are no errors, return `true`; otherwise, returns `nil`, `err_msg`(lua str).

----

**nil = conn:close()**

Close a connection. No returns.

----

**asio.spawn_light_thread(function, arg1, arg2, ...)**

Create and run a light thread.


# License

LuaAsio is available under the MIT license.

Copyright (C) 2018, by Jianhao Zhang (Heerozh) (heeroz@gmail.com), All rights reserved.

