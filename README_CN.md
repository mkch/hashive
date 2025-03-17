# Hashive

[English](README.md)

Hashive 是一个用 Go 语言实现的单文件只读键值数据库。

简单来说，一个 Hashive 数据库就是一个存储在文件中的 `map[string]any`，无需全部读入内存即可进行快速查找。

## 特性

1. 性能

    在特定场景下，Hashive 的查询速度是同样内容 SQLite3 数据库的5倍。

    Benchmark 数据如下（3次随机查询）：

    ```text
    Hashive    110955   10112 ns/op   568 B/op    21 allocs/op
    SQLite     22893    51927 ns/op   1760 B/op   57 allocs/op
    ```

2. 直接存储任何 Go 值

    除了常用的整数、浮点数、字符串、数组、关联对象（哈希表）外，任何受`encoding/gob`支持的类型均可以作为值存储。

## 示例

```go
package hashive_test

import (
    "fmt"
    "os"

    "github.com/mkch/hashive"
)

type Description struct {
    Content string
}

func Example() {
    value := map[string]any{
        "Key1": 123,
        "Key2": "456",
        "Owners": []any{
            map[string]any{
                "Name": "John",
                "Age":  28,
            },
            map[string]any{
                "Name": "Joe",
                "Age":  29,
                "Addr": "abc street",
            },
        },
        "Description": Description{"description here"},
    }

    const DB_FILE = "testdata/db"
    // Write
    err := hashive.WriteFile(DB_FILE, value)
    if err != nil {
        panic(err)
    }
    defer os.Remove(DB_FILE)

    // Read
    db, close, err := hashive.Open(DB_FILE, -1)
    if err != nil {
        panic(err)
    }
    defer close()

    v1, _ := db.Query("Key1") // db.Key1
    fmt.Println(v1)
    v2, _ := db.Query("Key2") // db.Key2
    fmt.Println(v2)
    var desc Description
    err = db.QueryGob(&desc, "Description") // db.Description
    fmt.Println(desc)
    name, _ := db.Query("Owners", "0", "Name") // db.Owners[0].Name
    fmt.Println(name)
    addr, _ := db.Query("Owners", "1", "Addr") // db.Owners[1].Addr
    fmt.Println(addr)

    // Output:
    // 123
    // 456
    // {description here}
    // John
    // abc street
}

```
