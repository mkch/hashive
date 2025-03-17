# Hashive

[English](README.md)

Hashive 是一个单文件只读键值数据库。

简单来说，一个 Hashive 数据库就是一个存储在文件中的 `map[string]any`，无需全部读入内存即可进行快速查询。

## 设计目标

1. 单文件数据库

    一个 Hashive 数据库就是一个文件，无需全部读入内存即可查询。

2. 轻量

    不需要额外的配置步骤，开箱即用。不显著增加整个系统的复杂度。

3. 速度

    Hashive 追求和其他数据库一样（或更快）的查询速度.

4. 键值查询

    Hashive 只存储键值数据。通过字符串类型的键来查询对应的值。

5. 只读
    Hashive 针对只读做优化。可以用更复杂和更慢的写入来换取查询的速度。实际上，Hashive 数据库通常只写入（生成）一次。

很明显，前3个设计目标和 SQLite 数据库高度重合，但是 SQLite 不是键值数据库也不是只读数据库，对于键值查询和只读数据集的优化不足。第3和第4点和哈希表高度重合，但是哈希表需要全部载入内存才能使用。Hashive 就是要在 SQLite 和内存哈希表之间找到一个针对只读数据集的平衡点。

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
