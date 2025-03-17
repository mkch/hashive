# Hashive

[简体中文](README_CN.md)

Hashive is a single-file, read-only key-value database implemented in Go.

In simple terms, a Hashive database is essentially a `map[string]any` stored in a file, allowing for fast lookups without the need to load the entire file into memory.

## Features

1. Performance

    In specific scenarios, Hashive's query speed is 5 times faster than that of an SQLite3 database with the same content.

    Benchmark(3 random queries):

    ```text
    Hashive    110955   10112 ns/op   568 B/op    21 allocs/op
    SQLite     22893    51927 ns/op   1760 B/op   57 allocs/op
    ```

2. Direct Storage of Any Go Value

    In addition to commonly used types such as integers, floating-point numbers, strings, arrays, and associative objects (hash maps), any type supported by `encoding/gob` can be stored as a value.

## Example

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
